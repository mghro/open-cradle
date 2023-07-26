#ifndef CRADLE_INNER_RESOLVE_SERI_REQ_H
#define CRADLE_INNER_RESOLVE_SERI_REQ_H

// Service to resolve a serialized request to a serialized response,
// either locally or remotely

#include <string>

#include <cppcoro/task.hpp>

#include <cradle/inner/requests/generic.h>
#include <cradle/inner/resolve/seri_result.h>

namespace cradle {

/**
 * Resolves a serialized request to a serialized response
 *
 * ctx indicates where the resolution should happen: locally or remotely.
 * If the request is to be resolved locally, it must exist in the catalog
 * (otherwise, it should exist in the remote's catalog).
 *
 * Resolving a request yields a value with a request-dependent type, such as
 * int, double, blob or string.
 * Anywhere we have a serialized request, the response should also be
 * serialized. So, this function's return type is the serialized value;
 * currently(?), this will be a MessagePack string.
 */
cppcoro::task<serialized_result>
resolve_serialized_request(context_intf& ctx, std::string seri_req);

/**
 * Resolves a serialized request to a serialized response, remotely
 */
cppcoro::task<serialized_result>
resolve_serialized_remote(remote_context_intf& ctx, std::string seri_req);

/**
 * Resolves a serialized request to a serialized response, locally
 */
cppcoro::task<serialized_result>
resolve_serialized_local(local_context_intf& ctx, std::string seri_req);

} // namespace cradle

#endif