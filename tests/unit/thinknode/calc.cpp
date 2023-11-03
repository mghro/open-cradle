#include <cppcoro/sync_wait.hpp>

#include <fmt/format.h>

#include <fakeit.h>

#include "../../support/thinknode.h"
#include <cradle/inner/core/monitoring.h>
#include <cradle/inner/io/mock_http.h>
#include <cradle/inner/utilities/for_async.h>
#include <cradle/thinknode/calc.h>
#include <cradle/typing/encodings/json.h>
#include <cradle/typing/utilities/testing.h>

using namespace cradle;
using namespace fakeit;

TEST_CASE("calc status utilities", "[thinknode][tn_calc]")
{
    // We can test most cases in the status ordering and query string
    // translation by simply constructing the expected order of query strings
    // and seeing if that's what repeated application of those functions
    // produces.
    std::vector<string> expected_query_order
        = {"status=waiting",
           "status=queued&queued=pending",
           "status=queued&queued=ready",
           // Do a couple of these manually just to make sure we're not
           // generating the same wrong string as in the actual code.
           "status=calculating&progress=0.00",
           "status=calculating&progress=0.01"};
    for (int i = 2; i != 100; ++i)
    {
        expected_query_order.push_back(
            fmt::format("status=calculating&progress={:4.2f}", i / 100.0));
    }
    expected_query_order.push_back("status=uploading&progress=0.00");
    for (int i = 1; i != 100; ++i)
    {
        expected_query_order.push_back(
            fmt::format("status=uploading&progress={:4.2f}", i / 100.0));
    }
    expected_query_order.push_back("status=completed");
    // Go through the entire progression, starting with the waiting status.
    auto status = some(make_calculation_status_with_waiting(nil));
    for (auto query_string : expected_query_order)
    {
        REQUIRE(calc_status_as_query_string(*status) == query_string);
        status = get_next_calculation_status(*status);
    }
    // Nothing further is possible.
    REQUIRE(!status);

    // Test the other cases that aren't covered above.
    {
        auto failed = make_calculation_status_with_failed(
            calculation_failure_status());
        REQUIRE(get_next_calculation_status(failed) == none);
        REQUIRE(calc_status_as_query_string(failed) == "status=failed");
    }
    {
        auto canceled = make_calculation_status_with_canceled(nil);
        REQUIRE(get_next_calculation_status(canceled) == none);
        REQUIRE(calc_status_as_query_string(canceled) == "status=canceled");
    }
    {
        auto generating = make_calculation_status_with_generating(nil);
        REQUIRE(
            get_next_calculation_status(generating)
            == some(make_calculation_status_with_queued(
                calculation_queue_type::READY)));
        REQUIRE(
            calc_status_as_query_string(generating) == "status=generating");
    }
}

TEST_CASE("calc status query", "[thinknode][tn_calc]")
{
    thinknode_test_scope scope;

    auto& mock_http = scope.enable_http_mocking();
    mock_http.set_script(
        {{make_get_request(
              "https://mgh.thinknode.io/api/v1.0/calc/abc/"
              "status?context=123",
              {{"Authorization", "Bearer xyz"},
               {"Accept", "application/json"}}),
          make_http_200_response("{ \"completed\": null }")}});

    auto ctx{scope.make_context()};
    auto status
        = cppcoro::sync_wait(query_calculation_status(ctx, "123", "abc"));
    REQUIRE(status == make_calculation_status_with_completed(nil));

    REQUIRE(mock_http.is_complete());
    REQUIRE(mock_http.is_in_order());
}

TEST_CASE("calc request retrieval", "[thinknode][tn_calc]")
{
    thinknode_test_scope scope;

    auto& mock_http = scope.enable_http_mocking();
    mock_http.set_script(
        {{make_get_request(
              "https://mgh.thinknode.io/api/v1.0/calc/abc?context=123",
              {{"Authorization", "Bearer xyz"},
               {"Accept", "application/json"}}),
          make_http_200_response("{ \"value\": [2.1, 4.2] }")}});

    auto ctx{scope.make_context()};
    auto request
        = cppcoro::sync_wait(retrieve_calculation_request(ctx, "123", "abc"));

    REQUIRE(
        request
        == make_thinknode_calc_request_with_value(dynamic({2.1, 4.2})));

    REQUIRE(mock_http.is_complete());
    REQUIRE(mock_http.is_in_order());
}

