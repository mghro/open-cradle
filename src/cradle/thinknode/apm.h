#ifndef CRADLE_THINKNODE_APM_H
#define CRADLE_THINKNODE_APM_H

#include <cradle/service/core.h>
#include <cradle/thinknode/types.hpp>

namespace cradle {

struct http_connection_interface;

// Query a particular version of an app.
cppcoro::shared_task<thinknode_app_version_info>
get_app_version_info(
    thinknode_request_context ctx, string account, string app, string version);

} // namespace cradle

#endif
