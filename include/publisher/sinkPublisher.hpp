///////////////////////////////////////
// SinkBase + concrete sinks
///////////////////////////////////////
#pragma once
#include <string>
#include <string_view>
#include <sstream>
#include <iomanip>
#include <chrono>
#include <ctime>

#include "logger/log_names.hpp"  // className(), methodName()
#include "common/log_ids.hpp"    // LogClassId, MethodId

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

    static std::string format_impl(view_type line) {
        return "JsonTest" + std::string(line);
    }
};

// TEXT sink
struct TextSink : SinkBase<TextSink> {
    using view_type = std::string_view;

    static std::string format_impl(view_type line) {
        std::string result;
        result.reserve(line.size() + 100);
        
        size_t pos = 0;
        
        while (pos < line.size()) {
            // Szukaj timestamp=
            auto ts_pos = line.find("timestamp=", pos);
            if (ts_pos != std::string_view::npos) {
                result.append(line.substr(pos, ts_pos - pos));
                result.append("timestamp=");
                
                auto num_start = ts_pos + 10;
                auto num_end = line.find(' ', num_start);
                if (num_end == std::string_view::npos) num_end = line.size();
                
                auto ts_str = line.substr(num_start, num_end - num_start);
                std::uint64_t us = std::stoull(std::string(ts_str));
                result.append(format_timestamp_us(us));
                
                pos = num_end;
                continue;
            }
            
            // Szukaj class_id=
            auto class_pos = line.find("class_id=", pos);
            if (class_pos != std::string_view::npos && (ts_pos == std::string_view::npos || class_pos < ts_pos)) {
                result.append(line.substr(pos, class_pos - pos));
                result.append("class_id=");
                
                auto num_start = class_pos + 9;
                auto num_end = line.find(' ', num_start);
                if (num_end == std::string_view::npos) num_end = line.size();
                
                auto id_str = line.substr(num_start, num_end - num_start);
                auto id = static_cast<LogClassId>(std::stoi(std::string(id_str)));
                result.append(className(id));
                
                pos = num_end;
                continue;
            }
            
            // Szukaj method_id=
            auto method_pos = line.find("method_id=", pos);
            if (method_pos != std::string_view::npos && 
                (ts_pos == std::string_view::npos || method_pos < ts_pos) &&
                (class_pos == std::string_view::npos || method_pos < class_pos)) {
                result.append(line.substr(pos, method_pos - pos));
                result.append("method_id=");
                
                auto num_start = method_pos + 10;
                auto num_end = line.find(' ', num_start);
                if (num_end == std::string_view::npos) num_end = line.size();
                
                auto id_str = line.substr(num_start, num_end - num_start);
                auto id = static_cast<MethodId>(std::stoi(std::string(id_str)));
                result.append(methodName(id));
                
                pos = num_end;
                continue;
            }
            
            // Nic nie znaleziono - przepisz resztę
            result.append(line.substr(pos));
            break;
        }
        
        return result;
    }

private:
    static std::string format_timestamp_us(std::uint64_t us_since_epoch) {
        using namespace std::chrono;
        
        auto dur = microseconds{us_since_epoch};
        auto tp = system_clock::time_point{duration_cast<system_clock::duration>(dur)};
        auto time_t_tp = system_clock::to_time_t(tp);
        
        auto us = us_since_epoch % 1'000'000;
        
        std::ostringstream oss;
        oss << std::put_time(std::localtime(&time_t_tp), "%Y-%m-%d %H:%M:%S")
            << '.' << std::setfill('0') << std::setw(6) << us;
        
        return oss.str();
    }
};
