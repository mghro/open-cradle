#include <functional>
#include <stdexcept>
#include <thread>

#include <cppcoro/sync_wait.hpp>
#include <cppcoro/task.hpp>
#include <rpc/this_handler.h>

#include <cradle/inner/core/exception.h>
#include <cradle/inner/core/fmt_format.h>
#include <cradle/inner/io/mock_http.h>
#include <cradle/inner/requests/domain.h>
#include <cradle/inner/resolve/seri_req.h>
#include <cradle/rpclib/server/handlers.h>
#include <cradle/typing/service/core.h>

namespace cradle {

rpclib_handler_context::rpclib_handler_context(
    service_config const& config,
    service_core& service,
    spdlog::logger& logger)
    : service_{service},
      testing_{
          config.get_bool_or_default(generic_config_keys::TESTING, false)},
      logger_{logger},
      request_pool_{
          static_cast<BS::concurrency_t>(config.get_number_or_default(
              rpclib_config_keys::REQUEST_CONCURRENCY, 16))}
{
}

// [[noreturn]]
// Throws something that is handled inside the rpclib library
static void
handle_exception(rpclib_handler_context& hctx, std::exception& e)
{
    auto& logger{hctx.logger()};
    logger.error("caught {}", e.what());
    rpc::this_handler().respond_error(e.what());
}

static rpclib_response
resolve_sync(
    rpclib_handler_context& hctx,
    std::string domain_name,
    std::string seri_req)
{
    auto& service{hctx.service()};
    auto& logger{hctx.logger()};
    logger.info("resolve_sync {}: {}", domain_name, seri_req);
    auto dom = find_domain(domain_name);
    auto ctx{dom->make_sync_context(service, false, "")};
    auto& loc_ctx{cast_ctx_to_ref<local_context_intf>(*ctx)};
    auto seri_result = cppcoro::sync_wait(
        resolve_serialized_local(loc_ctx, std::move(seri_req)));
    // TODO try to get rid of .value()
    blob result = seri_result.value();
    logger.info("result {}", result);
    // TODO if the result references blob files, then create a response_id
    // uniquely identifying the set of those files
    static uint32_t response_id = 0;
    response_id += 1;
    return rpclib_response{response_id, std::move(result)};
}

rpclib_response
handle_resolve_sync(
    rpclib_handler_context& hctx,
    std::string domain_name,
    std::string seri_req)
try
{
    auto fut = hctx.request_pool().submit(
        resolve_sync,
        std::ref(hctx),
        std::move(domain_name),
        std::move(seri_req));
    return fut.get();
}
catch (std::exception& e)
{
    handle_exception(hctx, e);
    return rpclib_response{};
}

void
handle_ack_response(rpclib_handler_context& hctx, int response_id)
try
{
    auto& logger{hctx.logger()};
    logger.info("ack_response {}", response_id);
    // TODO release the temporary lock on the blob files referenced in
    // response #response_id
}
catch (std::exception& e)
{
    handle_exception(hctx, e);
}

void
handle_mock_http(rpclib_handler_context& hctx, std::string const& body)
try
{
    auto& session = enable_http_mocking(hctx.service());
    session.set_canned_response(make_http_200_response(body));
}
catch (std::exception& e)
{
    handle_exception(hctx, e);
}

static void
resolve_async(
    rpclib_handler_context& hctx,
    std::shared_ptr<local_async_context_intf> actx,
    std::string seri_req)
{
    auto& logger{hctx.logger()};
    if (hctx.delayed_resolve_async())
    {
        logger.warn("resolve_async forced startup delay");
        std::this_thread::sleep_for(std::chrono::milliseconds(500));
    }
    logger.info("resolve_async start");
    // TODO update status to STARTED or so
    try
    {
        blob res = cppcoro::sync_wait(
                       resolve_serialized_local(*actx, std::move(seri_req)))
                       .value();
        logger.info("resolve_async done: {}", res);
        if (actx->get_status() != async_status::FINISHED)
        {
            logger.error(
                "resolve_async finished but status is {}", actx->get_status());
        }
        actx->set_result(std::move(res));
    }
    catch (async_cancelled const&)
    {
        logger.warn("resolve_async: caught async_cancelled");
        actx->update_status(async_status::CANCELLED);
    }
    catch (std::exception& e)
    {
        logger.warn("resolve_async: caught error {}", e.what());
        actx->update_status_error(e.what());
    }
}

async_id
handle_submit_async(
    rpclib_handler_context& hctx,
    std::string const& domain_name,
    std::string const& seri_req)
try
{
    std::string actual_domain_name{domain_name};
    auto& service{hctx.service()};
    auto& logger{hctx.logger()};
    logger.info(
        "submit_async {}: {} ...", domain_name, seri_req.substr(0, 10));
    if (hctx.testing())
    {
        if (domain_name == "fail_submit_async")
        {
            logger.warn("submit_async: forced failure");
            throw remote_error{"submit_async forced failure"};
        }
        if (domain_name == "testing_delay_resolve_async")
        {
            logger.warn("forcing delayed resolve_async");
            hctx.force_delayed_resolve_async();
            actual_domain_name = "testing";
        }
    }
    auto dom = find_domain(actual_domain_name);
    auto ctx{dom->make_async_context(service, false, "")};
    auto actx = cast_ctx_to_shared_ptr<local_async_context_intf>(ctx);
    hctx.get_async_db().add(actx);
    // TODO update status to SUBMITTED
    // This function should return asap.
    // Need to dispatch a thread calling the blocking cppcoro::sync_wait().
    hctx.request_pool().push_task(
        resolve_async, std::ref(hctx), actx, seri_req);
    async_id aid = actx->get_id();
    logger.info("async_id {}", aid);
    return aid;
}
catch (std::exception& e)
{
    handle_exception(hctx, e);
    return async_id{};
}

remote_context_spec_list
handle_get_sub_contexts(rpclib_handler_context& hctx, async_id aid)
try
{
    auto& db{hctx.get_async_db()};
    auto& logger{hctx.logger()};
    logger.info("handle_get_sub_contexts {}", aid);
    auto actx{db.find(aid)};
    auto nsubs = actx->get_local_num_subs();
    logger.debug("  {} subs", nsubs);
    remote_context_spec_list result;
    for (decltype(nsubs) ix = 0; ix < nsubs; ++ix)
    {
        auto& sub_actx = actx->get_local_sub(ix);
        logger.debug(
            "  sub {}: id {} ({}) {}",
            ix,
            sub_actx.get_id(),
            sub_actx.is_req() ? "REQ" : "VAL",
            sub_actx.get_status());
        result.push_back(
            remote_context_spec{sub_actx.get_id(), sub_actx.is_req()});
    }
    return result;
}
catch (std::exception& e)
{
    handle_exception(hctx, e);
    return remote_context_spec_list{};
}

int
handle_get_async_status(rpclib_handler_context& hctx, async_id aid)
try
{
    auto& db{hctx.get_async_db()};
    auto& logger{hctx.logger()};
    logger.info("handle_get_async_status {}", aid);
    auto actx{db.find(aid)};
    auto status = actx->get_status();
    logger.info("handle_get_async_status -> {}", status);
    return static_cast<int>(status);
}
catch (std::exception& e)
{
    handle_exception(hctx, e);
    return int{};
}

std::string
handle_get_async_error_message(rpclib_handler_context& hctx, async_id aid)
try
{
    auto& db{hctx.get_async_db()};
    auto& logger{hctx.logger()};
    logger.info("handle_get_async_error_message {}", aid);
    auto actx{db.find(aid)};
    auto errmsg = actx->get_error_message();
    logger.info("handle_get_async_error_message -> {}", errmsg);
    return errmsg;
}
catch (std::exception& e)
{
    handle_exception(hctx, e);
    return std::string{};
}

rpclib_response
handle_get_async_response(rpclib_handler_context& hctx, async_id root_aid)
try
{
    auto& db{hctx.get_async_db()};
    auto& logger{hctx.logger()};
    logger.info("handle_get_async_response {}", root_aid);
    auto actx{db.find(root_aid)};
    // TODO response_id
    uint32_t response_id = 0;
    return rpclib_response{response_id, actx->get_result()};
}
catch (std::exception& e)
{
    handle_exception(hctx, e);
    return rpclib_response{};
}

int
handle_request_cancellation(rpclib_handler_context& hctx, async_id aid)
try
{
    auto& db{hctx.get_async_db()};
    auto& logger{hctx.logger()};
    logger.info("handle_request_cancellation {}", aid);
    auto actx{db.find(aid)};
    actx->request_cancellation();
    return int{};
}
catch (std::exception& e)
{
    handle_exception(hctx, e);
    return int{};
}

int
handle_finish_async(rpclib_handler_context& hctx, async_id root_aid)
try
{
    auto& db{hctx.get_async_db()};
    auto& logger{hctx.logger()};
    logger.info("handle_finish_async {}", root_aid);
    db.remove_tree(root_aid);
    return int{};
}
catch (std::exception& e)
{
    handle_exception(hctx, e);
    return int{};
}

} // namespace cradle
