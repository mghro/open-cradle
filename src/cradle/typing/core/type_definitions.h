#ifndef CRADLE_TYPING_CORE_TYPE_DEFINITIONS_H
#define CRADLE_TYPING_CORE_TYPE_DEFINITIONS_H

#include <any>
#include <iostream>
#include <map>
#include <optional>
#include <string>
#include <variant>

#include <boost/core/noncopyable.hpp>
#include <boost/date_time/posix_time/posix_time_types.hpp>

#include <cradle/inner/core/type_definitions.h>

namespace cradle {

using boost::noncopyable;

using std::string;

using std::optional;

typedef int64_t integer;

// nil_t is a unit type. It has only one possible value, :nil.
struct nil_t
{
};
static nil_t nil;

// type_info_query<T>::get(&info) should set :info to the CRADLE type info for
// the type T from the perspective of someone *using* T. This might not be the
// full definition of T (e.g., for named types, it's just the name).
//
// All CRADLE regular types must provide a specialization of this.
//
template<class T>
struct type_info_query
{
};

struct api_type_info;

// definitive_type_info_query<T>::get(&info) should set *info to the definitive
// CRADLE type info for the type T. This is always the full definition of the
// type, even for named types.
//
// The default implementation of this simply forwards to the regular query.
//
template<class T>
struct definitive_type_info_query : type_info_query<T>
{
};

// enum_type_info_query<T>::get(&info) should set :info to the enum type info
// for the type T.
//
// All CRADLE enum types must provide a specialization of this.
//
template<class T>
struct enum_type_info_query
{
};

struct api_enum_info;

// structure_field_type_info_adder<T>::add(&fields), where T is a structure
// type, adds the CRADLE type info for the fields of T to the map :fields.
//
// This should be implemented by all CRADLE structure types.
//
template<class T>
struct structure_field_type_info_adder
{
};

struct dynamic;

enum class value_type
{
    NIL, // nil_t - no value
    BOOLEAN, // bool
    INTEGER, // integer
    FLOAT, // double
    STRING, // string
    BLOB, // blob - binary blob
    DATETIME, // boost::posix_time::ptime
    ARRAY, // dynamic_array - array of dynamic values
    MAP, // dynamic_map - collection of named dynamic values
};

// Arrays are represented as std::vectors and can be manipulated as such.
typedef std::vector<dynamic> dynamic_array;

// Maps are represented as std::maps and can be manipulated as such.
typedef std::map<dynamic, dynamic> dynamic_map;

using dynamic_storage = std::variant<
    nil_t,
    bool,
    integer,
    double,
    string,
    blob,
    boost::posix_time::ptime,
    dynamic_array,
    dynamic_map>;

struct dynamic
{
    // CONSTRUCTORS

    // Default construction creates a nil value.
    dynamic()
    {
        set(nil);
    }

    // Construct a dynamic from one of the base types.
    dynamic(nil_t v)
    {
        set(v);
    }
    dynamic(bool v)
    {
        set(v);
    }
    dynamic(integer v)
    {
        set(v);
    }
    dynamic(double v)
    {
        set(v);
    }
    dynamic(string const& v)
    {
        set(v);
    }
    dynamic(string&& v)
    {
        set(std::move(v));
    }
    dynamic(char const* v)
    {
        set(string(v));
    }
    dynamic(blob const& v)
    {
        set(v);
    }
    dynamic(blob&& v)
    {
        set(std::move(v));
    }
    dynamic(boost::posix_time::ptime const& v)
    {
        set(v);
    }
    dynamic(boost::posix_time::ptime&& v)
    {
        set(std::move(v));
    }
    dynamic(dynamic_array const& v)
    {
        set(v);
    }
    dynamic(dynamic_array&& v)
    {
        set(std::move(v));
    }
    dynamic(dynamic_map const& v)
    {
        set(v);
    }
    dynamic(dynamic_map&& v)
    {
        set(std::move(v));
    }

    // Construct from an initializer list.
    dynamic(std::initializer_list<dynamic> list);

    // GETTERS

