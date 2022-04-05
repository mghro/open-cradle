#ifndef CRADLE_TESTS_INNER_SUPPORT_IDS_H
#define CRADLE_TESTS_INNER_SUPPORT_IDS_H

#include <memory>

#include <boost/lexical_cast.hpp>
#include <catch2/catch.hpp>

#include <cradle/inner/core/id.h>

// Test all the relevant ID operations on a pair of equal IDs.
inline void
test_equal_ids(cradle::id_interface const& a, cradle::id_interface const& b)
{
    REQUIRE(a == b);
    REQUIRE(b == a);
    REQUIRE(!(a < b));
    REQUIRE(!(b < a));
    REQUIRE(a.hash() == b.hash());
    REQUIRE(
        boost::lexical_cast<std::string>(a)
        == boost::lexical_cast<std::string>(b));
}

// Test all the ID operations on a single ID.
template<class Id>
void
test_single_id(Id const& id)
{
    test_equal_ids(id, id);

    std::shared_ptr<cradle::id_interface> clone(id.clone());
    test_equal_ids(id, *clone);

    Id copy;
    id.deep_copy(&copy);
    test_equal_ids(id, copy);

    // Copying a clone is sometimes different because the clone is free of
    // references to the surrounding stack frame.
    Id clone_copy;
    clone->deep_copy(&clone_copy);
    test_equal_ids(id, clone_copy);
}

// Test all the ID operations on a pair of different IDs.
template<class A, class B>
void
test_different_ids(A const& a, B const& b)
{
    test_single_id(a);
    test_single_id(b);
    REQUIRE(a != b);
    REQUIRE((a < b && !(b < a) || b < a && !(a < b)));
    REQUIRE(a.hash() != b.hash());
    REQUIRE(
        boost::lexical_cast<std::string>(a)
        != boost::lexical_cast<std::string>(b));
}

#endif
