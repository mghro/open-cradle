#ifndef CRADLE_INNER_RESOLVE_RESOLVE_REQUEST_H
#define CRADLE_INNER_RESOLVE_RESOLVE_REQUEST_H

#include <stdexcept>
#include <type_traits>
#include <utility>

#include <cppcoro/task.hpp>
#include <fmt/format.h>

#include <cradle/inner/caching/immutable/cache.h>
#include <cradle/inner/caching/immutable/local_locked_record.h>
#include <cradle/inner/caching/immutable/lock.h>
#include <cradle/inner/caching/immutable/ptr.h>
#include <cradle/inner/encodings/msgpack_value.h>
#include <cradle/inner/introspection/tasklet.h>
#include <cradle/inner/requests/cast_ctx.h>
#include <cradle/inner/requests/generic.h>
#include <cradle/inner/resolve/remote.h>
#include <cradle/inner/resolve/util.h>
#include <cradle/inner/service/resources.h>
#include <cradle/inner/service/secondary_cached_blob.h>
#include <cradle/inner/service/secondary_storage_intf.h>
#include <cradle/inner/utilities/logging.h>

/*
 * Service resolving a request to a value
 *
 * The public interface is resolve_request().
 */

namespace cradle {

// Constrain the options for resolve_request(). Useful because:
// (a) Code for a non-selected option might not compile
// (b) The generated object code size will be lower
// (c) The actual context class may not be known at the resolve_request()
//     call location (e.g. because the context object was created via the
//     domain interface class)
// (d) The actual context class might implement options that the caller is
//     not interested in (e.g., it's in a local-only environment)
template<
    bool ForceRemote = false,
    bool ForceLocal = false,
    bool ForceSync = false,
    bool ForceAsync = false,
    bool IsSub = false>
struct ResolutionConstraints
{
    static_assert(!(ForceRemote && ForceLocal));
    static_assert(!(ForceSync && ForceAsync));
    // IsSub is relevant only for async, so set to false if ForceSync
    // (preventing unnecessary template instantiations)
    static_assert(!(ForceSync && IsSub));

    static constexpr bool force_remote = ForceRemote;
    static constexpr bool force_local = ForceLocal;
    static constexpr bool force_sync = ForceSync;
    static constexpr bool force_async = ForceAsync;
    static constexpr bool is_sub = IsSub;

