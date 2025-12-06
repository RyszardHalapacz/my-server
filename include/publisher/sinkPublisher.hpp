///////////////////////////////////////
// SinkBase + concrete sinks
///////////////////////////////////////
#pragma once
#include <string>
#include <string_view>

template<typename Derived>
struct SinkBase {
    using view_type = std::string_view;

    // Stateless formatting entry point.
    //
    // Contract for Derived:
    //   static std::string format_impl(view_type line);
    //
    // This keeps the sink as a pure, stateless policy type:
    // no instances, no data members, all dispatch is done by type.
    static std::string format(view_type line) {
        return Derived::format_impl(line);
    }
};

// JSON sink
struct JsonSink : SinkBase<JsonSink> {
    using view_type = std::string_view;

    // Example implementation: wrap the line in some JSON-like structure.
    static std::string format_impl(view_type line) {
        return "JsonTest" + std::string(line);
    }
};

// TEXT sink
struct TextSink : SinkBase<TextSink> {
    using view_type = std::string_view;

    // Plain text formatting – could prepend timestamps, severity, etc.
    static std::string format_impl(view_type line) {
        return std::string(line);
    }
};
