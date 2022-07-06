#ifndef CRADLE_THINKNODE_REQUEST_H
#define CRADLE_THINKNODE_REQUEST_H

#include <compare>
#include <memory>
#include <optional>
#include <typeindex>
#include <typeinfo>
#include <utility>

#include <cppcoro/task.hpp>

#include <cradle/inner/core/hash.h>
#include <cradle/inner/core/id.h>
#include <cradle/inner/requests/generic.h>
#include <cradle/thinknode/types.hpp>

namespace cradle {

namespace detail {

class request_hasher
{
 public:
    template<typename... Args>
    void
    operator()(Args&&... args)
    {
        value_ = combine_hashes(invoke_hash(args)...);
    }

    size_t
    get_value()
    {
        return value_;
    }

 private:
    size_t value_;
};

struct args_comparator
{
    void
    operator()()
    {
        value_ = 0;
    }

    template<typename Arg0, typename... Args>
    requires(std::three_way_comparable<Arg0>) void
    operator()(Arg0&& lhs_arg0, Arg0&& rhs_arg0, Args&&... args)
    {
        auto diff = lhs_arg0 <=> rhs_arg0;
        if (diff != 0)
        {
            value_ = diff < 0 ? -1 : diff == 0 ? 0 : 1;
        }
        else
        {
            operator()(std::forward<Args>(args)...);
        }
    }

    template<typename Arg0, typename... Args>
    requires(!std::three_way_comparable<Arg0>) void
    operator()(Arg0&& lhs_arg0, Arg0&& rhs_arg0, Args&&... args)
    {
        if (lhs_arg0 < rhs_arg0)
        {
            value_ = -1;
        }
        else if (rhs_arg0 < lhs_arg0)
        {
            value_ = 1;
        }
        else
        {
            operator()(std::forward<Args>(args)...);
        }
    }

    int value_;
};

template<typename Base>
class thinknode_request_mixin : public Base, public id_interface
{
 public:
    using element_type = thinknode_request_mixin;
    using value_type = typename Base::value_type;

    template<typename... Args>
    thinknode_request_mixin(Args&&... args) : Base(std::move(args)...)
    {
    }

    bool
    equals(thinknode_request_mixin const& other) const
    {
        if (this == &other)
        {
            return true;
        }
        if (get_function_type_index() != other.get_function_type_index())
        {
            return false;
        }
        args_comparator cmp;
        this->template compare(cmp, other);
        return cmp.value_ == 0;
    }

    bool
    less_than(thinknode_request_mixin const& other) const
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
        args_comparator cmp;
        this->template compare(cmp, other);
        return cmp.value_ < 0;
    }

    bool
    equals(id_interface const& other) const override
    {
        auto const& other_id
            = static_cast<thinknode_request_mixin const&>(other);
        return equals(other_id);
    }

    bool
    less_than(id_interface const& other) const override
    {
        auto const& other_id
            = static_cast<thinknode_request_mixin const&>(other);
        return less_than(other_id);
    }

    size_t
    hash() const override
    {
        if (!hash_)
        {
            detail::request_hasher hasher;
            const_cast<thinknode_request_mixin*>(this)->serialize(hasher);
            hash_ = hasher.get_value();
        }
        return *hash_;
    }

    void
    update_hash(unique_hasher& hasher) const override
    {
        // TODO
    }

    std::type_index
    get_function_type_index() const
    {
        // The typeid() is evaluated at compile time
        return std::type_index(typeid(Base));
    }

 private:
    mutable std::optional<size_t> hash_;
};

} // namespace detail

template<caching_level_type level, typename Base>
class thinknode_request_container
{
 public:
    using element_type = thinknode_request_container;
    using value_type = typename Base::value_type;
    using impl_type = detail::thinknode_request_mixin<Base>;

    static constexpr caching_level_type caching_level = level;
    // Or depend on Base::get_introspection_title() presence
    static constexpr bool introspective = true;

    template<typename... Args>
    thinknode_request_container(Args... args)
    {
        auto impl{std::make_shared<impl_type>(std::move(args)...)};
        impl_ = impl;
        if constexpr (caching_level != caching_level_type::none)
        {
            captured_id_ = captured_id{impl};
        }
    }

    bool
    equals(thinknode_request_container const& other) const
    {
        return impl_->equals(*other.impl_);
    }

    bool
    less_than(thinknode_request_container const& other) const
    {
        return impl_->less_than(*other.impl_);
    }

    size_t
    hash() const
    {
        return impl_->hash();
    }

    void
    update_hash(unique_hasher& hasher) const
    {
        impl_->update_hash(hasher);
    }

    captured_id const&
    get_captured_id() const
    {
        if constexpr (caching_level == caching_level_type::none)
        {
            throw not_implemented_error(
                "captured_id only available for cached requests");
        }
        return captured_id_;
    }

    cppcoro::task<value_type>
    resolve(thinknode_request_context const& ctx) const
    {
        return impl_->resolve(ctx);
    }

    std::string
    get_introspection_title() const requires(introspective)
    {
        return impl_->get_introspection_title();
    }

 private:
    std::shared_ptr<impl_type> impl_;
    captured_id captured_id_;
};

} // namespace cradle

#endif