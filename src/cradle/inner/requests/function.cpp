#include <stdexcept>

#include <fmt/format.h>

#include <cradle/inner/requests/function.h>
#include <cradle/inner/utilities/logging.h>

namespace cradle {

void
check_title_is_valid(std::string const& title)
{
    if (title.empty())
    {
        throw std::invalid_argument{"empty title"};
    }
}

cereal_functions_registry&
cereal_functions_registry::instance()
{
    // The singleton is part of the main program, and any dynamically loaded
    // library will see this instance.
    static cereal_functions_registry the_instance;
    return the_instance;
}

cereal_functions_registry::cereal_functions_registry()
    : logger_{ensure_logger("cfr")}
{
}

void
cereal_functions_registry::add(
    catalog_id cat_id,
    std::string const& uuid_str,
    create_t* create,
    std::any function)
{
    // TODO investigate exception opportunities and consequences.
    logger_->debug("add uuid {}, cat {}", uuid_str, cat_id.value());
    std::scoped_lock lock{mutex_};
    auto outer_it = entries_.find(uuid_str);
    if (outer_it == entries_.end())
    {
        outer_it = entries_.emplace(uuid_str, inner_list_t{}).first;
    }
    auto& inner_list = outer_it->second;
    for (auto& inner_it : inner_list)
    {
        if (inner_it.cat_id == cat_id)
        {
            // Should not happen; maybe an earlier exception prevented the
            // unregister() call.
            logger_->error(
                "existing entry for uuid {} and cat {}",
                uuid_str,
                cat_id.value());
        }
    }
    // Any existing matching entry could contain stale pointers, and attempts
    // to overwrite it could lead to crashes. Push new entry to the front so
    // that find_entry() will find it and not a stale one.
    // TODO multiple normalized_arg entries possible?
    inner_list.push_front(entry_t{cat_id, create, std::move(function)});
}

void
cereal_functions_registry::unregister_catalog(catalog_id cat_id)
{
    logger_->info(
        "cereal_functions_registry: unregister_catalog {}", cat_id.value());
    std::scoped_lock lock{mutex_};
    std::vector<std::string> keys_to_remove;
    for (auto& [uuid_str, inner_list] : entries_)
    {
        for (auto inner_it = inner_list.begin(); inner_it != inner_list.end();)
        {
            if (inner_it->cat_id == cat_id)
            {
                logger_->debug(
                    "removing entry for uuid {}, cat {}",
                    uuid_str,
                    cat_id.value());
                inner_it = inner_list.erase(inner_it);
            }
            else
            {
                ++inner_it;
            }
        }
        if (inner_list.empty())
        {
            // Calling erase() here would invalidate the iterator.
            keys_to_remove.push_back(uuid_str);
        }
    }
    for (auto const& key : keys_to_remove)
    {
        logger_->debug("removing empty inner list for uuid {}", key);
        entries_.erase(key);
    }
}

// Finds _an_ entry for uuid_str.
// Assuming that the ODR holds across DLLs, `create` and `function` functions
// implemented in DLL X should be identical to ones implemented in DLL Y.
// TODO keep track of pointers to DLL code and do not unload if they exist
cereal_functions_registry::entry_t&
cereal_functions_registry::find_entry(std::string const& uuid_str)
{
    std::scoped_lock lock{mutex_};
    auto it = entries_.find(uuid_str);
    if (it == entries_.end())
    {
        throw unregistered_uuid_error(fmt::format(
            "cereal_functions_registry: no entry found for uuid {}",
            uuid_str));
    }
    auto& inner_list = it->second;
    if (inner_list.empty())
    {
        // Violating the invariant that inner_list is not empty.
        throw unregistered_uuid_error(fmt::format(
            "cereal_functions_registry: empty list for uuid {}", uuid_str));
    }
    // Any entry from inner_list should do.
    return *inner_list.begin();
}

} // namespace cradle
