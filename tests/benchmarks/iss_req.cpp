#include <cradle/thinknode/iss_req.h>

#include <iostream>

//#include <benchmark/benchmark.h>
#include <cppcoro/sync_wait.hpp>
#include <cppcoro/task.hpp>

#include <cradle/inner/requests/value.h>
#include <cradle/inner/service/core.h>
#include <cradle/inner/service/request.h>
#include <cradle/typing/encodings/msgpack.h>
#include <cradle/typing/io/mock_http.h>
#include <cradle/typing/service/core.h>
#include <cradle/typing/utilities/testing.h>

#include "../inner/support/core.h"
#include "support.h"

using namespace cradle;

static void
set_mock_script(mock_http_session& mock_http, int num_loops)
{
    mock_http_script script;
    mock_http_exchange exchange{
        make_http_request(
            http_request_method::POST,
            "https://mgh.thinknode.io/api/v1.0/iss/string?context=123",
            {{"Authorization", "Bearer xyz"},
             {"Accept", "application/json"},
             {"Content-Type", "application/octet-stream"}},
            make_blob("payload")),
        make_http_200_response("{ \"id\": \"def\" }")};
    for (int i = 0; i < num_loops; ++i)
    {
        script.push_back(exchange);
    }
    mock_http.set_script(script);
}

#if 0
static string
expected_result(int num_loops)
{
    string result;
    for (int i = 0; i < num_loops; ++i)
    {
        result += "def";
    }
    return result;
}
#endif

template<Request Req>
cppcoro::task<string>
resolve_n_requests(int n, thinknode_request_context& ctx, Req const& req)
{
    string result;
    for (int i = 0; i < n; ++i)
    {
        result += co_await resolve_request(ctx, req);
    }
    co_return result;
}

#if 0
TEST_CASE("ISS POST", "[iss]")
{
    service_core service;
    init_test_service(service, true);
    auto& mock_http = enable_http_mocking(service);

    thinknode_session session;
    session.api_url = "https://mgh.thinknode.io/api/v1.0";
    session.access_token = "xyz";
    thinknode_request_context ctx{service, session, nullptr};
    string context_id{"123"};
    auto schema{
        make_thinknode_type_info_with_string_type(thinknode_string_type())};
    auto object_data{make_blob("payload")};
    auto req_none{rq_post_iss_object<caching_level_type::none>(
        session.api_url, context_id, schema, object_data)};
    auto req_mem{rq_post_iss_object<caching_level_type::memory>(
        session.api_url, context_id, schema, object_data)};

    BENCHMARK("create request, uncached")
    {
        return rq_post_iss_object<caching_level_type::none>(
            session.api_url, context_id, schema, object_data);
    };

    BENCHMARK("create request, memory cached")
    {
        return rq_post_iss_object<caching_level_type::memory>(
            session.api_url, context_id, schema, object_data);
    };

    BENCHMARK("resolve request x 10, uncached")
    {
        constexpr int num_loops = 10;
        set_mock_script(mock_http, num_loops);
        auto id
            = cppcoro::sync_wait(resolve_n_requests(num_loops, ctx, req_none));
        REQUIRE(id == expected_result(num_loops));
    };

    BENCHMARK("resolve request x 20, uncached")
    {
        constexpr int num_loops = 20;
        set_mock_script(mock_http, num_loops);
        auto id
            = cppcoro::sync_wait(resolve_n_requests(num_loops, ctx, req_none));
        REQUIRE(id == expected_result(num_loops));
    };

    BENCHMARK("resolve request, fill cache")
    {
        constexpr int num_loops = 2;
        set_mock_script(mock_http, num_loops);
        auto id
            = cppcoro::sync_wait(resolve_n_requests(num_loops, ctx, req_mem));
        REQUIRE(id == expected_result(num_loops));
    };

    BENCHMARK("resolve request x 10, memory cached")
    {
        constexpr int num_loops = 10;
        set_mock_script(mock_http, num_loops);
        auto id
            = cppcoro::sync_wait(resolve_n_requests(num_loops, ctx, req_mem));
        REQUIRE(id == expected_result(num_loops));
    };

    BENCHMARK("resolve request x 20, memory cached")
    {
        constexpr int num_loops = 20;
        set_mock_script(mock_http, num_loops);
        auto id
            = cppcoro::sync_wait(resolve_n_requests(num_loops, ctx, req_mem));
        REQUIRE(id == expected_result(num_loops));
    };
}
#endif

int my_counter = 0;

static void
BM_resolve_request_uncached(int num_loops)
{
    std::cout << "BM_resolve_request_uncached(" << num_loops << ")\n";
    service_core service;
    init_test_service(service, true);
    auto& mock_http = enable_http_mocking(service);

    thinknode_session session;
    session.api_url = "https://mgh.thinknode.io/api/v1.0";
    session.access_token = "xyz";
    thinknode_request_context ctx{service, session, nullptr};
    string context_id{"123"};
    auto schema{
        make_thinknode_type_info_with_string_type(thinknode_string_type())};
    auto object_data{make_blob("payload")};
    auto req_none{rq_post_iss_object<caching_level_type::none>(
        session.api_url, context_id, schema, object_data)};
    //    auto req_mem{rq_post_iss_object<caching_level_type::memory>(
    //        session.api_url, context_id, schema, object_data)};

    for (my_counter = 0; my_counter < 100000; ++my_counter)
    {
        set_mock_script(mock_http, num_loops);
        cppcoro::sync_wait(resolve_n_requests(num_loops, ctx, req_none));
    }
}

static void
BM_resolve_request_memory_cached(int num_loops)
{
    std::cout << "BM_resolve_request_memory_cached(" << num_loops << ")\n";
    service_core service;
    init_test_service(service, true);
    auto& mock_http = enable_http_mocking(service);

    thinknode_session session;
    session.api_url = "https://mgh.thinknode.io/api/v1.0";
    session.access_token = "xyz";
    thinknode_request_context ctx{service, session, nullptr};
    string context_id{"123"};
    auto schema{
        make_thinknode_type_info_with_string_type(thinknode_string_type())};
    auto object_data{make_blob("payload")};
    //    auto req_none{rq_post_iss_object<caching_level_type::none>(
    //        session.api_url, context_id, schema, object_data)};
    auto req_mem{rq_post_iss_object<caching_level_type::memory>(
        session.api_url, context_id, schema, object_data)};

    // Fill the memory cache
    set_mock_script(mock_http, 1000);
    cppcoro::sync_wait(resolve_n_requests(1000, ctx, req_mem));

    for (int i = 0; i < 100000; ++i)
    {
        set_mock_script(mock_http, num_loops);
        cppcoro::sync_wait(resolve_n_requests(num_loops, ctx, req_mem));
    }
}

// BENCHMARK(BM_resolve_request_uncached)->Arg(10)->Arg(20);
// BENCHMARK(BM_resolve_request_memory_cached)->Arg(10)->Arg(20);

int
main()
{
    BM_resolve_request_uncached(10);
    BM_resolve_request_uncached(20);
    BM_resolve_request_memory_cached(10);
    BM_resolve_request_memory_cached(20);
}
