#include <cradle/thinknode/iss.h>

#include <cstring>

#include <boost/format.hpp>

#include <cppcoro/sync_wait.hpp>

#include <cradle/inner/core/monitoring.h>
#include <cradle/inner/io/http_requests.h>
#include <cradle/inner/io/mock_http.h>
#include <cradle/typing/encodings/msgpack.h>
#include <cradle/typing/service/core.h>
#include <cradle/typing/utilities/testing.h>

using namespace cradle;

TEST_CASE("ISS object resolution", "[thinknode][iss]")
{
    service_core service;
    init_test_service(service);

    auto& mock_http = enable_http_mocking(service);
    mock_http.set_script(
        {{make_get_request(
              "https://mgh.thinknode.io/api/v1.0/iss/abc/"
              "immutable?context=123"
              "&ignore_upgrades=false",
              {{"Authorization", "Bearer xyz"},
               {"Accept", "application/json"}}),
          make_http_200_response("{ \"id\": \"def\" }")}});

    thinknode_session session;
    session.api_url = "https://mgh.thinknode.io/api/v1.0";
    session.access_token = "xyz";

    thinknode_request_context ctx{service, session, nullptr};
    auto id = cppcoro::sync_wait(
        resolve_iss_object_to_immutable(ctx, "123", "abc", false));
    REQUIRE(id == "def");

    REQUIRE(mock_http.is_complete());
    REQUIRE(mock_http.is_in_order());
}

TEST_CASE("ISS object metadata", "[thinknode][iss]")
{
    service_core service;
    init_test_service(service);

    auto& mock_http = enable_http_mocking(service);
    mock_http.set_script(
        {{make_http_request(
              http_request_method::HEAD,
              "https://mgh.thinknode.io/api/v1.0/iss/abc?context=123",
              {{"Authorization", "Bearer xyz"}},
              blob()),
          make_http_response(
              200,
              {{"Access-Control-Allow-Origin", "*"},
               {"Cache-Control", "max-age=60"}},
              blob())}});

    thinknode_session session;
    session.api_url = "https://mgh.thinknode.io/api/v1.0";
    session.access_token = "xyz";

    thinknode_request_context ctx{service, session, nullptr};
    auto metadata
        = cppcoro::sync_wait(get_iss_object_metadata(ctx, "123", "abc"));
    REQUIRE(
        metadata
        == (std::map<string, string>(
            {{"Access-Control-Allow-Origin", "*"},
             {"Cache-Control", "max-age=60"}})));

    REQUIRE(mock_http.is_complete());
    REQUIRE(mock_http.is_in_order());
}

TEST_CASE("ISS immutable retrieval", "[thinknode][iss]")
{
    service_core service;
    init_test_service(service);

    auto& mock_http = enable_http_mocking(service);
    mock_http.set_script(
        {{make_get_request(
              "https://mgh.thinknode.io/api/v1.0/iss/immutable/"
              "abc?context=123",
              {{"Authorization", "Bearer xyz"},
               {"Accept", "application/octet-stream"}}),
          make_http_200_response(
              value_to_msgpack_string(dynamic("the-data")))}});

    thinknode_session session;
    session.api_url = "https://mgh.thinknode.io/api/v1.0";
    session.access_token = "xyz";

    thinknode_request_context ctx{service, session, nullptr};
    auto data = cppcoro::sync_wait(retrieve_immutable(ctx, "123", "abc"));
    REQUIRE(data == dynamic("the-data"));

    REQUIRE(mock_http.is_complete());
    REQUIRE(mock_http.is_in_order());
}

// Check that both directions of URL type string conversion works for the
// given case.
static void
check_url_type_string(
    thinknode_session const& session,
    thinknode_type_info const& type,
    string const& url_string)
{
    INFO(url_string)
    INFO(type)
    REQUIRE(get_url_type_string(session, type) == url_string);
    REQUIRE(parse_url_type_string(url_string) == type);
}

