#ifndef CRADLE_INNER_REQUESTS_FUNCTION_H
#define CRADLE_INNER_REQUESTS_FUNCTION_H

#include <cassert>
#include <memory>
#include <optional>
#include <stdexcept>
#include <typeindex>
#include <typeinfo>
#include <utility>

#include <cereal/types/memory.hpp>
#include <cereal/types/tuple.hpp>
#include <cppcoro/task.hpp>

#include <cradle/inner/core/exception.h>
#include <cradle/inner/core/hash.h>
#include <cradle/inner/core/id.h>
#include <cradle/inner/core/sha256_hash_id.h>
#include <cradle/inner/core/unique_hash.h>
#include <cradle/inner/requests/cereal.h>
#include <cradle/inner/requests/generic.h>
#include <cradle/inner/requests/uuid.h>
#include <cradle/inner/service/core.h>
#include <cradle/inner/service/request.h>

namespace cradle {

/*
 * The first part of this file defines the "original" function requests,
 * that have no type erasure.
 *
 * They do have some drawbacks:
 * - The request class reflects the entire request tree, and tends to grow
 *   fast. Compilation times will become much slower, and compilers will
 *   give up altogether when the tree has more than a few dozen requests.
 * - class function_request_cached stores its arguments twice: once in the
 *   request object itself, once in its captured_id member.
 * - Type-erased objects have some overhead (due to accessing the "_impl"
 *   object through a shared_ptr), but a function_request_cached object also
 *   has a shared_ptr in its captured_id member.
 * - Request identity (uuid) is not really supported.
 *
 * So normally the type-erased requests in function.h should be preferred.
 */

template<typename Value, typename Function, typename... Args>
class function_request_uncached
{
 public:
    using element_type = function_request_uncached;
    using value_type = Value;

    static constexpr caching_level_type caching_level
        = caching_level_type::none;

    function_request_uncached(Function function, Args... args)
        : function_{std::move(function)}, args_{std::move(args)...}
    {
    }

    request_uuid
    get_uuid() const
    {
        // The uuid should cover function_, but C++ does not offer anything
        // that can be used across application runs.
        throw not_implemented_error();
    }

    template<UncachedContext Ctx>
    cppcoro::task<Value>
    resolve(Ctx& ctx) const
    {
        // The "func=function_" is a workaround to prevent a gcc-10 internal
        // compiler error in release builds.
        co_return co_await std::apply(
            [&, func = function_](auto&&... args)
                -> cppcoro::task<Value> {
                co_return func((co_await resolve_request(ctx, args))...);
            },
            args_);
    }

