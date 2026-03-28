///////////////////////////////////////////////
// PolicyBase + concrete policies
///////////////////////////////////////////////
#pragma once
#include <string>
#include <string_view>
#include <fstream>
#include <cstdio>
#include <utility>

template<typename Derived, typename Sink>
struct PolicyBase {
    using sink_type = Sink;
    using view_type = std::string_view;

    // Stateless base: no data members.
    PolicyBase() = default;

    // Entry point used by the core to publish a single, fully formatted line.
    //
    // Pipeline:
    //  1. Take a lightweight view over the already-built log line.
    //  2. Ask the Sink (formatting policy) to produce a concrete std::string.
    //  3. Pass a string_view pointing to that string into Derived::write_impl.
    //
    // Contract:
    //  - publish is synchronous: write_impl must complete before publish returns.
    //  - write_impl MUST NOT store the string_view beyond the call.
    void publish(view_type line) {
        // 1) Sink formats the input view into a temporary std::string.
        //    Sink is a pure policy type with a static format(...) function.
        std::string msg = sink_type::format(line);

        // 2) Manually build a string_view over the string's buffer.
        //    We keep it explicit and portable (no relying on implicit conversions).
        view_type msg_view{msg.data(), msg.size()};

        // 3) Delegate the final I/O step to the derived policy.
        static_cast<Derived*>(this)->write_impl(msg_view);
        // msg lives until the end of this function, so msg_view is valid
        // for the entire duration of write_impl.
    }
};

// Policy: "write using the given format to the terminal"
template<typename Sink>
struct TerminalPolicy
    : PolicyBase<TerminalPolicy<Sink>, Sink>
{
    using base_type = PolicyBase<TerminalPolicy<Sink>, Sink>;
    using view_type = typename base_type::view_type;

    TerminalPolicy() = default;

    // Final I/O step (console).
    //
    // Contract:
    //  - msg is only valid for the duration of this call.
    //  - This function must not store msg beyond its lifetime.
    void write_impl(view_type msg) {
        std::fwrite(msg.data(), 1, msg.size(), stdout);
        std::fflush(stdout); // optional but often useful for tests
    }
};

// Policy: "write using the given format to a file"
template<typename Sink>
struct FilePolicy
    : PolicyBase<FilePolicy<Sink>, Sink>
{
    using base_type = PolicyBase<FilePolicy<Sink>, Sink>;
    using view_type = typename base_type::view_type;

    // Sink is a pure policy type, so we do not need a Sink instance here.
    explicit FilePolicy(std::string path ="PublisherFile.log")
        : base_type{}
        , file_{std::move(path), std::ios::out | std::ios::app}
    {}

    // Final I/O step (file).
    void write_impl(view_type msg) {
        if (!file_.is_open())
            return;

        file_.write(msg.data(),
                    static_cast<std::streamsize>(msg.size()));
        file_.flush(); // make sure data is written out
    }

private:
    std::ofstream file_;
};

