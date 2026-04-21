#pragma once

#include <cstdint>
#include <string_view>

#include "event.hpp"
#include "server/server_types.hpp"
#include "server/command.hpp"

namespace server {

// Protocol adapter decodes raw Event into parser-ready input.
// IMPORTANT:
// - no std::string allocation forced by the contract
// - adapter returns a string_view into Event-owned storage or another stable buffer
template <typename Derived>
class ProtocolAdapterCRTP {
public:
    [[nodiscard]] ProtocolKind kind() const noexcept {
        return d().kindImpl();
    }

    [[nodiscard]] std::string_view name() const noexcept {
        return d().nameImpl();
    }

    [[nodiscard]] bool supportsPort(uint16_t port) const noexcept {
        return d().supportsPortImpl(port);
    }

    [[nodiscard]] bool decodeView(const ::Event& ev,
                                  std::string_view& out) noexcept {
        return d().decodeViewImpl(ev, out);
    }

protected:
    ~ProtocolAdapterCRTP() = default;

private:
    Derived& d() noexcept { return static_cast<Derived&>(*this); }
    const Derived& d() const noexcept { return static_cast<const Derived&>(*this); }
};

template <typename Derived>
class ParserCRTP {
public:
    [[nodiscard]] bool parse(std::string_view input,
                             command::Command& out) noexcept {
        return d().parseImpl(input, out);
    }

    [[nodiscard]] std::string_view name() const noexcept {
        return d().nameImpl();
    }

protected:
    ~ParserCRTP() = default;

private:
    Derived& d() noexcept { return static_cast<Derived&>(*this); }
    const Derived& d() const noexcept { return static_cast<const Derived&>(*this); }
};

template <typename Derived>
class ExecutorCRTP {
public:
    [[nodiscard]] bool execute(const command::Command& cmd,
                               result::Result& out) noexcept {
        return d().executeImpl(cmd, out);
    }

    [[nodiscard]] std::string_view name() const noexcept {
        return d().nameImpl();
    }

protected:
    ~ExecutorCRTP() = default;

private:
    Derived& d() noexcept { return static_cast<Derived&>(*this); }
    const Derived& d() const noexcept { return static_cast<const Derived&>(*this); }
};

template <typename Derived>
class DistributorCRTP {
public:
    void distribute(const result::Result& res) noexcept {
        d().distributeImpl(res);
    }

    [[nodiscard]] std::string_view name() const noexcept {
        return d().nameImpl();
    }

protected:
    ~DistributorCRTP() = default;

private:
    Derived& d() noexcept { return static_cast<Derived&>(*this); }
    const Derived& d() const noexcept { return static_cast<const Derived&>(*this); }
};

struct ListenerRuntime {
    uint16_t port = 0;
    ProtocolKind protocol = ProtocolKind::custom;
    bool enabled = true;
};

} // namespace server