TEST_CASE("calc status long polling", "[thinknode][tn_calc]")
{
    thinknode_test_scope scope;

    auto& mock_http = scope.enable_http_mocking();
    mock_http.set_script(
        {{make_get_request(
              "https://mgh.thinknode.io/api/v1.0/calc/abc/status?context=123",
              {{"Authorization", "Bearer xyz"},
               {"Accept", "application/json"}}),
          make_http_200_response(value_to_json(
              to_dynamic(make_calculation_status_with_calculating(
                  calculation_calculating_status{0.115}))))},
         {make_get_request(
              "https://mgh.thinknode.io/api/v1.0/calc/abc/status"
              "?status=calculating&progress=0.12&timeout=120&context=123",
              {{"Authorization", "Bearer xyz"},
               {"Accept", "application/json"}}),
          make_http_200_response(
              value_to_json(to_dynamic(make_calculation_status_with_uploading(
                  calculation_uploading_status{0.995}))))},
         {make_get_request(
              "https://mgh.thinknode.io/api/v1.0/calc/abc/status"
              "?status=completed&timeout=120&context=123",
              {{"Authorization", "Bearer xyz"},
               {"Accept", "application/json"}}),
          make_http_200_response(value_to_json(
              to_dynamic(make_calculation_status_with_completed(nil))))}});

    std::vector<calculation_status> expected_statuses
        = {make_calculation_status_with_calculating(
               calculation_calculating_status{0.115}),
           make_calculation_status_with_uploading(
               calculation_uploading_status{0.995}),
           make_calculation_status_with_completed(nil)};

    auto ctx{scope.make_context()};
    cppcoro::sync_wait([&]() -> cppcoro::task<> {
        auto statuses = long_poll_calculation_status(ctx, "123", "abc");
        size_t status_counter = 0;
        co_await for_async(std::move(statuses), [&](auto status) {
            REQUIRE(status == expected_statuses.at(status_counter));
            ++status_counter;
        });
        REQUIRE(status_counter == expected_statuses.size());
    }());

    REQUIRE(mock_http.is_complete());
    REQUIRE(mock_http.is_in_order());
}

TEST_CASE("calc variable substitution", "[thinknode][tn_calc]")
{
    auto a_substitute = make_thinknode_calc_request_with_reference("abc");
    auto b_substitute = make_thinknode_calc_request_with_value(dynamic("def"));

    std::map<string, thinknode_calc_request> substitutions
        = {{"a", a_substitute}, {"b", b_substitute}};

    auto variable_a = make_thinknode_calc_request_with_variable("a");
    auto variable_b = make_thinknode_calc_request_with_variable("b");

    auto item_schema
        = make_thinknode_type_info_with_string_type(thinknode_string_type());

    // value
    auto value_calc = make_thinknode_calc_request_with_value(dynamic("xyz"));
    REQUIRE(substitute_variables(substitutions, value_calc) == value_calc);

    // reference
    REQUIRE(
        substitute_variables(
            substitutions, make_thinknode_calc_request_with_reference("a"))
        == make_thinknode_calc_request_with_reference("a"));

    // function
    REQUIRE(
        substitute_variables(
            substitutions,
            make_thinknode_calc_request_with_function(
                make_thinknode_function_application(
                    "my_account",
                    "my_name",
                    "my_function",
                    none,
                    {variable_b, value_calc, variable_a})))
        == make_thinknode_calc_request_with_function(
            make_thinknode_function_application(
                "my_account",
                "my_name",
                "my_function",
                none,
                {b_substitute, value_calc, a_substitute})));

    // array
    auto original_array
        = make_thinknode_calc_request_with_array(make_thinknode_array_calc(
            {variable_a, variable_b, value_calc}, item_schema));
    auto substituted_array
        = make_thinknode_calc_request_with_array(make_thinknode_array_calc(
            {a_substitute, b_substitute, value_calc}, item_schema));
    REQUIRE(
        substitute_variables(substitutions, original_array)
        == substituted_array);
    auto array_schema = make_thinknode_type_info_with_array_type(
        make_thinknode_array_info(item_schema, none));

    // item
    auto original_item
        = make_thinknode_calc_request_with_item(make_thinknode_item_calc(
            original_array,
            make_thinknode_calc_request_with_value(dynamic(integer(0))),
            item_schema));
    auto substituted_item
        = make_thinknode_calc_request_with_item(make_thinknode_item_calc(
            substituted_array,
            make_thinknode_calc_request_with_value(dynamic(integer(0))),
            item_schema));
    REQUIRE(
        substitute_variables(substitutions, original_item)
        == substituted_item);

    // object
    auto object_schema = make_thinknode_type_info_with_structure_type(
        make_thinknode_structure_info(
            {{"i", make_thinknode_structure_field_info("", none, item_schema)},
             {"j", make_thinknode_structure_field_info("", none, item_schema)},
             {"k",
              make_thinknode_structure_field_info("", none, item_schema)}}));
    auto original_object
        = make_thinknode_calc_request_with_object(make_thinknode_object_calc(
            {{"i", variable_b}, {"j", variable_a}, {"k", value_calc}},
            object_schema));
    auto substituted_object
        = make_thinknode_calc_request_with_object(make_thinknode_object_calc(
            {{"i", b_substitute}, {"j", a_substitute}, {"k", value_calc}},
            object_schema));
    REQUIRE(
        substitute_variables(substitutions, original_object)
        == substituted_object);

    // property
    auto original_property = make_thinknode_calc_request_with_property(
        make_thinknode_property_calc(
            original_object,
            make_thinknode_calc_request_with_value(dynamic("j")),
            item_schema));
    auto substituted_property = make_thinknode_calc_request_with_property(
        make_thinknode_property_calc(
            substituted_object,
            make_thinknode_calc_request_with_value(dynamic("j")),
            item_schema));
    REQUIRE(
        substitute_variables(substitutions, original_property)
        == substituted_property);

    // let
    REQUIRE_THROWS(substitute_variables(
        substitutions,
        make_thinknode_calc_request_with_let(
            make_thinknode_let_calc(substitutions, value_calc))));

    // variables
    REQUIRE(substitute_variables(substitutions, variable_a) == a_substitute);
    REQUIRE(substitute_variables(substitutions, variable_b) == b_substitute);
    REQUIRE_THROWS(substitute_variables(
        substitutions, make_thinknode_calc_request_with_variable("c")));

    // meta
    REQUIRE(
        substitute_variables(
            substitutions,
            make_thinknode_calc_request_with_meta(
                make_thinknode_meta_calc(original_array, array_schema)))
        == make_thinknode_calc_request_with_meta(
            make_thinknode_meta_calc(substituted_array, array_schema)));
}

