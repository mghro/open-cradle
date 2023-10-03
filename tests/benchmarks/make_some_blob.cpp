#include <stdexcept>

#include <benchmark/benchmark.h>
#include <cppcoro/sync_wait.hpp>
#include <cppcoro/task.hpp>
#include <fmt/format.h>

#include <cradle/inner/remote/loopback.h>
#include <cradle/inner/service/resources.h>
#include <cradle/plugins/domain/testing/domain_factory.h>
#include <cradle/plugins/domain/testing/requests.h>
#include <cradle/rpclib/client/proxy.h>
#include <cradle/rpclib/client/registry.h>

#include "../support/inner_service.h"
#include "benchmark_support.h"

using namespace cradle;
using namespace std;

static void
register_remote_services(
    inner_resources& resources, std::string const& proxy_name)
{
    static bool registered_domain = false;
    if (!registered_domain)
    {
        // TODO register_and_initialize_testing_domain();
        registered_domain = true;
    }
    if (proxy_name == "loopback")
    {
        auto loopback{std::make_unique<loopback_service>(
            make_inner_tests_config(), resources)};
        resources.register_domain(create_testing_domain(resources));
        resources.register_proxy(std::move(loopback));
    }
    else if (proxy_name == "rpclib")
    {
        register_rpclib_client(make_inner_tests_config(), resources);
    }
    else
    {
        throw std::invalid_argument(
            fmt::format("Unknown proxy name {}", proxy_name));
    }
}

template<caching_level_type caching_level, bool storing, typename Req>
void
BM_try_resolve_testing_request(
    benchmark::State& state, Req const& req, std::string const& proxy_name)
{
    inner_resources resources;
    init_test_inner_service(resources);
    bool remotely = proxy_name.size() > 0;
    if (remotely)
    {
        register_remote_services(resources, proxy_name);
    }
    testing_request_context ctx{resources, nullptr, remotely, proxy_name};

    // Fill the appropriate cache if any
    auto init = [&]() -> cppcoro::task<void> {
        if constexpr (caching_level != caching_level_type::none)
        {
            benchmark::DoNotOptimize(co_await resolve_request(ctx, req));
            if constexpr (caching_level == caching_level_type::full)
            {
                sync_wait_write_disk_cache(resources);
            }
        }
        co_return;
    };
    cppcoro::sync_wait(init());

    int num_loops = static_cast<int>(state.range(0));
    for (auto _ : state)
    {
        auto loop = [&]() -> cppcoro::task<void> {
            for (int i = 0; i < num_loops; ++i)
            {
// Visual C++ 14.30 (2022) seems to be overly eager in reporting unused
// variables. (If the variable is only used in a path that is never taken for
// some values of constexpr variables, it complains.)
#ifdef _MSC_VER
#pragma warning(push)
#pragma warning(disable : 4189)
#endif
                constexpr bool need_empty_memory_cache
                    = caching_level == caching_level_type::full || storing;
                constexpr bool need_empty_disk_cache
                    = caching_level == caching_level_type::full && storing;
                if constexpr (need_empty_memory_cache || need_empty_disk_cache)
                {
                    // Some scenarios are problematic for some reason
                    // (huge CPU times, only one iteration).
                    // Not stopping and resuming timing gives some improvement.
                    constexpr bool problematic
                        = caching_level == caching_level_type::none
                          || (caching_level == caching_level_type::memory
                              && storing);
                    constexpr bool pause_timing = !problematic;
                    if constexpr (pause_timing)
                    {
                        state.PauseTiming();
                    }
                    if constexpr (need_empty_memory_cache)
                    {
                        resources.reset_memory_cache();
                    }
                    if constexpr (need_empty_disk_cache)
                    {
                        clear_disk_cache(resources);
                    }
                    if constexpr (pause_timing)
                    {
                        state.ResumeTiming();
                    }
                }
                benchmark::DoNotOptimize(co_await resolve_request(ctx, req));
#ifdef _MSC_VER
#pragma warning(pop)
#endif
            }
            co_return;
        };
        cppcoro::sync_wait(loop());
    }
}

template<caching_level_type caching_level, bool storing, typename Req>
void
BM_resolve_testing_request(
    benchmark::State& state, Req const& req, std::string const& proxy_name)
{
    try
    {
        BM_try_resolve_testing_request<caching_level, storing, Req>(
            state, req, proxy_name);
    }
    catch (std::exception& e)
    {
        handle_benchmark_exception(state, e.what());
    }
    catch (...)
    {
        handle_benchmark_exception(state, "Caught unknown exception");
    }
}

