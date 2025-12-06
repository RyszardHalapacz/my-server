#include <gtest/gtest.h>

#include <string>
#include <string_view>
#include <fstream>

// Adjust the include path to your project layout if needed.
#include "policyPublisher.hpp"

// If TerminalPolicy / FilePolicy live in a namespace, uncomment and adapt:
// using publisher::PolicyBase;
// using publisher::TerminalPolicy;
// using publisher::FilePolicy;

// ---------------------------------------------------------
// Fake sink implementing the "Sink concept" for tests
// ---------------------------------------------------------

struct FakeSink {
    using view_type = std::string_view;

    // Stateless formatting entry point expected by PolicyBase:
    //
    // Contract:
    //  - Takes a lightweight view of the input line.
    //  - Returns a freshly allocated std::string with the formatted content.
    static std::string format(view_type line) {
        return std::string{"FMT:"} + std::string{line};
    }
};

// ---------------------------------------------------------
// Test-only policy that records last written message
// ---------------------------------------------------------

template<typename Sink>
struct RecordingPolicy : PolicyBase<RecordingPolicy<Sink>, Sink> {
    using base_type = PolicyBase<RecordingPolicy<Sink>, Sink>;
    using view_type = typename base_type::view_type;

    // Base is stateless, so default ctor is enough.
    RecordingPolicy() = default;

    std::string last_msg;

    // write_impl is called by PolicyBase::publish(...) after formatting.
    void write_impl(view_type msg) {
        last_msg = std::string{msg};
    }
};

// ---------------------------------------------------------
// Tests for PolicyBase behavior via RecordingPolicy
// ---------------------------------------------------------

TEST(PolicyBaseTest, PublishUsesSinkAndWriteImpl) {
    RecordingPolicy<FakeSink> policy{};   // no sink instance

    std::string_view input{"ABC"};

    policy.publish(input);

    // We expect:
    //  1) FakeSink::format("ABC") -> "FMT:ABC"
    //  2) write_impl receives "FMT:ABC"
    EXPECT_EQ(policy.last_msg, "FMT:ABC");
}

TEST(PolicyBaseTest, PublishWorksWithEmptyInput) {
    RecordingPolicy<FakeSink> policy{};   // no sink instance

    std::string_view input{""};

    policy.publish(input);

    EXPECT_EQ(policy.last_msg, "FMT:");
}

// ---------------------------------------------------------
// Tests for TerminalPolicy (using CaptureStdout)
// ---------------------------------------------------------

TEST(TerminalPolicyTest, WritesFormattedMessageToStdout) {
    // If TerminalPolicy is in a namespace, qualify it here:
    // publisher::TerminalPolicy<FakeSink> policy{};
    TerminalPolicy<FakeSink> policy{};    // no sink instance

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
    const std::string path = "FilePolicyTest.log";

    // Make sure file is clean before the test.
    {
        std::ofstream cleanup(path, std::ios::trunc);
    }

    // If FilePolicy is in a namespace, qualify it here:
    // publisher::FilePolicy<FakeSink> policy{path};
    FilePolicy<FakeSink> policy{path};   // only file path

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
