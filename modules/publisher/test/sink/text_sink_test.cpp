#include <gtest/gtest.h>
#include "publisher/sink_publisher.hpp"

TEST(TextSinkTest, PassthroughWhenNoTokens) {
    auto result = TextSink::format("hello world");
    EXPECT_EQ(result, "hello world");
}

TEST(TextSinkTest, FormatsEmptyString) {
    auto result = TextSink::format("");
    EXPECT_EQ(result, "");
}

TEST(TextSinkTest, ResolvesClassId) {
    auto result = TextSink::format("class_id=0 rest");
    EXPECT_NE(result.find("class_id=Server"), std::string::npos);
    EXPECT_EQ(result.find("class_id=0"), std::string::npos);
}

TEST(TextSinkTest, ResolvesMethodId) {
    auto result = TextSink::format("method_id=0 rest");
    EXPECT_NE(result.find("method_id=AddEvent"), std::string::npos);
    EXPECT_EQ(result.find("method_id=0"), std::string::npos);
}

TEST(TextSinkTest, FormatsTimestamp) {
    auto result = TextSink::format("timestamp=1000000 rest");
    EXPECT_NE(result.find("1970"), std::string::npos);
    EXPECT_NE(result.find("000000"), std::string::npos);
    EXPECT_EQ(result.find("timestamp=1000000"), std::string::npos);
}

TEST(TextSinkTest, HandlesMultipleTokensInLine) {
    auto result = TextSink::format(
        "severity=Info timestamp=1000000 class_id=0 method_id=0 end");
    EXPECT_NE(result.find("severity=Info"), std::string::npos);
    EXPECT_NE(result.find("1970"), std::string::npos);
    EXPECT_NE(result.find("class_id=Server"), std::string::npos);
    EXPECT_NE(result.find("method_id=AddEvent"), std::string::npos);
}