    // Get the type of value stored here.
    value_type
    type() const
    {
        return value_type(storage_.index());
    }

    // Get the contents.
    // This should be used with caution.
    // cast<T>(dynamic) provides a safer interface to this.
    dynamic_storage const&
    contents() const&
    {
        return storage_;
    }

    // Get a non-const reference to the contents.
    // This should be used with caution.
    // cast<T>(dynamic) provides a safer interface to this.
    dynamic_storage&
    contents() &
    {
        return storage_;
    }

    // Get an r-value reference to the contents.
    // This should be used with caution.
    // cast<T>(dynamic) provides a safer interface to this.
    dynamic_storage&&
    contents() &&
    {
        return std::move(storage_);
    }

 private:
    void
    set(nil_t _)
    {
        storage_ = _;
    }
    void
    set(bool v)
    {
        storage_ = v;
    }
    void
    set(integer v)
    {
        storage_ = v;
    }
    void
    set(double v)
    {
        storage_ = v;
    }
    void
    set(string const& v)
    {
        storage_ = v;
    }
    void
    set(string&& v)
    {
        storage_ = std::move(v);
    }
    void
    set(char const* v)
    {
        set(string(v));
    }
    void
    set(blob const& v)
    {
        storage_ = v;
    }
    void
    set(blob&& v)
    {
        storage_ = std::move(v);
    }
    void
    set(boost::posix_time::ptime const& v)
    {
        storage_ = v;
    }
    void
    set(boost::posix_time::ptime&& v)
    {
        storage_ = std::move(v);
    }
    void
    set(dynamic_array const& v)
    {
        storage_ = v;
    }
    void
    set(dynamic_array&& v)
    {
        storage_ = std::move(v);
    }
    void
    set(dynamic_map const& v)
    {
        storage_ = v;
    }
    void
    set(dynamic_map&& v)
    {
        storage_ = std::move(v);
    }

    friend void
    swap(dynamic& a, dynamic& b);

    dynamic_storage storage_;
};

// omissible<T> is essentially the same as optional<T>, but it obeys
// Thinknode's behavior for omissible fields. (It should only be used as a
// field in a structure.)
template<class T>
struct omissible
{
    typedef T value_type;
    omissible()
    {
    }
    omissible(T value) : wrapped_(std::move(value))
    {
    }
    omissible(none_t) : wrapped_(none)
    {
    }
    omissible(optional<T> opt) : wrapped_(std::move(opt))
    {
    }
    omissible&
    operator=(T value)
    {
        wrapped_ = std::move(value);
        return *this;
    }
    omissible&
    operator=(none_t)
    {
        wrapped_ = none;
        return *this;
    }
    omissible&
    operator=(optional<T> opt)
    {
        wrapped_ = std::move(opt);
        return *this;
    }
    // allows use within if statements without other unintended conversions
    typedef std::optional<T> omissible::*unspecified_bool_type;
    operator unspecified_bool_type() const
    {
        return wrapped_ ? &omissible::wrapped_ : 0;
    }
    operator optional<T>() const
    {
        return wrapped_;
    }
    T const&
    operator*() const
    {
        return *wrapped_;
    }
    T&
    operator*()
    {
        return *wrapped_;
    }
    T const*
    operator->() const
    {
        assert(wrapped_);
        return &*wrapped_;
    }
    T*
    operator->()
    {
        assert(wrapped_);
        return &*wrapped_;
    }

 private:
    std::optional<T> wrapped_;
};

// IMMUTABLES

struct untyped_immutable_value : noncopyable
{
    virtual ~untyped_immutable_value()
    {
    }
    virtual api_type_info
    type_info() const
        = 0;
    virtual size_t
    deep_size() const
        = 0;
    virtual size_t
    hash() const
        = 0;
    virtual dynamic
    as_dynamic() const
        = 0;
    virtual bool
    equals(untyped_immutable_value const* other) const
        = 0;
};

struct untyped_immutable
{
    std::shared_ptr<untyped_immutable_value> ptr;
};

} // namespace cradle

#endif
