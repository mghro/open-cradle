#include <cppcoro/fmap.hpp>

#include <cradle/inner/service/disk_cached_blob.h>
#include <cradle/plugins/disk_cache/serialization/native/native.h>
#include <cradle/typing/encodings/native.h>

namespace cradle {

// This is a coroutine so takes key by value.
template<>
cppcoro::task<dynamic>
disk_cached(
    inner_resources& resources,
    captured_id key,
    std::function<cppcoro::task<dynamic>()> create_task)
{
    auto dynamic_to_blob = [](dynamic x) -> blob {
        return make_blob(write_natively_encoded_value(x));
    };
    auto create_blob_task = [&]() {
        return cppcoro::make_task(
            cppcoro::fmap(dynamic_to_blob, create_task()));
    };
    blob x = co_await disk_cached_blob(resources, key, create_blob_task);
    auto data = reinterpret_cast<uint8_t const*>(x.data());
    co_return read_natively_encoded_value(data, x.size());
}

template<>
cppcoro::task<blob>
disk_cached(
    inner_resources& core,
    captured_id key,
    std::function<cppcoro::task<blob>()> create_task)
{
    return disk_cached_blob(core, std::move(key), std::move(create_task));
}

} // namespace cradle