TEST_CASE("URL type string", "[thinknode][iss]")
{
    thinknode_session session;
    session.api_url = "https://mgh.thinknode.io/api/v1.0";
    session.access_token = "xyz";

    thinknode_named_type_reference named_info;
    named_info.account = string("my_account");
    named_info.app = "my_app";
    named_info.name = "my_type";
    auto named_type = make_thinknode_type_info_with_named_type(named_info);
    check_url_type_string(
        session, named_type, "named/my_account/my_app/my_type");
    named_info.account = none;
    auto named_type_without_account
        = make_thinknode_type_info_with_named_type(named_info);
    REQUIRE(
        get_url_type_string(session, named_type_without_account)
        == "named/mgh/my_app/my_type");

    auto integer_type
        = make_thinknode_type_info_with_integer_type(thinknode_integer_type());
    check_url_type_string(session, integer_type, "integer");

    auto float_type
        = make_thinknode_type_info_with_float_type(thinknode_float_type());
    check_url_type_string(session, float_type, "float");

    auto string_type
        = make_thinknode_type_info_with_string_type(thinknode_string_type());
    check_url_type_string(session, string_type, "string");

    auto boolean_type
        = make_thinknode_type_info_with_boolean_type(thinknode_boolean_type());
    check_url_type_string(session, boolean_type, "boolean");

    auto blob_type
        = make_thinknode_type_info_with_blob_type(thinknode_blob_type());
    check_url_type_string(session, blob_type, "blob");

    auto dynamic_type
        = make_thinknode_type_info_with_dynamic_type(thinknode_dynamic_type());
    check_url_type_string(session, dynamic_type, "dynamic");

    auto nil_type
        = make_thinknode_type_info_with_nil_type(thinknode_nil_type());
    check_url_type_string(session, nil_type, "nil");

    auto datetime_type = make_thinknode_type_info_with_datetime_type(
        thinknode_datetime_type());
    check_url_type_string(session, datetime_type, "datetime");

    thinknode_array_info array_info;
    array_info.element_schema = boolean_type;
    auto array_type = make_thinknode_type_info_with_array_type(array_info);
    check_url_type_string(session, array_type, "array/boolean");

    thinknode_map_info map_info;
    map_info.key_schema = array_type;
    map_info.value_schema = blob_type;
    auto map_type = make_thinknode_type_info_with_map_type(map_info);
    check_url_type_string(session, map_type, "map/array/boolean/blob");

    thinknode_structure_info struct_info;
    struct_info.fields["def"].schema = array_type;
    struct_info.fields["abc"].schema = blob_type;
    auto struct_type
        = make_thinknode_type_info_with_structure_type(struct_info);
    check_url_type_string(
        session, struct_type, "structure/2/abc/blob/def/array/boolean");

    thinknode_union_info union_info;
    union_info.members["def"].schema = array_type;
    union_info.members["abc"].schema = blob_type;
    union_info.members["ghi"].schema = string_type;
    auto union_type = make_thinknode_type_info_with_union_type(union_info);
    check_url_type_string(
        session, union_type, "union/3/abc/blob/def/array/boolean/ghi/string");

    auto optional_type = make_thinknode_type_info_with_optional_type(map_type);
    check_url_type_string(
        session, optional_type, "optional/map/array/boolean/blob");

    thinknode_enum_info enum_info;
    enum_info.values["def"] = thinknode_enum_value_info();
    enum_info.values["abc"] = thinknode_enum_value_info();
    auto enum_type = make_thinknode_type_info_with_enum_type(enum_info);
    check_url_type_string(session, enum_type, "enum/2/abc/def");

    auto ref_type = make_thinknode_type_info_with_reference_type(named_type);
    check_url_type_string(
        session, ref_type, "reference/named/my_account/my_app/my_type");
}

TEST_CASE("ISS POST", "[thinknode][iss]")
{
    service_core service;
    init_test_service(service);

    auto& mock_http = enable_http_mocking(service);
    mock_http.set_script(
        {{make_http_request(
              http_request_method::POST,
              "https://mgh.thinknode.io/api/v1.0/iss/string?context=123",
              {{"Authorization", "Bearer xyz"},
               {"Accept", "application/json"},
               {"Content-Type", "application/octet-stream"}},
              value_to_msgpack_blob(dynamic("payload"))),
          make_http_200_response("{ \"id\": \"def\" }")}});

    thinknode_session session;
    session.api_url = "https://mgh.thinknode.io/api/v1.0";
    session.access_token = "xyz";

    auto id = cppcoro::sync_wait(post_iss_object(
        thinknode_request_context{service, session, nullptr},
        "123",
        make_thinknode_type_info_with_string_type(thinknode_string_type()),
        dynamic("payload")));
    REQUIRE(id == "def");

    REQUIRE(mock_http.is_complete());
    REQUIRE(mock_http.is_in_order());
}

TEST_CASE("ISS object copy", "[thinknode][iss]")
{
    service_core service;
    init_test_service(service);

    auto& mock_http = enable_http_mocking(service);
    mock_http.set_script(
        {{make_http_request(
              http_request_method::POST,
              "https://mgh.thinknode.io/api/v1.0/iss/def/buckets/"
              "abc?context=123",
              {{"Authorization", "Bearer xyz"}},
              blob()),
          make_http_200_response("")}});

    thinknode_session session;
    session.api_url = "https://mgh.thinknode.io/api/v1.0";
    session.access_token = "xyz";

    thinknode_request_context ctx{service, session, nullptr};
    cppcoro::sync_wait(shallowly_copy_iss_object(ctx, "abc", "123", "def"));

    REQUIRE(mock_http.is_complete());
    REQUIRE(mock_http.is_in_order());
}
