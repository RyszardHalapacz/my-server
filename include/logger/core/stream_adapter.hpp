#pragma once

#include <array>
#include <ostream>
#include <sstream>
#include <string>
#include <string_view>
#include <type_traits>

#include "common/messages/traits.hpp"

namespace logger::core::detail {

// Checks if type T has a member function:
//   void T::print_header(std::ostream&) const;
template<typename T>
concept HasPrintHeader = requires(const T& t, std::ostream& os) {
    { t.print_header(os) } -> std::same_as<void>;
};

// Checks if type T has a member function:
//   void T::debug_print(std::ostream&) const;
template<typename T>
concept HasDebugPrint = requires(const T& t, std::ostream& os) {
    { t.debug_print(os) } -> std::same_as<void>;
};

// Checks if type T can be inserted into std::ostream via operator<<
template<typename T>
concept OstreamInsertable = requires(const T& t, std::ostream& os) {
    { os << t } -> std::same_as<std::ostream&>;
};

// Envelope-like constraint:
// 1) type must expose print_header(...) – to ensure it is our "envelope"
//    (PayloadBase or a derived type),
// 2) and it must be printable either via debug_print(...) or operator<<.
template<typename T>
concept EnvelopeLike =
    HasPrintHeader<T> &&
    (HasDebugPrint<T> || OstreamInsertable<T>);

// Fixed-size streambuf — writes into an internal char array.
// No heap allocation. Overflow (message too long) truncates silently.
template<std::size_t N>
struct FixedStringBuf : std::streambuf
{
    FixedStringBuf()  { setp(buf_.data(), buf_.data() + N); }
    void reset()      { setp(buf_.data(), buf_.data() + N); }

    std::string_view view() const noexcept
    {
        return {buf_.data(), static_cast<std::size_t>(pptr() - pbase())};
    }

private:
    // Silently drop bytes that exceed the buffer.
    int_type overflow(int_type ch) override
    {
        return ch; // consume but discard
    }

    std::array<char, N> buf_{};
};

// ------------------------------------------------------------------
// StreamAdapter
//
// Converts an "envelope-like" object into std::string
// by using its debug_print(...) if available, or operator<< otherwise.
//
// This is the boundary between:
//   - the "envelope world" (PayloadBase / derived types),
//   - and the "pure string" world used by formatters / sinks / LogCore.
// ------------------------------------------------------------------
struct StreamAdapter {
    template<EnvelopeLike T>
    static std::string to_string(const T& obj) {
        std::ostringstream oss;
        print(obj, oss);
        return oss.str();
    }

private:
    // Preferred path: use debug_print(...) when available
    template<typename T>
        requires (EnvelopeLike<T> && HasDebugPrint<T>)
    static void print(const T& obj, std::ostream& os) {
        // We rely on the envelope/payload implementation to decide
        // the exact order and naming of fields.
        obj.debug_print(os);
    }

    // Fallback: use operator<< for envelope-like types without debug_print(...)
    template<typename T>
        requires (EnvelopeLike<T> && !HasDebugPrint<T> && OstreamInsertable<T>)
    static void print(const T& obj, std::ostream& os) {
        os << obj;
    }
};

} // namespace logger::detail
