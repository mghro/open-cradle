#include <cradle/background/requests.h>

#include <cradle/utilities/testing.h>

using namespace cradle;

// Wait up to a second to see if a condition occurs (i.e., returns true).
// Check once per millisecond to see if it occurs.
// Return whether or not it occurs.
template<class Condition>
bool
occurs_soon(Condition&& condition)
{
    int n = 0;
    while (true)
    {
        if (std::forward<Condition>(condition)())
            return true;
        if (++n > 1000)
            return false;
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
}

template<class Value>
void
check_request_value(
    request_resolution_system& sys,
    request_interface<Value>& request,
    Value const& expected_value)
{
    bool was_evaluated = false;
    post_request(sys, request, [&](Value value) {
        was_evaluated = true;
        REQUIRE(value == expected_value);
    });
    REQUIRE(was_evaluated);
}

template<class Value>
void
check_async_request_value(
    request_resolution_system& sys,
    request_interface<Value>& request,
    Value const& expected_value)
{
    bool was_evaluated = false;
    post_request(sys, request, [&](Value value) {
        was_evaluated = true;
        REQUIRE(value == expected_value);
    });
    REQUIRE(occurs_soon([&]() -> bool { return was_evaluated; }));
}

TEST_CASE("value requests", "[background]")
{
    request_resolution_system sys;

    auto four = rq::value(4);

    check_request_value(sys, four, 4);
}

TEST_CASE("apply requests", "[background]")
{
    request_resolution_system sys;

    auto four = rq::value(4);
    auto two = rq::value(2);

    auto sum = rq::apply([](auto x, auto y) { return x + y; }, four, two);
    check_request_value(sys, sum, 6);

    auto difference
        = rq::apply([](auto x, auto y) { return x - y; }, four, two);
    check_request_value(sys, difference, 2);
}

TEST_CASE("meta requests", "[background]")
{
    request_resolution_system sys;

    auto four = rq::value(4);
    auto two = rq::value(2);
    auto sum_generator = [](auto x, auto y) {
        return rq::apply(
            [](auto x, auto y) { return x + y; }, rq::value(x), rq::value(y));
    };
    auto sum = rq::meta(rq::apply(sum_generator, four, two));

    check_request_value(sys, sum, 6);
}

TEST_CASE("async requests", "[background]")
{
    request_resolution_system sys;

    std::atomic<bool> allowed_to_execute = false;
    std::atomic<bool> executed = false;

    auto four = rq::value(4);
    auto two = rq::value(2);
    auto f = [&allowed_to_execute](auto x, auto y) {
        while (!allowed_to_execute)
            std::this_thread::yield();
        return x + y;
    };
    auto sum = rq::async(f, four, two);

    post_request(sys, sum, [&executed](int value) {
        executed = true;
        REQUIRE(value == 6);
    });
    REQUIRE(!executed);
    allowed_to_execute = true;
    REQUIRE(occurs_soon([&]() -> bool { return executed; }));
}

TEST_CASE("cached requests", "[background]")
{
    request_resolution_system sys;

    auto four = rq::value(4);
    auto two = rq::value(2);

    int call_count = 0;
    auto counted_add = [&call_count](auto x, auto y) {
        ++call_count;
        return x + y;
    };

    auto sum = rq::cached(
        combine_ids(make_function_id(counted_add), make_id(4), make_id(2)),
        rq::apply(counted_add, four, two));
    check_async_request_value(sys, sum, 6);
    REQUIRE(call_count == 1);

    auto same_sum = rq::cached(
        combine_ids(make_function_id(counted_add), make_id(4), make_id(2)),
        rq::apply(counted_add, four, two));
    check_async_request_value(sys, same_sum, 6);
    REQUIRE(call_count == 1);

    auto different_sum = rq::cached(
        combine_ids(make_function_id(counted_add), make_id(2), make_id(2)),
        rq::apply(counted_add, two, two));
    check_async_request_value(sys, different_sum, 4);
    REQUIRE(call_count == 2);
}