 private:
    Function function_;
    std::tuple<Args...> args_;
};

template<typename Value, typename Function, typename... Args>
struct arg_type_struct<function_request_uncached<Value, Function, Args...>>
{
    using value_type = Value;
};

template<typename Value, typename Function, typename... Args>
struct arg_type_struct<
    std::unique_ptr<function_request_uncached<Value, Function, Args...>>>
{
    using value_type = Value;
};

template<typename Value, typename Function, typename... Args>
struct arg_type_struct<
    std::shared_ptr<function_request_uncached<Value, Function, Args...>>>
{
    using value_type = Value;
};

// Due to absence of a usable uuid, these objects are suitable for memory
// caching only, and cannot be disk cached.
// (And even memory caching is not guaranteed to work, as it relies on
// type_info::name() results being unique, and this may not be so.)
template<
    caching_level_type Level,
    typename Value,
    typename Function,
    typename... Args>
requires(Level != caching_level_type::full) class function_request_cached
{
 public:
    using element_type = function_request_cached;
    using value_type = Value;

    static constexpr caching_level_type caching_level = Level;
    static constexpr bool introspective = false;

    // TODO type_info::name() "can be identical for several types"
    // (But is there an alternative? C++ can correctly compare type_info or
    // type_index values, but not map them onto a unique value.)
    function_request_cached(Function function, Args... args)
        : id_{make_captured_sha256_hashed_id(
            typeid(function).name(), args...)},
          function_{std::move(function)},
          args_{std::move(args)...}
    {
    }

    // *this and other are the same type
    bool
    equals(function_request_cached const& other) const
    {
        return *id_ == *other.id_;
    }

    bool
    less_than(function_request_cached const& other) const
    {
        return *id_ < *other.id_;
    }

    size_t
    hash() const
    {
        return id_->hash();
    }

    void
    update_hash(unique_hasher& hasher) const
    {
        id_->update_hash(hasher);
    }

    request_uuid
    get_uuid() const
    {
        // The uuid should cover function_, but C++ does not offer anything
        // that can be used across application runs.
        throw not_implemented_error();
    }

    captured_id const&
    get_captured_id() const
    {
        return id_;
    }

    template<CachedContext Ctx>
    cppcoro::task<Value>
    resolve(Ctx& ctx) const
    {
        // The "func=function_" is a workaround to prevent a gcc-10 internal
        // compiler error in release builds.
        co_return co_await std::apply(
            [&, func = function_](auto&&... args) -> cppcoro::task<Value> {
                co_return func((co_await resolve_request(ctx, args))...);
            },
            args_);
    }

 private:
    captured_id id_;
    Function function_;
    std::tuple<Args...> args_;
};

template<
    caching_level_type Level,
    typename Value,
    typename Function,
    typename... Args>
struct arg_type_struct<
    function_request_cached<Level, Value, Function, Args...>>
{
    using value_type = Value;
};

template<
    caching_level_type Level,
    typename Value,
    typename Function,
    typename... Args>
struct arg_type_struct<
    std::unique_ptr<function_request_cached<Level, Value, Function, Args...>>>
{
    using value_type = Value;
};

template<
    caching_level_type Level,
    typename Value,
    typename Function,
    typename... Args>
struct arg_type_struct<
    std::shared_ptr<function_request_cached<Level, Value, Function, Args...>>>
{
    using value_type = Value;
};

// Used for comparing subrequests, where the main requests have the same type;
// so the subrequests have the same type too.
template<
    caching_level_type Level,
    typename Value,
    typename Function,
    typename... Args>
bool
operator==(
    function_request_cached<Level, Value, Function, Args...> const& lhs,
    function_request_cached<Level, Value, Function, Args...> const& rhs)
{
    return lhs.equals(rhs);
}

template<
    caching_level_type Level,
    typename Value,
    typename Function,
    typename... Args>
bool
operator<(
    function_request_cached<Level, Value, Function, Args...> const& lhs,
    function_request_cached<Level, Value, Function, Args...> const& rhs)
{
    return lhs.less_than(rhs);
}

template<
    caching_level_type Level,
    typename Value,
    typename Function,
    typename... Args>
size_t
hash_value(function_request_cached<Level, Value, Function, Args...> const& req)
{
    return req.hash();
}

template<
    caching_level_type Level,
    typename Value,
    typename Function,
    typename... Args>
void
update_unique_hash(
    unique_hasher& hasher,
    function_request_cached<Level, Value, Function, Args...> const& req)
{
    req.update_hash(hasher);
}

template<caching_level_type Level, typename Function, typename... Args>
requires(Level == caching_level_type::none) auto rq_function(
    Function function, Args... args)
{
    using Value = std::invoke_result_t<Function, arg_type<Args>...>;
    return function_request_uncached<Value, Function, Args...>{
        std::move(function), std::move(args)...};
}

template<caching_level_type Level, typename Function, typename... Args>
requires(Level != caching_level_type::none) auto rq_function(
    Function function, Args... args)
{
    using Value = std::invoke_result_t<Function, arg_type<Args>...>;
    return function_request_cached<Level, Value, Function, Args...>{
        std::move(function), std::move(args)...};
}

template<
    typename Value,
    caching_level_type Level,
    typename Function,
    typename... Args>
requires(Level != caching_level_type::none) auto rq_function(
    Function function, Args... args)
{
    return function_request_cached<Level, Value, Function, Args...>{
        std::move(function), std::move(args)...};
}

template<caching_level_type Level, typename Function, typename... Args>
requires(Level == caching_level_type::none) auto rq_function_up(
    Function function, Args... args)
{
    using Value = std::invoke_result_t<Function, arg_type<Args>...>;
    return std::make_unique<
        function_request_uncached<Value, Function, Args...>>(
        std::move(function), std::move(args)...);
}

template<caching_level_type Level, typename Function, typename... Args>
requires(Level != caching_level_type::none) auto rq_function_up(
    Function function, Args... args)
{
    using Value = std::invoke_result_t<Function, arg_type<Args>...>;
    return std::make_unique<
        function_request_cached<Level, Value, Function, Args...>>(
        std::move(function), std::move(args)...);
}

template<caching_level_type Level, typename Function, typename... Args>
requires(Level == caching_level_type::none) auto rq_function_sp(
    Function function, Args... args)
{
    using Value = std::invoke_result_t<Function, arg_type<Args>...>;
    return std::make_shared<
        function_request_uncached<Value, Function, Args...>>(
        std::move(function), std::move(args)...);
}

template<caching_level_type Level, typename Function, typename... Args>
requires(Level != caching_level_type::none) auto rq_function_sp(
    Function function, Args... args)
{
    using Value = std::invoke_result_t<Function, arg_type<Args>...>;
    return std::make_shared<
        function_request_cached<Level, Value, Function, Args...>>(
        std::move(function), std::move(args)...);
}

/*
 * The second part of this file defines the "type-erased" requests.
 * The main request object (class function_request_erased) has a shared_ptr
 * to a function_request_intf object; that object's full type
 * (i.e., function_request_impl's template arguments) are known in
 * function_request_erased's constructor only.
 *
 * These classes intend to overcome the drawbacks of the earlier ones.
 */

/*
 * The interface type exposing the functionality that function_request_erased
 * requires outside its constructor.
 *
 * Ctx is the "minimum" context needed to resolve this request.
 * E.g. a "cached" context can be used to resolve a non-cached request.
 */
template<RequestContext Ctx, typename Value>
class function_request_intf : public id_interface
{
 public:
    using context_type = Ctx;

