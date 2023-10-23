#include <memory>
#include <sstream>
#include <string>

#include <catch2/catch.hpp>
#include <fmt/format.h>

#include "../../support/inner_service.h"
#include <cradle/inner/core/type_definitions.h>
#include <cradle/inner/core/type_interfaces.h>
#include <cradle/inner/encodings/msgpack_adaptors_rpclib.h>
#include <cradle/inner/service/resources.h>

using namespace cradle;

static std::string
make_string(std::vector<size_t> const& v)
{
    std::string s;
    for (auto i : v)
    {
        s += static_cast<char>(i);
    }
    return s;
}

static void
test_one(blob const& x, std::string const& expected, bool with_zone)
{
    std::stringstream ss;
    if (with_zone)
    {
        auto z = std::make_unique<clmdep_msgpack::zone>();
        clmdep_msgpack::object x_obj{x, *z};
        clmdep_msgpack::pack(ss, x_obj);
    }
    else
    {
        clmdep_msgpack::pack(ss, x);
    }
    auto serialized = ss.str();
    REQUIRE(serialized == expected);

    clmdep_msgpack::object_handle y_oh
        = clmdep_msgpack::unpack(serialized.c_str(), serialized.size());
    clmdep_msgpack::object y_obj = y_oh.get();
    blob y;
    y_obj.convert(y);
    REQUIRE(y == x);
}

static void
test_both(blob const& x, std::string const& expected)
{
    test_one(x, expected, false);
    test_one(x, expected, true);
}

TEST_CASE("msgpack converting empty blob (rpclib)", "[encodings][msgpack]")
{
    // bin format family, 0 bytes
    std::vector<size_t> expected{0xc4, 0x00};
    test_both(blob(), make_string(expected));
}

TEST_CASE("msgpack converting plain blob (rpclib)", "[encodings][msgpack]")
{
    // bin format family, 5 bytes
    std::vector<size_t> expected{0xc4, 0x05, 'a', 'b', 'c', 'd', 'e'};
    test_both(make_string_literal_blob("abcde"), make_string(expected));
}

TEST_CASE("msgpack converting file blob (rpclib)", "[encodings][msgpack]")
{
    auto resources{make_inner_test_resources()};
    auto shared_writer{resources->make_blob_file_writer(3)};
    auto& writer{*shared_writer};
    std::memcpy(writer.data(), "fgh", 3);
    writer.on_write_completed();

    std::string path{writer.mapped_file()};
    std::string expected;
    if (path.size() <= 31)
    {
        // str format family, <= 31 bytes
        std::vector<size_t> header{0xa0 + path.size()};
        expected = make_string(header) + path;
    }
    else if (path.size() <= 255)
    {
        // str format family, <= 255 bytes
        std::vector<size_t> header{0xd9, path.size()};
        expected = make_string(header) + path;
    }
    else
    {
        // str format family, <= 65535 bytes
        std::vector<size_t> header{
            0xda, path.size() / 256, path.size() & 0xff};
        expected = make_string(header) + path;
    }
    test_both(blob{shared_writer, writer.bytes(), writer.size()}, expected);
}

TEST_CASE(
    "msgpack decoding throws on bad data (rpclib)", "[encodings][msgpack]")
{
    int x{};
    std::stringstream ss;
    clmdep_msgpack::pack(ss, x);
    auto serialized = ss.str();

    clmdep_msgpack::object_handle y_oh
        = clmdep_msgpack::unpack(serialized.c_str(), serialized.size());
    clmdep_msgpack::object y_obj = y_oh.get();
    blob y;
    REQUIRE_THROWS(y_obj.convert(y));
}

static void
test_one_throws(blob const& x, bool with_zone)
{
    std::stringstream ss;
    if (with_zone)
    {
        auto z = std::make_unique<clmdep_msgpack::zone>();
        REQUIRE_THROWS(clmdep_msgpack::object{x, *z});
    }
    else
    {
        REQUIRE_THROWS(clmdep_msgpack::pack(ss, x));
    }
}

static void
test_both_throw(blob const& x)
{
    test_one_throws(x, false);
    test_one_throws(x, true);
}

TEST_CASE(
    "msgpack encoding throws on blob >= 4GB (rpclib)", "[encodings][msgpack]")
{
    std::byte data[1]{};
    test_both_throw(blob(data, 0x1'00'00'00'00));
}
