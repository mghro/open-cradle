#ifndef CRADLE_WEBSOCKET_CLIENT_H
#define CRADLE_WEBSOCKET_CLIENT_H

#include <cradle/typing/core.h>

namespace cradle {

struct websocket_server_message;
struct websocket_client_message;

struct websocket_client_impl;

CRADLE_DEFINE_EXCEPTION(websocket_client_error)
// This exception provides internal_error_message_info.

struct websocket_client
{
    websocket_client();
    ~websocket_client();

    void
    connect(string const& uri);

    void
    set_message_handler(
        std::function<void(websocket_server_message const& message)> handler);

    void
    set_open_handler(std::function<void()> handler);

    void
    send(websocket_client_message const& message);

    void
    run();

    void
    close();

 private:
    websocket_client_impl* impl_;
};

} // namespace cradle

#endif
