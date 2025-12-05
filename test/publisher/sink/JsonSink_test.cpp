#include <gtest/gtest.h>
#include <string>
#include <string_view>
#include "sinkPublisher.hpp"

// Include your real header here, e.g.:
// #include "logger/core/sink.hpp"

// For clarity in the example we assume JsonSink is in the global namespace.

TEST(JsonSinkTest, FormatsLiteralWithPrefix) {
    JsonSink sink{};
    std::string_view input{"ABC"};

    // Call the generic interface provided by SinkBase.
    auto result = sink.format(input);

    // Expect the prefix "JsonTest" followed by the original content.
    EXPECT_EQ(result, "JsonTestABC");
}

TEST(JsonSinkTest, FormatsEmptyString) {
    JsonSink sink{};
    std::string_view input{""};

    // An empty input should still return just the prefix.
    auto result = sink.format(input);

    EXPECT_EQ(result, "JsonTest");
}

TEST(JsonSinkTest, AcceptsViewFromStdString) {
    JsonSink sink{};
    std::string tmp = "payload";

    // Simulate a typical usage pattern:
    // we build a std::string, then pass a string_view into the sink.
    std::string_view view{tmp};

    auto result = sink.format(view);

    // The sink should not care about the original storage,
    // only about the characters visible through std::string_view.
    EXPECT_EQ(result, "JsonTestpayload");
}
