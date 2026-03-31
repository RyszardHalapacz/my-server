///////////////////////////////////////
// SinkBase + concrete sinks
///////////////////////////////////////
#pragma once
#include <string>
#include <string_view>

#include "logger/log_names.hpp"
#include "common/log_ids.hpp"

template<typename Derived>
struct SinkBase {
    using view_type = std::string_view;

    static std::string format(view_type line) {
        return Derived::format_impl(line);
    }
};

// JSON sink
struct JsonSink : SinkBase<JsonSink> {
    using view_type = std::string_view;
    static std::string format_impl(view_type line);
};

// TEXT sink
struct TextSink : SinkBase<TextSink> {
    using view_type = std::string_view;
    static std::string format_impl(view_type line);
private:
    static std::string format_timestamp_us(std::uint64_t us_since_epoch);
};
