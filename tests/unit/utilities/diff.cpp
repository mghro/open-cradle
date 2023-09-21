#include <cradle/typing/utilities/diff.hpp>

#include <cradle/typing/core.h>
#include <cradle/typing/utilities/testing.h>

using namespace cradle;

void
test_diff(dynamic const& a, dynamic const& b, value_diff const& expected_diff)
{
    auto diff = compute_value_diff(a, b);
    REQUIRE(diff == expected_diff);
    REQUIRE(apply_value_diff(a, diff) == b);
}

TEST_CASE("simple diffs", "[core][diff]")
{
    test_diff(
        dynamic("foo"),
        dynamic("bar"),
        {make_value_diff_item(
            {},
            value_diff_op::UPDATE,
            some(dynamic("foo")),
            some(dynamic("bar")))});
}

TEST_CASE("array diffs", "[core][diff]")
{
    test_diff(
        {dynamic(0.), dynamic(1.), dynamic(2.)},
        {dynamic(0.), dynamic(1.), dynamic(3.)},
        {make_value_diff_item(
            {dynamic(integer(2))},
            value_diff_op::UPDATE,
            some(dynamic(2.)),
            some(dynamic(3.)))});

    test_diff(
        {dynamic(0.), dynamic(1.), dynamic(2.)},
        {dynamic(1.), dynamic(3.)},
        {make_value_diff_item(
            {},
            value_diff_op::UPDATE,
            some(dynamic({dynamic(0.), dynamic(1.), dynamic(2.)})),
            some(dynamic({dynamic(1.), dynamic(3.)})))});

    test_diff(
        {dynamic(0.),
         dynamic(3.),
         dynamic(2.),
         dynamic(4.),
         dynamic(5.),
         dynamic(6.),
         dynamic(7.)},
        {dynamic(1.),
         dynamic(3.),
         dynamic(2.),
         dynamic(0.),
         dynamic(5.),
         dynamic(6.),
         dynamic(7.)},
        {make_value_diff_item(
             {dynamic(integer(0))},
             value_diff_op::UPDATE,
             some(dynamic(0.)),
             some(dynamic(1.))),
         make_value_diff_item(
             {dynamic(integer(3))},
             value_diff_op::UPDATE,
             some(dynamic(4.)),
             some(dynamic(0.)))});

    test_diff(
        {dynamic(0.), dynamic(1.), dynamic(2.)},
        {dynamic(1.), dynamic(1.), dynamic(2.)},
        {make_value_diff_item(
            {dynamic(integer(0))},
            value_diff_op::UPDATE,
            some(dynamic(0.)),
            some(dynamic(1.)))});

    test_diff(
        {dynamic(0.), dynamic(1.), dynamic(2.)},
        {dynamic(0.), dynamic(1.)},
        {make_value_diff_item(
            {dynamic(integer(2))},
            value_diff_op::DELETE,
            some(dynamic(2.)),
            none)});

    test_diff(
        {dynamic(0.), dynamic(1.), dynamic(2.)},
        {dynamic(0.), dynamic(2.)},
        {make_value_diff_item(
            {dynamic(integer(1))},
            value_diff_op::DELETE,
            some(dynamic(1.)),
            none)});

    test_diff(
        {dynamic(0.), dynamic(1.), dynamic(2.)},
        {dynamic(1.), dynamic(2.)},
        {make_value_diff_item(
            {dynamic(integer(0))},
            value_diff_op::DELETE,
            some(dynamic(0.)),
            none)});

    test_diff(
        {dynamic(3.),
         dynamic(1.),
         dynamic(2.),
         dynamic(4.),
         dynamic(6.),
         dynamic(0.),
         dynamic(4.)},
        {dynamic(2.), dynamic(4.), dynamic(6.), dynamic(0.), dynamic(4.)},
        {make_value_diff_item(
             {dynamic(integer(1))},
             value_diff_op::DELETE,
             some(dynamic(1.)),
             none),
         make_value_diff_item(
             {dynamic(integer(0))},
             value_diff_op::DELETE,
             some(dynamic(3.)),
             none)});

    test_diff(
        {dynamic(3.), dynamic(1.), dynamic(0.), dynamic(2.)},
        dynamic({dynamic(2.)}),
        {make_value_diff_item(
            {},
            value_diff_op::UPDATE,
            some(
                dynamic({dynamic(3.), dynamic(1.), dynamic(0.), dynamic(2.)})),
            some(dynamic({dynamic(2.)})))});

    test_diff(
        {dynamic(0.), dynamic(1.)},
        {dynamic(0.), dynamic(2.), dynamic(1.)},
        {make_value_diff_item(
            {dynamic(integer(1))}, value_diff_op::INSERT, none, dynamic(2.))});

    test_diff(
        {dynamic(1.), dynamic(2.)},
        {dynamic(0.), dynamic(1.), dynamic(2.)},
        {make_value_diff_item(
            {dynamic(integer(0))}, value_diff_op::INSERT, none, dynamic(0.))});

    test_diff(
        {dynamic(0.), dynamic(1.)},
        {dynamic(0.), dynamic(1.), dynamic(2.)},
        {make_value_diff_item(
            {dynamic(integer(2))}, value_diff_op::INSERT, none, dynamic(2.))});

    test_diff(
        {dynamic(0.), dynamic(1.), dynamic(4.), dynamic(3.), dynamic(1.)},
        {dynamic(0.),
         dynamic(3.),
         dynamic(2.),
         dynamic(1.),
         dynamic(4.),
         dynamic(3.),
         dynamic(1.)},
        {make_value_diff_item(
             {dynamic(integer(1))}, value_diff_op::INSERT, none, dynamic(3.)),
         make_value_diff_item(
             {dynamic(integer(2))},
             value_diff_op::INSERT,
             none,
             dynamic(2.))});
}

