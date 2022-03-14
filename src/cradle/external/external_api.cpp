#include <cradle/external/external_api_impl.h>
#include <cradle/external_api.h>
#include <cradle/thinknode/calc.h>
#include <cradle/thinknode/iam.h>
#include <cradle/thinknode/iss.h>
#include <cradle/websocket/calculations.h>
#include <cradle/websocket/server_api.h>
#include <cradle/websocket/types.hpp>

namespace cradle {

namespace external {

api_service::api_service(api_service_config const& config)
    : pimpl_{std::make_unique<api_service_impl>(config)}
{
}

api_service::api_service(api_service&& that) : pimpl_{std::move(that.pimpl_)}
{
}

api_service::~api_service()
{
}

api_service
start_service(api_service_config const& config)
{
    return api_service(config);
}

cradle::service_config
make_service_config(api_service_config const& config)
{
    cradle::service_config result;
    if (config.memory_cache_unused_size_limit)
    {
        result.immutable_cache = cradle::immutable_cache_config{};
        result.immutable_cache->unused_size_limit
            = config.memory_cache_unused_size_limit.value();
    }
    if (config.disk_cache_directory || config.disk_cache_size_limit)
    {
        if (!config.disk_cache_size_limit)
        {
            CRADLE_THROW(
                external_api_violation()
                << reason_info("config.disk_cache_directory given but not "
                               "config.disk_cache_size_limit"));
        }
        result.disk_cache = cradle::disk_cache_config{};
        if (config.disk_cache_directory)
        {
            result.disk_cache->directory = config.disk_cache_directory.value();
        }
        result.disk_cache->size_limit = config.disk_cache_size_limit.value();
    }
    result.request_concurrency = config.request_concurrency;
    result.compute_concurrency = config.compute_concurrency;
    result.http_concurrency = config.http_concurrency;
    return result;
}

api_service_impl::api_service_impl(api_service_config const& config)
    : service_core_{make_service_config(config)}
{
}

api_session::api_session(
    api_service& service, api_thinknode_session_config const& config)
    : pimpl_{std::make_unique<api_session_impl>(service.impl(), config)}
{
}

api_session::api_session(api_session&& that) : pimpl_{std::move(that.pimpl_)}
{
}

api_session::~api_session()
{
}

cradle::service_core&
get_service_core(api_session& session)
{
    return session.impl().get_service_core();
}

api_session
start_session(api_service& service, api_thinknode_session_config const& config)
{
    return api_session(service, config);
}

api_session_impl::api_session_impl(
    api_service_impl& service, api_thinknode_session_config const& config)
    : service_{service},
      thinknode_session_{
          cradle::make_thinknode_session(config.api_url, config.access_token)}
{
}

cppcoro::task<std::string>
get_context_id(api_session& session, std::string realm)
{
    return cradle::get_context_id(
        session.impl().get_service_core(),
        session.impl().get_thinknode_session(),
        std::move(realm));
}

cppcoro::shared_task<blob>
get_iss_object(
    api_session& session,
    std::string context_id,
    std::string object_id,
    bool ignore_upgrades)
{
    return cradle::get_iss_blob(
        session.impl().get_service_core(),
        session.impl().get_thinknode_session(),
        std::move(context_id),
        std::move(object_id),
        ignore_upgrades);
}

cppcoro::shared_task<std::string>
resolve_iss_object_to_immutable(
    api_session& session,
    std::string context_id,
    std::string object_id,
    bool ignore_upgrades)
{
    return cradle::resolve_iss_object_to_immutable(
        session.impl().get_service_core(),
        session.impl().get_thinknode_session(),
        std::move(context_id),
        std::move(object_id),
        ignore_upgrades);
}

cppcoro::shared_task<std::map<std::string, std::string>>
get_iss_object_metadata(
    api_session& session, std::string context_id, std::string object_id)
{
    return cradle::get_iss_object_metadata(
        session.impl().get_service_core(),
        session.impl().get_thinknode_session(),
        std::move(context_id),
        std::move(object_id));
}

cppcoro::shared_task<std::string>
post_iss_object(
    api_session& session,
    std::string context_id,
    std::string schema,
    blob object_data)
{
    return cradle::post_iss_object(
        session.impl().get_service_core(),
        session.impl().get_thinknode_session(),
        std::move(context_id),
        std::move(cradle::parse_url_type_string(schema)),
        object_data);
}

cppcoro::task<>
copy_iss_object(
    api_session& session,
    std::string source_context_id,
    std::string destination_context_id,
    std::string object_id)
{
    auto source_bucket = co_await cradle::get_context_bucket(
        session.impl().get_service_core(),
        session.impl().get_thinknode_session(),
        source_context_id);
    co_await cradle::deeply_copy_iss_object(
        session.impl().get_service_core(),
        session.impl().get_thinknode_session(),
        std::move(source_bucket),
        std::move(source_context_id),
        std::move(destination_context_id),
        std::move(object_id));
}

cppcoro::task<>
copy_calculation(
    api_session& session,
    std::string source_context_id,
    std::string destination_context_id,
    std::string calculation_id)
{
    auto source_bucket = co_await cradle::get_context_bucket(
        session.impl().get_service_core(),
        session.impl().get_thinknode_session(),
        source_context_id);
    co_await cradle::deeply_copy_calculation(
        session.impl().get_service_core(),
        session.impl().get_thinknode_session(),
        std::move(source_bucket),
        std::move(source_context_id),
        std::move(destination_context_id),
        std::move(calculation_id));
}

cppcoro::task<dynamic>
resolve_calc_to_value(
    api_session& session, string context_id, calculation_request request)
{
    return cradle::resolve_calc_to_value(
        session.impl().get_service_core(),
        session.impl().get_thinknode_session(),
        std::move(context_id),
        std::move(request));
}

cppcoro::task<std::string>
resolve_calc_to_iss_object(
    api_session& session, string context_id, calculation_request request)
{
    return cradle::resolve_calc_to_iss_object(
        session.impl().get_service_core(),
        session.impl().get_thinknode_session(),
        std::move(context_id),
        std::move(request));
}

cppcoro::task<calculation_request>
retrieve_calculation_request(
    api_session& session, std::string context_id, std::string calculation_id)
{
    co_return as_generic_calc(co_await cradle::retrieve_calculation_request(
        session.impl().get_service_core(),
        session.impl().get_thinknode_session(),
        std::move(context_id),
        std::move(calculation_id)));
}

} // namespace external

} // namespace cradle
