#include <string>
#include <string_view>
#include <fstream>
#include <cstdio>
#include <utility>

///////////////////////////////////////////////
// PolicyBase + concrete policies
///////////////////////////////////////////////

template<typename Derived, typename Sink>
struct PolicyBase {
    using sink_type = Sink;
    using view_type = std::string_view;

    explicit PolicyBase(Sink sink)
        : sink_{std::move(sink)} {}

    void publish(view_type line) {
        // 1) sink formats the input into a std::string (local buffer)
        std::string msg = sink_.format(line);

        // 2) pass a string_view pointing to that buffer into write_impl
        view_type msg_view{msg};
        static_cast<Derived*>(this)->write_impl(msg_view);
        // msg lives until the end of this function, so msg_view is valid inside write_impl
    }

protected:
    Sink sink_;
};

// Policy: "write using the given format to the terminal"
template<typename Sink>
struct TerminalPolicy
    : PolicyBase<TerminalPolicy<Sink>, Sink>
{
    using base_type = PolicyBase<TerminalPolicy<Sink>, Sink>;
    using base_type::base_type; // inherit constructors

    void write_impl(std::string_view msg) {
        std::fwrite(msg.data(), 1, msg.size(), stdout);
        std::fflush(stdout); // optional
    }
};

// Policy: "write using the given format to a file"
template<typename Sink>
struct FilePolicy
    : PolicyBase<FilePolicy<Sink>, Sink>
{
    using base_type = PolicyBase<FilePolicy<Sink>, Sink>;
    using base_type::base_type;

    explicit FilePolicy(Sink sink, std::string path)
        : base_type{std::move(sink)}
        , file_{std::move(path), std::ios::out | std::ios::app}
    {}

    void write_impl(std::string_view msg) {
        if (!file_.is_open()) return;
        file_.write(msg.data(),
                    static_cast<std::streamsize>(msg.size()));
        file_.flush(); // make sure data is written out
    }

private:
    std::ofstream file_;
};
