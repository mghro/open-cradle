#include <stdexcept>

#include <cradle/inner/requests/function.h>

namespace cradle {

void
check_title_is_valid(std::string const& title)
{
    if (title.empty())
    {
        throw std::invalid_argument{"empty title"};
    }
}

using entry_t = cereal_functions_registry_impl::entry_t;

static bool
operator==(entry_t const& lhs, entry_t const& rhs)
{
    return lhs.create == rhs.create && lhs.save == rhs.save
           && lhs.load == rhs.load;
}

static bool
operator!=(entry_t const& lhs, entry_t const& rhs)
{
    return !(lhs == rhs);
}

static cereal_functions_registry_impl the_instance;

cereal_functions_registry_impl&
cereal_functions_registry_impl::instance()
{
    return the_instance;
}

void
cereal_functions_registry_impl::add_entry(
    std::string const& uuid_str, create_t* create, save_t* save, load_t* load)
{
    std::scoped_lock lock{mutex_};
    entry_t new_entry{create, save, load};
    auto it = entries_.find(uuid_str);
    if (it == entries_.end())
    {
        entries_[uuid_str] = new_entry;
    }
    else if (it->second != new_entry)
    {
        throw conflicting_types_uuid_error{
            fmt::format("conflicting types for uuid {}", uuid_str)};
    }
}

entry_t&
cereal_functions_registry_impl::find_entry(request_uuid const& uuid)
{
    std::scoped_lock lock{mutex_};
    std::string uuid_str{uuid.str()};
    auto it = entries_.find(uuid_str);
    if (it == entries_.end())
    {
        // This should be an internal error.
        throw unregistered_uuid_error{fmt::format(
            "cereal_functions_registry_impl: no entry for {}", uuid_str)};
    }
    return it->second;
}

} // namespace cradle