    virtual ~function_request_intf() = default;

    virtual request_uuid
    get_uuid() const = 0;

    virtual cppcoro::task<Value>
    resolve(Ctx& ctx) const = 0;
};

/*
 * The actual type created by function_request_erased, but visible only in its
 * constructor (and erased elsewhere).
 *
 * Intf is a function_request_intf instantiation.
 *
 * This class implements id_interface exactly like sha256_hashed_id. Pros:
 * - args_ need not be copied
 * - Not needing a (theoretically?) unreliable type_info::name() value
 *
 * Only a small part of this class depends on the context type, so there will
 * be object code duplication if multiple instantiations exist differing in
 * the context (i.e., introspected + caching level) only. Maybe this could be
 * optimized if it becomes an issue.
 */
template<
    typename Value,
    typename Intf,
    bool AsCoro,
    typename Function,
    typename... Args>
class function_request_impl : public Intf
{
 public:
    static constexpr bool func_is_coro = AsCoro;
    using this_type = function_request_impl;
    using base_type = Intf;
    using context_type = typename Intf::context_type;

    function_request_impl(request_uuid uuid, Function function, Args... args)
        : uuid_{std::move(uuid)},
          function_{std::move(function)},
          args_{std::move(args)...}
    {
        if (uuid_.serializable())
        {
            register_polymorphic_type<this_type, base_type>(uuid_);
        }
    }

    // *this and other are the same type, so their function types are
    // identical, but the functions themselves might still be different.
    // Likewise, argument types will be identical, but their values might
    // differ.
    bool
    equals(function_request_impl const& other) const
    {
        if (this == &other)
        {
            return true;
        }
        return get_function_type_index() == other.get_function_type_index()
               && args_ == other.args_;
    }

    // *this and other are the same type
    bool
    less_than(function_request_impl const& other) const
    {
        if (this == &other)
        {
            return false;
        }
        auto this_type_index = get_function_type_index();
        auto other_type_index = other.get_function_type_index();
        if (this_type_index != other_type_index)
        {
            return this_type_index < other_type_index;
        }
        return args_ < other.args_;
    }

