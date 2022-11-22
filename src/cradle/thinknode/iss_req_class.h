#ifndef CRADLE_THINKNODE_ISS_REQ_CLASS_H
#define CRADLE_THINKNODE_ISS_REQ_CLASS_H

// ISS requests implemented using dedicated base classes mixed into
// thinknode_request_container. Compared to the approach in iss_req_func.h,
// this could be more flexible, but creating these base classes implies more
// work, and is error-prone too.
// The alternative (using iss_req_func.h) currently looks better.

#include <cereal/types/string.hpp>
#include <cppcoro/task.hpp>

#include <cradle/inner/requests/value.h>
#include <cradle/thinknode/iss.h>
#include <cradle/thinknode/iss_req_common.h>
#include <cradle/thinknode/request.h>
#include <cradle/typing/encodings/msgpack.h>

namespace cradle {

template<Request ObjectDataRequest>
    requires(std::same_as<typename ObjectDataRequest::value_type, blob>)
// The identity of a request object is formed by:
// - The get_uuid() value, identifying the class
// - The runtime arguments: hash(), save(), load(), compare()
class my_post_iss_object_request_base
{
 public:
    using value_type = std::string;

    my_post_iss_object_request_base(
        std::string api_url,
        std::string context_id,
        thinknode_type_info schema,
        ObjectDataRequest object_data_request)
        : api_url_{api_url},
          context_id_{std::move(context_id)},
          url_type_string_{get_url_type_string(api_url, schema)},
          object_data_request_{std::move(object_data_request)}
    {
    }

    cppcoro::task<std::string>
    resolve(thinknode_request_context& ctx) const
    {
        auto object_data = co_await object_data_request_.resolve(ctx);
        co_return co_await post_iss_object_uncached_wrapper(
            ctx, api_url_, context_id_, url_type_string_, object_data);
    }

    request_uuid
    get_uuid() const
    {
        return combined_uuid(
            request_uuid("my_post_iss_object_request"),
            object_data_request_.get_uuid());
    }

    std::string
    get_introspection_title() const
    {
        return "my_post_iss_object_request";
    }

    // Update hasher for the runtime arguments of this request
    template<typename Hasher>
    void
    hash(Hasher& hasher) const
    {
        hasher(api_url_, context_id_, url_type_string_, object_data_request_);
    }

    // Compares against another request object, returning <0, 0, or >0.
    // The values passed to comparator are the same as in hash();
    // it would be nice if we could somehow get rid of this duplication.
    template<typename Comparator>
    int
    compare(
        Comparator& comparator,
        my_post_iss_object_request_base const& other) const
    {
        return comparator(
            api_url_,
            other.api_url_,
            context_id_,
            other.context_id_,
            url_type_string_,
            other.url_type_string_,
            object_data_request_,
            other.object_data_request_);
    }

 public:
    // cereal-related interface

    // Should be called (indirectly) from cereal::access only.
    my_post_iss_object_request_base() = default;

    template<typename Archive>
    void
    save(Archive& archive) const
    {
        // Trust archive to be save-only
        const_cast<my_post_iss_object_request_base*>(this)->load_save(archive);
    }

    // Identical to hash(), apart from the make_nvp's
    template<typename Archive>
    void
    load(Archive& archive)
    {
        load_save(archive);
    }

 private:
    template<typename Archive>
    void
    load_save(Archive& archive)
    {
        archive(
            cereal::make_nvp("api_url", api_url_),
            cereal::make_nvp("context_id", context_id_),
            cereal::make_nvp("url_type_string", url_type_string_),
            cereal::make_nvp("object_data_request", object_data_request_));
    }

