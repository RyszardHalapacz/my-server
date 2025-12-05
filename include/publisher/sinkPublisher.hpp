#include <string>
#include <string_view>
#include <fstream>
#include <cstdio>
#include <utility>

///////////////////////////////////////
// SinkBase + concrete sinks
///////////////////////////////////////

template<typename Derived>
struct SinkBase {
    using view_type = std::string_view;

    std::string format(view_type line) const {
        return static_cast<const Derived*>(this)->format_impl(line);
    }
};

// JSON sink
struct JsonSink : SinkBase<JsonSink> {
    using view_type = std::string_view;

    std::string format_impl(view_type line) const {
        // Example: wrap the line in some JSON structure
        return "JsonTest" + std::string(line);
    }
};

// TEXT sink
struct TextSink : SinkBase<TextSink> {
    using view_type = std::string_view;

    std::string format_impl(view_type line) const {
        // plain text – for example, prepend a header, timestamp, etc.
        // or simply: return std::string{line};
        return std::string(line);
    }
};
