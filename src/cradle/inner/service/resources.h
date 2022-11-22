#ifndef CRADLE_INNER_SERVICE_RESOURCES_H
#define CRADLE_INNER_SERVICE_RESOURCES_H

// Resources available for resolving requests: the memory cache, and optionally
// some disk cache.

#include <memory>
#include <optional>

#include <cradle/inner/caching/immutable/cache.h>
#include <cradle/inner/service/config.h>
#include <cradle/inner/service/disk_cache_intf.h>

namespace cradle {

// Configuration keys for the inner resources
struct inner_config_keys
{
    // (Optional integer)
    // The maximum amount of memory to use for caching results that are no
    // longer in use, in bytes.
    inline static std::string const MEMORY_CACHE_UNUSED_SIZE_LIMIT{
        "memory_cache/unused_size_limit"};

    // (Mandatory string)
    // Specifies the factory to use to create a disk cache implementation.
    // The string should equal a key passed to register_disk_cache_factory().
    inline static std::string const DISK_CACHE_FACTORY{"disk_cache/factory"};
};

// Factory of disk_cache_intf objects.
// A "disk cache" type of plugin would implement one such factory.
class disk_cache_factory
{
 public:
    virtual ~disk_cache_factory() = default;

    virtual std::unique_ptr<disk_cache_intf>
    create(service_config const& config) = 0;
};

// Registers a disk cache factory, identified by a key.
// A plugin would call this function in its initialization.
void
register_disk_cache_factory(
    std::string const& key, std::unique_ptr<disk_cache_factory> factory);

class inner_resources
{
 public:
    // Creates an object that needs an inner_initialize() call
    inner_resources() = default;

    void
    inner_initialize(service_config const& config);

    void
    inner_reset_memory_cache();

    void
    inner_reset_memory_cache(service_config const& config);

    void
    inner_reset_disk_cache(service_config const& config);

    cradle::immutable_cache&
    memory_cache()
    {
        return *memory_cache_;
    }

    disk_cache_intf&
    disk_cache()
    {
        return *disk_cache_;
    }

 private:
    std::unique_ptr<cradle::immutable_cache> memory_cache_;
    std::unique_ptr<disk_cache_intf> disk_cache_;

    void
    create_memory_cache(service_config const& config);

    void
    create_disk_cache(service_config const& config);
};

} // namespace cradle

#endif