#include <gtest/gtest.h>
#include <string>
#include <string_view>
#include <fstream>
#include "policyPublisher.hpp"

// Include your real header with PolicyBase / TerminalPolicy / FilePolicy.
// #include "publisher/policy/policy.hpp"

// --- FakeSink & RecordingPolicy from above ---

struct FakeSink {
    using view_type = std::string_view;

    std::string format(view_type line) const {
        return std::string{"FMT:"} + std::string{line};
    }
};

template<typename Sink>
struct RecordingPolicy : PolicyBase<RecordingPolicy<Sink>, Sink> {
    using base_type = PolicyBase<RecordingPolicy<Sink>, Sink>;
    using base_type::base_type;

    std::string last_msg;

    void write_impl(std::string_view msg) {
        last_msg = std::string{msg};
    }
};

// ---------------------------------------------------------
// Tests for PolicyBase behavior via RecordingPolicy
// ---------------------------------------------------------

TEST(PolicyBaseTest, PublishUsesSinkAndWriteImpl) {
    FakeSink sink{};
    RecordingPolicy<FakeSink> policy{sink};

    std::string_view input{"ABC"};

    policy.publish(input);

    // We expect:
    // 1) sink.format("ABC") -> "FMT:ABC"
    // 2) write_impl receives "FMT:ABC"
    EXPECT_EQ(policy.last_msg, "FMT:ABC");
}

TEST(PolicyBaseTest, PublishWorksWithEmptyInput) {
    FakeSink sink{};
    RecordingPolicy<FakeSink> policy{sink};

    std::string_view input{""};

    policy.publish(input);

    EXPECT_EQ(policy.last_msg, "FMT:");
}

// ---------------------------------------------------------
// Tests for TerminalPolicy (optional, using CaptureStdout)
// ---------------------------------------------------------

TEST(TerminalPolicyTest, WritesFormattedMessageToStdout) {
    FakeSink sink{};
    TerminalPolicy<FakeSink> policy{sink};

    std::string_view input{"XYZ"};

    // Capture stdout produced by TerminalPolicy.
    testing::internal::CaptureStdout();
    policy.publish(input);
    std::string output = testing::internal::GetCapturedStdout();

    // TerminalPolicy should receive "FMT:XYZ" from the sink
    // and write it directly to stdout.
    EXPECT_EQ(output, "FMT:XYZ");
}

// ---------------------------------------------------------
// Tests for FilePolicy (real file interaction)
// ---------------------------------------------------------

TEST(FilePolicyTest, WritesFormattedMessageToFile) {
    // Use a temporary file name for the test.
    const std::string path = "FilePolicyTest.log";

    // Make sure file is clean before the test.
    {
        std::ofstream cleanup(path, std::ios::trunc);
    }

    FakeSink sink{};
    FilePolicy<FakeSink> policy{sink, path};

    std::string_view input{"DATA"};

    policy.publish(input);

    // Now read back the file and verify its contents.
    std::ifstream in(path);
    ASSERT_TRUE(in.is_open());

    std::string file_content;
    std::getline(in, file_content);

    // We expect the formatted message "FMT:DATA"
    EXPECT_EQ(file_content, "FMT:DATA");
}
