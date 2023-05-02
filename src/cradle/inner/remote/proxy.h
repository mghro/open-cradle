#ifndef CRADLE_INNER_REMOTE_PROXY_H
#define CRADLE_INNER_REMOTE_PROXY_H

#include <memory>
#include <string>
#include <tuple>

#include <spdlog/spdlog.h>

#include <cradle/inner/requests/generic.h>
#include <cradle/inner/service/seri_result.h>

namespace cradle {

// Thrown if an error occurred on a remote (server), or while communicating
// with a remote.
class remote_error : public std::logic_error
{
 public:
    remote_error(std::string const& what) : std::logic_error(what)
    {
    }

    remote_error(std::string const& what, std::string const& msg)
        : std::logic_error(fmt::format("{}: {}", what, msg))
    {
    }
};

// Minimal descriptor for a child node in an asynchronous context tree on a
// remote.
// This is a tuple because msgpack has built-in support for tuples but not
// for structs.
// The first element is the value identifying the child context.
// The second element is true for a request, false for a plain value.
using remote_context_spec = std::tuple<async_id, bool>;

// Minimal descriptor for the children of a node in an asynchronous context
// tree on a remote.
using remote_context_spec_list = std::vector<remote_context_spec>;

/*
 * Proxy for a remote (server) capable of resolving requests, synchronously
 * and/or asynchronously.
 * All remote calls throw on error.
 * TODO Only remote_error should be thrown
 */
class remote_proxy
{
 public:
    virtual ~remote_proxy() = default;

    // Returns the name of this proxy
    virtual std::string
    name() const
        = 0;

    // Returns the logger associated with this proxy
    virtual spdlog::logger&
    get_logger()
        = 0;

    // Resolves a request, synchronously.
    // ctx will be the root of a context tree.
    // TODO formalize "context object is root"
    virtual serialized_result
    resolve_sync(
        remote_context_intf& ctx,
        std::string domain_name,
        std::string seri_req)
        = 0;

    // Submits a request for asynchronous resolution.
    // ctx will be the root of a context tree.
    // Returns the remote id of the server's remote context associated with
    // the root request in the request tree. Other remote contexts will likely
    // be constructed only when the request is deserialized, and that could
    // take some time.
    virtual async_id
    submit_async(
        remote_context_intf& ctx,
        std::string domain_name,
        std::string seri_req)
        = 0;

    // Returns the specification of the child contexts of the context subtree
    // of which aid is the root.
    // Should be called for the root aid (returned from submit_async) only
    // when its status is SUBS_RUNNING, SELF_RUNNING or FINISHED.
    virtual remote_context_spec_list
    get_sub_contexts(async_id aid)
        = 0;

    // Returns the status of the remote context specified by aid.
    virtual async_status
    get_async_status(async_id aid)
        = 0;

    // Returns an error message
    // Should be called only when status == ERROR
    virtual std::string
    get_async_error_message(async_id aid)
        = 0;

    // Returns the value that request resolution calculated. root_aid should
    // be the return value of a former submit_async() call. The status of the
    // root context should be FINISHED.
    virtual serialized_result
    get_async_response(async_id root_aid)
        = 0;

    // Requests for an asynchronous resolution to be cancelled. aid should
    // specify a context in the tree.
    virtual void
    request_cancellation(async_id aid)
        = 0;

    // Finishes an asynchronous resolution, giving the server a chance to clean
    // up its administration associated with the resolution. Should be called
    // even when the resolution did not finish successfully (e.g. an
    // exception was thrown).
    virtual void
    finish_async(async_id root_aid)
        = 0;
};

// Registers a proxy.
void
register_proxy(std::shared_ptr<remote_proxy> proxy);

// Returns the proxy with the given name.
// Throws if the proxy was not found.
remote_proxy&
find_proxy(std::string const& name);

} // namespace cradle

#endif