TEST_CASE("let calculation submission", "[thinknode][tn_calc]")
{
    thinknode_session mock_session;
    mock_session.api_url = "https://mgh.thinknode.io/api/v1.0";
    mock_session.access_token = "xyz";

    string mock_context_id = "abc";

    auto function_call = make_thinknode_calc_request_with_function(
        make_thinknode_function_application(
            "my_account",
            "my_name",
            "my_function",
            none,
            {
                make_thinknode_calc_request_with_variable("b"),
                make_thinknode_calc_request_with_variable("a"),
            }));

    auto let_calculation
        = make_thinknode_calc_request_with_let(make_thinknode_let_calc(
            {{"a", make_thinknode_calc_request_with_value(dynamic("-a-"))},
             {"b", make_thinknode_calc_request_with_value(dynamic("-b-"))}},
            make_thinknode_calc_request_with_let(make_thinknode_let_calc(
                {{"c", make_thinknode_calc_request_with_value(dynamic("-c-"))},
                 {"d", function_call}},
                make_thinknode_calc_request_with_array(
                    make_thinknode_array_calc(
                        {make_thinknode_calc_request_with_variable("a"),
                         make_thinknode_calc_request_with_variable("b"),
                         make_thinknode_calc_request_with_variable("c"),
                         make_thinknode_calc_request_with_variable("d")},
                        make_thinknode_type_info_with_string_type(
                            thinknode_string_type())))))));

    std::vector<thinknode_calc_request> expected_requests
        = {make_thinknode_calc_request_with_value(dynamic("-a-")),
           make_thinknode_calc_request_with_value(dynamic("-b-")),
           make_thinknode_calc_request_with_value(dynamic("-c-")),
           make_thinknode_calc_request_with_function(
               make_thinknode_function_application(
                   "my_account",
                   "my_name",
                   "my_function",
                   none,
                   {make_thinknode_calc_request_with_reference("b-id"),
                    make_thinknode_calc_request_with_reference("a-id")})),
           make_thinknode_calc_request_with_array(make_thinknode_array_calc(
               {make_thinknode_calc_request_with_reference("a-id"),
                make_thinknode_calc_request_with_reference("b-id"),
                make_thinknode_calc_request_with_reference("c-id"),
                make_thinknode_calc_request_with_reference("d-id")},
               make_thinknode_type_info_with_string_type(
                   thinknode_string_type())))};

    std::vector<string> mock_responses
        = {"a-id", "b-id", "c-id", "d-id", "main-id"};

    Mock<calculation_submission_interface> mock_submitter;

    size_t request_counter = 0;
    When(Method(mock_submitter, submit))
        .AlwaysDo(
            [&](thinknode_session session,
                string context_id,
                thinknode_calc_request request,
                bool dry_run) -> cppcoro::task<optional<string>> {
                REQUIRE(session == mock_session);
                REQUIRE(context_id == mock_context_id);
                REQUIRE(request == expected_requests.at(request_counter));
                if (!dry_run)
                {
                    auto response = mock_responses.at(request_counter);
                    ++request_counter;
                    co_return some(response);
                }
                else
                {
                    ++request_counter;
                    co_return none;
                }
            });

    auto submission_info = cppcoro::sync_wait(submit_thinknode_let_calc(
        mock_submitter.get(),
        mock_session,
        mock_context_id,
        make_augmented_calculation_request(let_calculation, {"d"}),
        false));
    REQUIRE(request_counter == expected_requests.size());
    REQUIRE(submission_info);
    REQUIRE(submission_info->main_calc_id == "main-id");
    REQUIRE(
        submission_info->reported_subcalcs
        == std::vector<reported_calculation_info>{
            make_reported_calculation_info("d-id", "my_function")});
    REQUIRE(
        submission_info->other_subcalc_ids
        == (std::vector<string>{"a-id", "b-id", "c-id"}));

    request_counter = 0;
    submission_info = cppcoro::sync_wait(submit_thinknode_let_calc(
        mock_submitter.get(),
        mock_session,
        mock_context_id,
        make_augmented_calculation_request(let_calculation, {"d"}),
        true));
    REQUIRE(!submission_info);
}
