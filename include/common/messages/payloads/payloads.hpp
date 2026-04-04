#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <ostream>
#include <string_view>

#include "common/messages/log_message.hpp"
#include "publisher/api/publishable.hpp"
#include "publisher/runtime/registration_handle.hpp"

template<std::size_t N>
struct Padding
{
    std::byte padding[N];
};

template<>
struct Padding<0>
{
};

enum class Severity : std::uint8_t { Info, Warn, Error };

inline std::ostream& operator<<(std::ostream& os, Severity s)
{
    switch (s)
    {
        case Severity::Info:  return os << "Info";
        case Severity::Warn:  return os << "Warn";
        case Severity::Error: return os << "Error";
    }

    return os << "Unknown(" << static_cast<int>(static_cast<std::uint8_t>(s)) << ')';
}

#ifndef MSG_PAD_TO_64B
#define MSG_PAD_TO_64B 1
#endif

template<MsgTag Tag, typename Derived>
struct PayloadBase : public publisher::api::Publishable<Derived>
{
    using PublishableBase = publisher::api::Publishable<Derived>;
    static constexpr MsgTag type_id = Tag;

    explicit PayloadBase(publisher::runtime::RegistrationHandle handle)
        : PublishableBase(std::move(handle))
    {
    }

    #define X(C,F) C F;
    #include "common/messages/payloads/log_payloads.def"
    #undef X

#if MSG_PAD_TO_64B
    struct _FieldsSizeProbe
    {
        #define X(C,F) C F;
        #include "common/messages/payloads/log_payloads.def"
        #undef X
    };

    static constexpr std::size_t _fields_size = sizeof(_FieldsSizeProbe);

    static constexpr std::size_t _pad_bytes = []() constexpr
    {
        if constexpr (Tag == MsgTag::Generic)
        {
            return (_fields_size < 64u) ? (64u - _fields_size) : 0u;
        }
        else
        {
            return 0u;
        }
    }();

    [[no_unique_address]] Padding<_pad_bytes> _padding{};

    static_assert(Tag != MsgTag::Generic || _fields_size <= 64u,
                  "Generic PayloadBase fields exceed 64 bytes");
#else
    static constexpr std::size_t _pad_bytes = 0;
#endif

    constexpr MsgTag tag() const noexcept
    {
        return type_id;
    }

    void print_header(std::ostream& os) const
    {
        os << "[tag=" << static_cast<int>(type_id) << "] ";

        #define X(C,F) os << #F << '=' << F << ' ';
        #include "common/messages/payloads/log_payloads.def"
        #undef X
    }

    void debug_print(std::ostream& os) const
    {
        static_cast<const Derived*>(this)->debug_impl(os);
    }
};

template<MsgTag Tag, typename Derived>
std::ostream& operator<<(std::ostream& os, const PayloadBase<Tag, Derived>& p)
{
    p.debug_print(os);
    return os;
}