    // Caller promises that *this and other are the same type.
    bool
    equals(id_interface const& other) const override
    {
        assert(dynamic_cast<function_request_impl const*>(&other));
        auto const& other_id
            = static_cast<function_request_impl const&>(other);
        return equals(other_id);
    }

    // Caller promises that *this and other are the same type.
    bool
    less_than(id_interface const& other) const override
    {
        assert(dynamic_cast<function_request_impl const*>(&other));
        auto const& other_id
            = static_cast<function_request_impl const&>(other);
        return less_than(other_id);
    }

    // Maybe caching the hashes could be optional (policy?).
    size_t
    hash() const override
    {
        if (!hash_)
        {
            auto function_hash = invoke_hash(get_function_type_index());
            auto args_hash = std::apply(
                [](auto&&... args) {
                    return combine_hashes(invoke_hash(args)...);
                },
                args_);
            hash_ = combine_hashes(function_hash, args_hash);
        }
        return *hash_;
    }

    void
    update_hash(unique_hasher& hasher) const override
    {
        if (!have_unique_hash_)
        {
            calc_unique_hash();
        }
        hasher.combine(unique_hash_);
    }

    request_uuid
    get_uuid() const override
    {
        return uuid_;
    }

    cppcoro::task<Value>
    resolve(context_type& ctx) const override
    {
        if constexpr (!func_is_coro)
        {
            // The "func=function_" is a workaround to prevent a gcc-10
            // internal compiler error in release builds.
            co_return co_await std::apply(
                [&, func = function_](auto&&... args) -> cppcoro::task<Value> {
                    co_return func((co_await resolve_request(ctx, args))...);
                },
                args_);
        }
        else
        {
            co_return co_await std::apply(
                [&](auto&&... args) -> cppcoro::task<Value> {
                    co_return co_await function_(
                        ctx, (co_await resolve_request(ctx, args))...);
                },
                args_);
        }
    }

 private:
    // cereal-related

    friend class cereal::access;
    // Assumes instance_ was set
    function_request_impl() : function_(instance_->function_)
    {
    }

 public:
    // It is kind of cheating, but at least it's some solution:
    // function_ cannot be serialized, and somehow needs to be set when
    // deserializing, if possible in a type-safe way.
    // For the time being, objects of this class can be deserialized only
    // when an instance was created before.
    void
    register_instance(std::shared_ptr<function_request_impl> const& instance)
    {
        assert(instance);
        instance_ = instance;
    }

    template<typename Archive>
    void
    save(Archive& archive) const
    {
        archive(
            cereal::make_nvp("uuid", uuid_), cereal::make_nvp("args", args_));
    }

    template<typename Archive>
    void
    load(Archive& archive)
    {
        archive(uuid_, args_);
    }

 private:
    static inline std::shared_ptr<function_request_impl> instance_;
    request_uuid uuid_;
    Function function_;
    std::tuple<Args...> args_;
    mutable std::optional<size_t> hash_;
    mutable unique_hasher::result_t unique_hash_;
    mutable bool have_unique_hash_{false};

    std::type_index
    get_function_type_index() const
    {
        // The typeid() is evaluated at compile time
        return std::type_index(typeid(Function));
    }