enum class remoting
{
    none,
    loopback,
    copy,
    shared
};

template<
    caching_level_type caching_level,
    bool storing,
    size_t size = 10240,
    remoting remote = remoting::none>
void
BM_resolve_make_some_blob(benchmark::State& state)
{
    std::string proxy_name;
    bool shared = false;
    switch (remote)
    {
        case remoting::none:
            shared = false;
            break;
        case remoting::loopback:
            proxy_name = "loopback";
            shared = false;
            break;
        case remoting::copy:
            proxy_name = "rpclib";
            shared = false;
            break;
        case remoting::shared:
            proxy_name = "rpclib";
            shared = true;
            break;
    }
    auto req{rq_make_some_blob<caching_level>(size, shared)};
    BM_resolve_testing_request<caching_level, storing>(state, req, proxy_name);
}

constexpr auto full = caching_level_type::full;
constexpr size_t tenK = 10'240;
constexpr size_t oneM = 1'048'576;

BENCHMARK(BM_resolve_make_some_blob<caching_level_type::none, false, tenK>)
    ->Name("BM_resolve_make_some_blob_uncached_10K")
    ->Apply(thousand_loops);
BENCHMARK(BM_resolve_make_some_blob<caching_level_type::none, false, oneM>)
    ->Name("BM_resolve_make_some_blob_uncached_1M")
    ->Apply(thousand_loops);
BENCHMARK(BM_resolve_make_some_blob<caching_level_type::memory, true, tenK>)
    ->Name("BM_resolve_make_some_blob_store_to_mem_cache_10K")
    ->Apply(thousand_loops);
BENCHMARK(BM_resolve_make_some_blob<caching_level_type::memory, true, oneM>)
    ->Name("BM_resolve_make_some_blob_store_to_mem_cache_1M")
    ->Apply(thousand_loops);
BENCHMARK(BM_resolve_make_some_blob<caching_level_type::memory, false, tenK>)
    ->Name("BM_resolve_make_some_blob_mem_cached_10K")
    ->Apply(thousand_loops);
BENCHMARK(BM_resolve_make_some_blob<caching_level_type::memory, false, oneM>)
    ->Name("BM_resolve_make_some_blob_mem_cached_1M")
    ->Apply(thousand_loops);
BENCHMARK(BM_resolve_make_some_blob<caching_level_type::full, false, tenK>)
    ->Name("BM_resolve_make_some_blob_disk_cached_10K")
    ->Apply(thousand_loops);
BENCHMARK(BM_resolve_make_some_blob<caching_level_type::full, false, oneM>)
    ->Name("BM_resolve_make_some_blob_disk_cached_1M")
    ->Apply(thousand_loops);
#if 0
/*
Current/previous problems with benchmarking disk caching:
(a) The disk cache wasn't be cleared between runs; this has been fixed.
(b) A race condition: issue #231.
*/
BENCHMARK(BM_resolve_make_some_blob<caching_level_type::full, true>)
    ->Name("BM_resolve_make_some_blob_store_to_disk_cache")
    ->Apply(thousand_loops);
BENCHMARK(BM_resolve_make_some_blob<caching_level_type::full, false>)
    ->Name("BM_resolve_make_some_blob_load_from_disk_cache")
    ->Apply(thousand_loops);
#endif

BENCHMARK(BM_resolve_make_some_blob<full, false, tenK, remoting::loopback>)
    ->Name("BM_resolve_make_some_blob_loopback_10K")
    ->Apply(thousand_loops);
BENCHMARK(BM_resolve_make_some_blob<full, false, oneM, remoting::loopback>)
    ->Name("BM_resolve_make_some_blob_loopback_1M")
    ->Apply(thousand_loops);
BENCHMARK(BM_resolve_make_some_blob<full, false, tenK, remoting::copy>)
    ->Name("BM_resolve_make_some_blob_rpclib_copy_10K")
    ->Apply(thousand_loops);
BENCHMARK(BM_resolve_make_some_blob<full, false, oneM, remoting::copy>)
    ->Name("BM_resolve_make_some_blob_rpclib_copy_1M")
    ->Apply(thousand_loops);
BENCHMARK(BM_resolve_make_some_blob<full, false, tenK, remoting::shared>)
    ->Name("BM_resolve_make_some_blob_rpclib_shared_10K")
    ->Apply(thousand_loops);
BENCHMARK(BM_resolve_make_some_blob<full, false, oneM, remoting::shared>)
    ->Name("BM_resolve_make_some_blob_rpclib_shared_1M")
    ->Apply(thousand_loops);