    ResolutionConstraints()
    {
    }
};

using NoResolutionConstraints
    = ResolutionConstraints<false, false, false, false, false>;
using ResolutionConstraintsLocal
    = ResolutionConstraints<false, true, false, false, false>;
using ResolutionConstraintsLocalSync
    = ResolutionConstraints<false, true, true, false, false>;
using ResolutionConstraintsLocalAsyncRoot
    = ResolutionConstraints<false, true, false, true, false>;
using ResolutionConstraintsLocalAsyncSub
    = ResolutionConstraints<false, true, false, true, true>;
using ResolutionConstraintsRemoteSync
    = ResolutionConstraints<true, false, true, false, false>;
using ResolutionConstraintsRemoteAsync
    = ResolutionConstraints<true, false, false, true, false>;

// These defaults should make it superfluous for the caller to specify the
// constraints, if the actual context class is final and known at the
// resolve_request() call location.
template<Context Ctx>
using DefaultResolutionConstraints = ResolutionConstraints<
    DefinitelyRemoteContext<Ctx>,
    DefinitelyLocalContext<Ctx>,
    DefinitelySyncContext<Ctx>,
    DefinitelyAsyncContext<Ctx>,
    false>;

// A context/constraints pair where the context can be used to resolve a
// request within the limits set by the constraints.
// Due to runtime polymorphism, a mismatch can be detected at compile time only
// if the context is final.
template<typename Ctx, typename Constraints>
concept MatchingContextConstraints
    = (!(Constraints::force_remote && DefinitelyLocalContext<Ctx>)
       && !(Constraints::force_local && DefinitelyRemoteContext<Ctx>)
       && !(Constraints::force_sync && DefinitelyAsyncContext<Ctx>)
       && !(Constraints::force_async && DefinitelySyncContext<Ctx>) );

// A request/constraints pair where the request can be resolved within the
// limits set by the constraints.
template<typename Req, typename Constraints>
concept MatchingRequestConstraints
    = (!(Req::is_proxy && Constraints::force_local));

// Resolves a request by directly calling its resolve_...() function.
template<Request Req, typename Constraints>
cppcoro::task<typename Req::value_type>
resolve_request_call(
    local_context_intf& ctx, Req const& req, Constraints constraints)
{
    // Third decision (based on constraints if possible): sync or async
    if constexpr (constraints.force_async)
    {
        auto& actx = cast_ctx_to_ref<local_async_context_intf>(ctx);
        return req.resolve_async(actx);
    }
    else if constexpr (constraints.force_sync)
    {
        return req.resolve_sync(ctx);
    }
    else
    {
        if (ctx.is_async())
        {
            auto& actx = cast_ctx_to_ref<local_async_context_intf>(ctx);
            return req.resolve_async(actx);
        }
        else
        {
            return req.resolve_sync(ctx);
        }
    }
}

// Resolves a request by directly calling its resolve_...() function;
// with introspection if the request wants that.
template<Request Req, typename Constraints>
    requires(!Req::introspective)
cppcoro::task<typename Req::value_type>
resolve_request_direct(
    local_context_intf& ctx, Req const& req, Constraints constraints)
{
    return resolve_request_call(ctx, req, constraints);
}

template<Request Req, typename Constraints>
    requires(Req::introspective)
cppcoro::task<typename Req::value_type>
resolve_request_direct(
    local_context_intf& ctx, Req const& req, Constraints constraints)
{
    auto& intr_ctx = cast_ctx_to_ref<introspective_context_intf>(ctx);
    co_await dummy_coroutine();
    coawait_introspection guard{
        intr_ctx,
        "resolve_request",
        fmt::format("{}/call", req.get_introspection_title())};
    co_return co_await resolve_request_call(ctx, req, constraints);
}

// Resolves a memory-cached request using some sort of secondary cache.
// A memory-cached request needs no secondary cache, so it can be resolved
// right away (by calling the request's function).
template<MemoryCachedRequest Req, typename Constraints>
cppcoro::task<typename Req::value_type>
resolve_secondary_cached(
    caching_context_intf& ctx, Req const& req, Constraints constraints)
{
    return resolve_request_direct(ctx, req, constraints);
}

// Resolves a fully-cached request using some sort of secondary cache, and some
// sort of serialization.
template<FullyCachedRequest Req, typename Constraints>
cppcoro::task<typename Req::value_type>
resolve_secondary_cached(
    caching_context_intf& ctx, Req const& req, Constraints constraints)
{
    using Value = typename Req::value_type;
    inner_resources& resources{ctx.get_resources()};
    auto create_blob_task = [&]() -> cppcoro::task<blob> {
        co_return serialize_value(
            co_await resolve_request_direct(ctx, req, constraints));
    };
    co_return deserialize_value<Value>(co_await secondary_cached_blob(
        resources, req.get_captured_id(), std::move(create_blob_task)));
}

// Called if the action cache contains no record for this request.
// Resolves the request, stores the result in the CAS, updates the action
// cache. The cache is accessed via ptr. The caller should ensure that ctx, req
// and ptr outlive the coroutine.
template<CachedRequest Req, typename Constraints>
cppcoro::shared_task<void>
resolve_request_on_memory_cache_miss(
    caching_context_intf& ctx,
    Req const& req,
    immutable_cache_ptr<typename Req::value_type>& ptr,
    Constraints constraints)
{
    try
    {
        ptr.record_value(
            co_await resolve_secondary_cached(ctx, req, constraints));
    }
    catch (...)
    {
        ptr.record_failure();
        throw;
    }
}

template<CompositionBasedCachedRequest Req, typename Constraints>
cppcoro::task<typename Req::value_type>
resolve_request_cached(
    caching_context_intf& ctx,
    Req const& req,
    cache_record_lock* lock_ptr,
    Constraints constraints)
{
    using value_type = typename Req::value_type;
    using ptr_type = immutable_cache_ptr<value_type>;
    // While ptr lives, the corresponding cache record lives too.
    // ptr lives until the shared_task has run (on behalf of the current
    // request, or a previous one), and the value has been retrieved from the
    // cache record.
    ptr_type ptr{
        ctx.get_resources().memory_cache(),
        req.get_captured_id(),
        [&ctx, &req, constraints](untyped_immutable_cache_ptr& ptr) {
            return resolve_request_on_memory_cache_miss(
                ctx, req, static_cast<ptr_type&>(ptr), constraints);
        }};
    if (lock_ptr != nullptr)
    {
        lock_ptr->set_record(
            std::make_unique<local_locked_cache_record>(ptr.get_record()));
    }
    if constexpr (IntrospectiveRequest<Req>)
    {
        auto& intr_ctx = cast_ctx_to_ref<introspective_context_intf>(ctx);
        // Have a dedicated tasklet track the co_await on ptr's shared_task.
        // Ensure that the tasklet's first timestamp coincides (almost) with
        // the "co_await shared_task".
        co_await dummy_coroutine();
        coawait_introspection guard{
            intr_ctx, "resolve_request", req.get_introspection_title()};
        // co_await' ptr's shared_task, ensuring that its value is available.
        co_await ptr.ensure_value_task();
    }
    else
    {
        // co_await' ptr's shared_task, ensuring that its value is available.
        co_await ptr.ensure_value_task();
    }
    // If function ran, status already will be FINISHED
    // If result came from cache, it will not yet be
    if (auto* actx = cast_ctx_to_ptr<local_async_context_intf>(ctx))
    {
        actx->update_status(async_status::FINISHED);
    }
    // Finally, return the shared_task's value.
    co_return ptr.get_value();
}

template<ValueBasedCachedRequest Req, typename Constraints>
cppcoro::task<typename Req::value_type>
resolve_request_cached(
    caching_context_intf& ctx,
    Req const& req,
    cache_record_lock* lock_ptr,
    Constraints constraints)
{
    // Make a CompositionBasedCachedRequest variant of req that has all
    // subrequests resolved and replaced by resulting values; then resolve
    // that request as any other request, using composition-based caching.
    co_return co_await resolve_request_cached(
        ctx, co_await req.make_flattened_clone(ctx), lock_ptr, constraints);
}

template<typename Val, typename Constraints>
    requires(!Request<Val>)
cppcoro::task<Val> resolve_request_local(
    local_context_intf& ctx,
    Val const& val,
    bool retrying,
    cache_record_lock* lock_ptr,
    Constraints constraints)
{
    // async status, if appropriate, should already be FINISHED
    co_return val;
}

template<Request Req, typename Constraints>
cppcoro::task<typename Req::value_type>
resolve_request_local(
    local_context_intf& ctx,
    Req const& req,
    bool retrying,
    cache_record_lock* lock_ptr,
    Constraints constraints)
{
    // Prepare and populate ctx if it is an async root.
    local_context_intf* new_ctx{&ctx};
    if constexpr (!constraints.is_sub && !constraints.force_sync)
    {
        bool async{};
        if constexpr (constraints.force_async)
        {
            async = true;
        }
        else
        {
            async = ctx.is_async();
        }
        if (async && !retrying)
        {
            root_local_async_context_intf* root_actx{};
            // The following cast should succeed if client uses atst_context or
            // similar
            if (auto* owner = cast_ctx_to_ptr<local_async_ctx_owner_intf>(ctx))
            {
                // (Re-)create ctx tree and root ctx; get the new root ctx
                root_actx = &owner->prepare_for_local_resolution();
                new_ctx = root_actx;
            }
            else
            {
                root_actx
                    = cast_ctx_to_ptr<root_local_async_context_intf>(ctx);
            }
            if (root_actx)
            {
                // Populate ctx with sub ctx's
                static_assert(VisitableRequest<Req>);
                req.accept(*root_actx->make_ctx_tree_builder());
            }
        }
    }

    // Second decision: cached or not
    if constexpr (UncachedRequest<Req>)
    {
        return resolve_request_direct(*new_ctx, req, constraints);
    }
    else
    {
        auto& cac_ctx = cast_ctx_to_ref<caching_context_intf>(*new_ctx);
        return resolve_request_cached(cac_ctx, req, lock_ptr, constraints);
    }
}

template<Request Req>
cppcoro::task<typename Req::value_type>
resolve_request_remote_coro(
    remote_context_intf& ctx, Req const& req, cache_record_lock* lock_ptr)
{
    // This runs in co_await resolve_request().
    co_return resolve_remote_to_value(ctx, req, lock_ptr);
}

template<Request Req>
cppcoro::task<typename Req::value_type>
resolve_request_remote(
    remote_context_intf& ctx, Req const& req, cache_record_lock* lock_ptr)
{
    // This runs in resolve_request(), which is where any preparation must
    // happen.
    if (auto* owner = cast_ctx_to_ptr<remote_async_ctx_owner_intf>(ctx))
    {
        // (Re-)create ctx tree and root ctx
        owner->prepare_for_remote_resolution();
    }
    return resolve_request_remote_coro(ctx, req, lock_ptr);
}

template<
    Context Ctx,
    Request Req,
    typename Constraints = DefaultResolutionConstraints<Ctx>>
cppcoro::task<typename Req::value_type>
resolve_request_one_try(
    Ctx& ctx,
    Req const& req,
    bool retrying,
    cache_record_lock* lock_ptr,
    Constraints constraints)
{
    // First decision (based on constraints if possible): remotely or locally.
    // A proxy request also forces remote resolving.
    if constexpr (Req::is_proxy || constraints.force_remote)
    {
        auto& rem_ctx{cast_ctx_to_ref<remote_context_intf>(ctx)};
        return resolve_request_remote(rem_ctx, req, lock_ptr);
    }
    else if constexpr (constraints.force_local)
    {
        // Call one of the two resolve_request_local() versions, depending on
        // Req being a plain value or a Request
        auto& loc_ctx{cast_ctx_to_ref<local_context_intf>(ctx)};
        return resolve_request_local(
            loc_ctx, req, retrying, lock_ptr, constraints);
    }
    else
    {
        if (ctx.remotely())
        {
            auto& rem_ctx = cast_ctx_to_ref<remote_context_intf>(ctx);
            return resolve_request_remote(rem_ctx, req, lock_ptr);
        }
        else
        {
            auto& loc_ctx{cast_ctx_to_ref<local_context_intf>(ctx)};
            return resolve_request_local(
                loc_ctx, req, retrying, lock_ptr, constraints);
        }
    }
}

template<
    Context Ctx,
    RetryableRequest Req,
    typename Constraints = DefaultResolutionConstraints<Ctx>>
cppcoro::task<typename Req::value_type>
resolve_request_with_retry(
    Ctx& ctx,
    Req const& req,
    cache_record_lock* lock_ptr,
    Constraints constraints)
{
    static_assert(ValidRetryableRequest<Req>);
    int attempt = 0;
    for (;;)
    {
        std::chrono::milliseconds delay{};
        try
        {
            co_return co_await resolve_request_one_try(
                ctx, req, attempt > 0, lock_ptr, constraints);
        }
        catch (std::exception const& exc)
        {
            delay = req.handle_exception(attempt, exc);
        }
        // TODO if introspective: update status. Not really possible with
        // tasklet_tracker.
        co_await ctx.schedule_after(delay);
        ++attempt;
    }
#ifdef __cpp_lib_unreachable
    // C++23 feature
    std::unreachable();
#else
    throw std::logic_error("unreachable");
#endif
}

/*****************************************************************************
 * Public interface: resolve_request()
 */

/*
 * Resolve a non-request value; locally, whatever the context
 *
 * The "requires" here is strictly not needed: if it is omitted, and the second
 * arg is a request, then the following template would subsume.
 * However, when that template is not selected for some reason, the "requires"
 * tends to result in compiler error messages that are somewhat less obscure.
 */
template<
    Context Ctx,
    typename Val,
    typename Constraints = NoResolutionConstraints>
    requires(!Request<Val>)
cppcoro::task<Val> resolve_request(
    Ctx& ctx, Val const& val, Constraints constraints = Constraints())
{
    static_assert(!constraints.force_remote);
    // async status, if appropriate, should already be FINISHED
    co_return val;
}

/*
 * Resolves a request; remotely or locally, synchronously or asynchronously,
 * depending on context and contraints.
 *
 * If lock_ptr is not nullptr, the call causes *lock_ptr to lock the associated
 * memory cache record. While the *lock_ptr object exists, the lock stays
 * active, and the cache record will not be evicted (so the cache keeps the
 * result in memory).
 *
 * Notes:
 * - The caller must ensure that the actual ctx type implements all needed
 *   context interfaces; if not, resolution will throw a "bad cast" exception.
 * - This function is blocking. Progress of an asynchronous request can be
 *   monitored via its context tree.
 * - This function throws async_cancelled when an asynchronous request is
 *   cancelled.
 * - It seems likely that for multiple calls for the same Request, Ctx will be
 *   the same in each case (so just one template instantiation).
 * - Passing a non-nullptr lock_ptr is useless for uncached requests.
 * - lock_ptr will be nullptr if Req is a subrequest.
 */
template<
    Context Ctx,
    Request Req,
    typename Constraints = DefaultResolutionConstraints<Ctx>>
cppcoro::task<typename Req::value_type>
resolve_request(
    Ctx& ctx,
    Req const& req,
    Constraints constraints = Constraints(),
    cache_record_lock* lock_ptr = nullptr)
{
    static_assert(ValidContext<Ctx>);
    static_assert(MatchingContextRequest<Ctx, Req>);
    static_assert(MatchingContextConstraints<Ctx, Constraints>);
    static_assert(MatchingRequestConstraints<Req, Constraints>);

    if constexpr (Req::retryable)
    {
        return resolve_request_with_retry(ctx, req, lock_ptr, constraints);
    }
    else
    {
        return resolve_request_one_try(ctx, req, false, lock_ptr, constraints);
    }
}

// Maybe a more convenient parameter order, but not backward compatible with
// the pre-cache_record_lock situation.
template<
    Context Ctx,
    Request Req,
    typename Constraints = DefaultResolutionConstraints<Ctx>>
cppcoro::task<typename Req::value_type>
resolve_request(
    Ctx& ctx,
    Req const& req,
    cache_record_lock* lock_ptr,
    Constraints constraints = Constraints())
{
    return resolve_request(ctx, req, constraints, lock_ptr);
}

} // namespace cradle

#endif