    void
    calc_unique_hash() const
    {
        unique_hasher hasher;
        assert(uuid_.disk_cacheable());
        update_unique_hash(hasher, uuid_);
        std::apply(
            [&hasher](auto&&... args) {
                (update_unique_hash(hasher, args), ...);
            },
            args_);
        hasher.get_result(unique_hash_);
        have_unique_hash_ = true;
    }
};

// Defines the context_intf derived class needed for resolving a request
// characterized by Intrsp and Level.
// Primary template first, followed by the four partial specializations for
// all (is request introspected?, is request cached?) combinations.
template<bool Intrsp, caching_level_type Level>
struct function_request_ctx_type;

template<caching_level_type Level>
struct function_request_ctx_type<false, Level>
{
    // Resolving a cached function request requires a cached context.
    using type = cached_context_intf;
};

template<caching_level_type Level>
struct function_request_ctx_type<true, Level>
{
    // Resolving a cached+introspected function request requires a
    // cached+introspected context.
    using type = cached_introspected_context_intf;
};

template<>
struct function_request_ctx_type<false, caching_level_type::none>
{
    // An uncached function request can be resolved using any kind of context.
    using type = context_intf;
};

template<>
struct function_request_ctx_type<true, caching_level_type::none>
{
    // Resolving an introspected function request requires an introspected
    // context.
    using type = introspected_context_intf;
};

/*
 * A function request that erases function and arguments types
 *
 * This class supports two kinds of functions:
 * (0) Plain function: res = function(args...)
 * (1) Coroutine needing context: res = co_await function(ctx, args...)
 *
 * Intrsp is a template argument instead of being passed by value because of
 * the overhead, in object size and execution time, when resolving an
 * introspected request; see resolve_request_cached().
 *
 * TODO consider turning level, Intrsp, AsCoro into policies
 */
template<caching_level_type Level, typename Value, bool Intrsp, bool AsCoro>
class function_request_erased
{
 public:
    using element_type = function_request_erased;
    using value_type = Value;
    using ctx_type = typename function_request_ctx_type<Intrsp, Level>::type;
    using intf_type = function_request_intf<ctx_type, Value>;

    static constexpr caching_level_type caching_level = Level;
    static constexpr bool introspective = Intrsp;

    template<typename Function, typename... Args>
    function_request_erased(
        request_uuid uuid, std::string title, Function function, Args... args)
        : title_{std::move(title)}
    {
        if constexpr (caching_level == caching_level_type::full)
        {
            if (!uuid.disk_cacheable())
            {
                throw uuid_error("Real uuid needed for fully-cached request");
            }
        }
        auto impl{std::make_shared<function_request_impl<
            Value,
            intf_type,
            AsCoro,
            Function,
            Args...>>(
            std::move(uuid), std::move(function), std::move(args)...)};
        impl->register_instance(impl);
        impl_ = impl;
        init_captured_id();
    }

    // *this and other are the same type
    bool
    equals(function_request_erased const& other) const
    {
        return impl_->equals(*other.impl_);
    }

    bool
    less_than(function_request_erased const& other) const
    {
        return impl_->less_than(*other.impl_);
    }

    size_t
    hash() const
    {
        // TODO combine with caching_level?
        return impl_->hash();
    }

    void
    update_hash(unique_hasher& hasher) const
    {
        impl_->update_hash(hasher);
    }

    captured_id const&
    get_captured_id() const requires(caching_level != caching_level_type::none)
    {
        return captured_id_;
    }

    request_uuid
    get_uuid() const
    {
        return impl_->get_uuid();
    }

    template<RequestContext Ctx>
    cppcoro::task<Value>
    resolve(Ctx& ctx) const
    {
        return impl_->resolve(ctx);
    }

    std::string
    get_introspection_title() const requires(introspective)
    {
        return title_;
    }

 public:
    // Interface for cereal
    function_request_erased() = default;

    // Construct object, deserializing from a cereal archive
    // Convenience constructor for when this is the "outer" object.
    // Equivalent alternative:
    //   function_request_erased<...> req;
    //   req.load(archive)
    template<typename Archive>
    explicit function_request_erased(Archive& archive)
    {
        load(archive);
    }

    template<typename Archive>
    void
    save(Archive& archive) const
    {
        archive(
            cereal::make_nvp("impl", impl_),
            cereal::make_nvp("title", title_));
    }

    template<typename Archive>
    void
    load(Archive& archive)
    {
        archive(impl_, title_);
        init_captured_id();
    }

 private:
    std::string title_;
    std::shared_ptr<intf_type> impl_;
    captured_id captured_id_;

