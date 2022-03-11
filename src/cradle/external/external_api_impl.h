#ifndef CRADLE_EXTERNAL_API_IMPL_H
#define CRADLE_EXTERNAL_API_IMPL_H

#include <cradle/core/exception.h>
#include <cradle/external_api.h>
#include <cradle/service/core.h>

namespace cradle {

namespace external {

CRADLE_DEFINE_EXCEPTION(external_api_violation)
CRADLE_DEFINE_ERROR_INFO(string, reason)

cradle::service_config
make_service_config(api_service_config const& config);

class api_service_impl
{
    cradle::service_core service_core_;

 public:
    api_service_impl(api_service_config const& config);

    cradle::service_core&
    get_service_core()
    {
        return service_core_;
    }
};

class api_session_impl
{
    api_service_impl& service_;
    cradle::thinknode_session thinknode_session_;

 public:
    api_session_impl(
        api_service_impl& service, api_thinknode_session_config const& config);

    cradle::service_core&
    get_service_core()
    {
        return service_.get_service_core();
    }

    cradle::thinknode_session&
    get_thinknode_session()
    {
        return thinknode_session_;
    }
};

} // namespace external

} // namespace cradle

#endif