TEST_CASE("map diffs", "[core][diff]")
{
    test_diff(
        {{dynamic("foo"), dynamic(0.)}, {dynamic("bar"), dynamic(1.)}},
        {{dynamic("foo"), dynamic(3.)}, {dynamic("bar"), dynamic(1.)}},
        {make_value_diff_item(
            {dynamic("foo")},
            value_diff_op::UPDATE,
            some(dynamic(0.)),
            some(dynamic(3.)))});

    test_diff(
        {{dynamic("foo"), dynamic(0.)}, {dynamic("bar"), dynamic(1.)}},
        {{dynamic("foo"), dynamic(0.)}},
        {make_value_diff_item(
            {dynamic("bar")},
            value_diff_op::DELETE,
            some(dynamic(1.)),
            none)});

    test_diff(
        {{dynamic("foo"), dynamic(0.)}},
        {{dynamic("foo"), dynamic(0.)}, {dynamic("bar"), dynamic(1.)}},
        {make_value_diff_item(
            {dynamic("bar")},
            value_diff_op::INSERT,
            none,
            some(dynamic(1.)))});

    test_diff(
        {{dynamic("abc"), dynamic(1.)},
         {dynamic("foo"), dynamic(0.)},
         {dynamic("bar"), dynamic(1.)},
         {dynamic("other"),
          dynamic("irrelevant but unchanged stuff to ensure that the 'simple' "
                  "diff is larger than the 'compressed' one")}},
        {{dynamic("abc"), dynamic(1.)},
         {dynamic("foo"), dynamic(3.)},
         {dynamic("baz"), dynamic(0.)},
         {dynamic("other"),
          dynamic("irrelevant but unchanged stuff to ensure that the 'simple' "
                  "diff is larger than the 'compressed' one")}},
        {make_value_diff_item(
             {dynamic("bar")}, value_diff_op::DELETE, dynamic(1.), none),
         make_value_diff_item(
             {dynamic("baz")}, value_diff_op::INSERT, none, some(dynamic(0.))),
         make_value_diff_item(
             {dynamic("foo")},
             value_diff_op::UPDATE,
             some(dynamic(0.)),
             some(dynamic(3.)))});
}

TEST_CASE("nested diffs", "[core][diff]")
{
    auto map_a = dynamic(
        {{dynamic("foo"), dynamic(0.)}, {dynamic("bar"), dynamic(1.)}});
    auto map_b = dynamic(
        {{dynamic("foo"), dynamic(3.)}, {dynamic("baz"), dynamic(0.)}});
    auto map_c = dynamic({{dynamic("related"), dynamic(0.)}});
    auto map_d = dynamic(
        {{dynamic("un"), dynamic(5.)}, {dynamic("related"), dynamic(0.)}});

    test_diff(
        {map_c, map_a},
        {map_d, map_b},
        {make_value_diff_item(
             {dynamic(integer(0)), dynamic("un")},
             value_diff_op::INSERT,
             none,
             some(dynamic(5.))),
         make_value_diff_item(
             {dynamic(integer(1))},
             value_diff_op::UPDATE,
             some(map_a),
             some(map_b))});

    auto map_e = dynamic(
        {{dynamic("un"), {dynamic(0.), dynamic(5.)}},
         {dynamic("related"), dynamic(0.)}});
    auto map_f = dynamic(
        {{dynamic("un"), {dynamic(0.), dynamic(4.)}},
         {dynamic("related"), dynamic(0.)}});

    test_diff(
        dynamic({map_a, map_b, map_e}),
        dynamic({map_a, map_b, map_f}),
        {make_value_diff_item(
            {dynamic(integer(2)), dynamic("un"), dynamic(integer(1))},
            value_diff_op::UPDATE,
            some(dynamic(5.)),
            some(dynamic(4.)))});
}