    void
    init_captured_id()
    {
        if constexpr (caching_level != caching_level_type::none)
        {
            captured_id_ = captured_id{impl_};
        }
    }
};

template<caching_level_type Level, typename Value, bool Intrsp, bool AsCoro>
struct arg_type_struct<function_request_erased<Level, Value, Intrsp, AsCoro>>
{
    using value_type = Value;
};

// Used for comparing subrequests, where the main requests have the same type;
// so the subrequests have the same type too.
template<caching_level_type Level, typename Value, bool Intrsp, bool AsCoro>
bool
operator==(
    function_request_erased<Level, Value, Intrsp, AsCoro> const& lhs,
    function_request_erased<Level, Value, Intrsp, AsCoro> const& rhs)
{
    return lhs.equals(rhs);
}

template<caching_level_type Level, typename Value, bool Intrsp, bool AsCoro>
bool
operator<(
    function_request_erased<Level, Value, Intrsp, AsCoro> const& lhs,
    function_request_erased<Level, Value, Intrsp, AsCoro> const& rhs)
{
    return lhs.less_than(rhs);
}

template<caching_level_type Level, typename Value, bool Intrsp, bool AsCoro>
size_t
hash_value(function_request_erased<Level, Value, Intrsp, AsCoro> const& req)
{
    return req.hash();
}

template<caching_level_type Level, typename Value, bool Intrsp, bool AsCoro>
void
update_unique_hash(
    unique_hasher& hasher,
    function_request_erased<Level, Value, Intrsp, AsCoro> const& req)
{
    req.update_hash(hasher);
}

template<caching_level_type Level, typename Function, typename... Args>
requires(Level != caching_level_type::full) auto rq_function_erased(
    Function function, Args... args)
{
    using Value = std::invoke_result_t<Function, arg_type<Args>...>;
    return function_request_erased<Level, Value, false, false>{
        request_uuid(), "", std::move(function), std::move(args)...};
}

template<
    caching_level_type Level,
    typename Value,
    typename Function,
    typename... Args>
requires(Level != caching_level_type::full) auto rq_function_erased_coro(
    Function function, Args... args)
{
    return function_request_erased<Level, Value, false, true>{
        request_uuid(), "", std::move(function), std::move(args)...};
}

template<caching_level_type Level, typename Function, typename... Args>
auto
rq_function_erased_uuid(request_uuid uuid, Function function, Args... args)
{
    using Value = std::invoke_result_t<Function, arg_type<Args>...>;
    return function_request_erased<Level, Value, false, false>{
        std::move(uuid), "", std::move(function), std::move(args)...};
}

template<
    caching_level_type Level,
    typename Value,
    typename Function,
    typename... Args>
auto
rq_function_erased_coro_uuid(
    request_uuid uuid, Function function, Args... args)
{
    return function_request_erased<Level, Value, false, true>{
        std::move(uuid), "", std::move(function), std::move(args)...};
}

template<caching_level_type Level, typename Function, typename... Args>
requires(Level != caching_level_type::full) auto rq_function_erased_intrsp(
    std::string title, Function function, Args... args)
{
    using Value = std::invoke_result_t<Function, arg_type<Args>...>;
    return function_request_erased<Level, Value, true, false>{
        request_uuid(),
        std::move(title),
        std::move(function),
        std::move(args)...};
}

template<
    caching_level_type Level,
    typename Value,
    typename Function,
    typename... Args>
requires(Level != caching_level_type::full) auto rq_function_erased_coro_intrsp(
    std::string title, Function function, Args... args)
{
    return function_request_erased<Level, Value, true, true>{
        request_uuid(),
        std::move(title),
        std::move(function),
        std::move(args)...};
}

template<caching_level_type Level, typename Function, typename... Args>
auto
rq_function_erased_uuid_intrsp(
    request_uuid uuid, std::string title, Function function, Args... args)
{
    using Value = std::invoke_result_t<Function, arg_type<Args>...>;
    return function_request_erased<Level, Value, true, false>{
        std::move(uuid),
        std::move(title),
        std::move(function),
        std::move(args)...};
}

template<
    caching_level_type Level,
    typename Value,
    typename Function,
    typename... Args>
auto
rq_function_erased_coro_uuid_intrsp(
    request_uuid uuid, std::string title, Function function, Args... args)
{
    return function_request_erased<Level, Value, true, true>{
        std::move(uuid),
        std::move(title),
        std::move(function),
        std::move(args)...};
}

} // namespace cradle

#endif