 private:
    std::string api_url_;
    std::string context_id_;
    // Or a request that can calculate url_type_string_ from schema and
    // api_url? It's now always evaluated and maybe the value is not needed.
    std::string url_type_string_;
    ObjectDataRequest object_data_request_;
};

template<caching_level_type level, Request ObjectDataRequest>
using my_post_iss_object_request = thinknode_request_container<
    level,
    my_post_iss_object_request_base<ObjectDataRequest>>;

// Create a request to post an ISS object, where the data are retrieved
// by resolving another request, and return the request's ID.
template<caching_level_type level, Request ObjectDataRequest>
    requires(std::same_as<typename ObjectDataRequest::value_type, blob>)
auto rq_post_iss_object(
    std::string api_url,
    std::string context_id,
    thinknode_type_info schema,
    ObjectDataRequest object_data_request)
{
    return my_post_iss_object_request<level, ObjectDataRequest>{
        std::move(api_url),
        std::move(context_id),
        std::move(schema),
        std::move(object_data_request)};
}

// Create a request to post an ISS object from a raw blob of data
// (e.g. encoded in MessagePack format), and return its ID.
template<caching_level_type level>
auto
rq_post_iss_object(
    std::string api_url,
    std::string context_id,
    thinknode_type_info schema,
    blob object_data)
{
    using ObjectDataRequest = value_request<blob>;
    return rq_post_iss_object<level, ObjectDataRequest>(
        std::move(api_url),
        std::move(context_id),
        std::move(schema),
        rq_value(object_data));
}

// Create a request to post an ISS object and return its ID.
template<caching_level_type level>
auto
rq_post_iss_object(
    std::string api_url,
    std::string context_id,
    thinknode_type_info schema,
    dynamic data)
{
    return rq_post_iss_object<level>(
        std::move(api_url),
        std::move(context_id),
        std::move(schema),
        rq_value(value_to_msgpack_blob(data)));
}

template<caching_level_type level, Request ObjectDataRequest>
    requires(std::same_as<typename ObjectDataRequest::value_type, blob>)
auto rq_post_iss_object_erased(
    std::string api_url,
    std::string context_id,
    thinknode_type_info schema,
    ObjectDataRequest object_data_request)
{
    using erased_type = thinknode_request_erased<level, std::string>;
    using impl_type = thinknode_request_impl<
        my_post_iss_object_request_base<ObjectDataRequest>>;
    return erased_type{std::make_shared<impl_type>(
        std::move(api_url),
        std::move(context_id),
        std::move(schema),
        std::move(object_data_request))};
}

template<caching_level_type level>
auto
rq_post_iss_object_erased(
    std::string api_url,
    std::string context_id,
    thinknode_type_info schema,
    blob object_data)
{
    return rq_post_iss_object_erased<level>(
        std::move(api_url),
        std::move(context_id),
        std::move(schema),
        rq_value(object_data));
}

template<Request ImmutableIdRequest>
    requires(
        std::same_as<typename ImmutableIdRequest::value_type, std::string>)
class my_retrieve_immutable_object_request_base
{
 public:
    using value_type = blob;

    my_retrieve_immutable_object_request_base(
        std::string api_url,
        std::string context_id,
        ImmutableIdRequest immutable_id_request)
        : api_url_{std::move(api_url)},
          context_id_{std::move(context_id)},
          immutable_id_request_{std::move(immutable_id_request)}
    {
    }

    cppcoro::task<blob>
    resolve(thinknode_request_context& ctx) const
    {
        auto immutable_id = co_await immutable_id_request_.resolve(ctx);
        co_return co_await retrieve_immutable_blob_uncached(
            ctx, context_id_, immutable_id);
    }

    request_uuid
    get_uuid() const
    {
        return combined_uuid(
            request_uuid("my_retrieve_immutable_object_request_base"),
            immutable_id_request_.get_uuid());
    }

    std::string
    get_introspection_title() const
    {
        return "my_retrieve_immutable_object_request";
    }

    // Defines the data members forming this object's state.
    template<typename Hasher>
    void
    hash(Hasher& hasher) const
    {
        hasher(api_url_, context_id_, immutable_id_request_);
    }

    template<typename Comparator>
    int
    compare(
        Comparator& comparator,
        my_retrieve_immutable_object_request_base const& other) const
    {
        return comparator(
            api_url_,
            other.api_url_,
            context_id_,
            other.context_id_,
            immutable_id_request_,
            other.immutable_id_request_);
    }

 public:
    // cereal-related

    // Should be called (indirectly) from cereal::access only.
    my_retrieve_immutable_object_request_base() = default;

    template<typename Archive>
    void
    save(Archive& archive) const
    {
        archive(
            cereal::make_nvp("api_url", api_url_),
            cereal::make_nvp("context_id", context_id_),
            cereal::make_nvp("immutable_id_request", immutable_id_request_));
    }

    template<typename Archive>
    void
    load(Archive& archive)
    {
        archive(api_url_, context_id_, immutable_id_request_);
    }

 private:
    std::string api_url_;
    std::string context_id_;
    ImmutableIdRequest immutable_id_request_;
};

template<caching_level_type level, Request ImmutableIdRequest>
    requires(
        std::same_as<typename ImmutableIdRequest::value_type, std::string>)
auto rq_retrieve_immutable_object(
    std::string api_url,
    std::string context_id,
    ImmutableIdRequest immutable_id_request)
{
    using erased_type = thinknode_request_erased<level, blob>;
    using impl_type = thinknode_request_impl<
        my_retrieve_immutable_object_request_base<ImmutableIdRequest>>;
    return erased_type{std::make_shared<impl_type>(
        std::move(api_url),
        std::move(context_id),
        std::move(immutable_id_request))};
}

template<caching_level_type level>
auto
rq_retrieve_immutable_object(
    std::string api_url, std::string context_id, std::string immutable_id)
{
    return rq_retrieve_immutable_object<level>(
        std::move(api_url), std::move(context_id), rq_value(immutable_id));
}

} // namespace cradle

#endif