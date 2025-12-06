#include <gtest/gtest.h>

#include <string>
#include <string_view>
#include <fstream>

#include "sinkPublisher.hpp"
#include "policyPublisher.hpp"
#include "publisher.hpp"
#include "common/messages/traits.hpp"

class PublisherFixture : public ::testing::Test {
protected:
    RequestPayload envelope_{};

    inline static int adapter_call_count_;

    void SetUp() override {
        adapter_call_count_ = 0;
        // If RequestPayload needs some initialization for your real system,
        // you can do it here.
    }

    static std::string_view to_view(const RequestPayload&) {
        ++adapter_call_count_;
        return std::string_view{"REQ"};
    }
};

TEST_F(PublisherFixture, TerminalJson_UsesJsonSinkAndWritesToStdout) {
    using Pub = Publisher<TerminalPolicy, JsonSink>;

    testing::internal::CaptureStdout();

    Pub::publish(envelope_, &PublisherFixture::to_view);

    std::string output = testing::internal::GetCapturedStdout();

    EXPECT_EQ(adapter_call_count_, 1);
    EXPECT_EQ(output, "JsonTestREQ");
}

TEST_F(PublisherFixture, TerminalText_UsesTextSinkAndWritesToStdout) {
    using Pub = Publisher<TerminalPolicy, TextSink>;

    testing::internal::CaptureStdout();

    Pub::publish(envelope_, &PublisherFixture::to_view);

    std::string output = testing::internal::GetCapturedStdout();

    EXPECT_EQ(adapter_call_count_, 1);
    EXPECT_EQ(output, "REQ");
}

TEST_F(PublisherFixture, FileJson_PublishesFormattedMessageToLocalFile) {
    using Pub = Publisher<FilePolicy, JsonSink>;

    const std::string path = "PublisherFile.log";   // <--- tu

    {
        std::ofstream cleanup(path, std::ios::trunc);
    }

    Pub::publish(envelope_, &PublisherFixture::to_view);

    EXPECT_EQ(adapter_call_count_, 1);

    std::ifstream in(path);
    ASSERT_TRUE(in.is_open());

    std::string file_content;
    std::getline(in, file_content);

    EXPECT_EQ(file_content, "JsonTestREQ");
}

TEST_F(PublisherFixture, FileText_PublishesFormattedMessageToLocalFile) {
    using Pub = Publisher<FilePolicy, TextSink>;

    const std::string path = "PublisherFile.log";   // <--- i tu

    {
        std::ofstream cleanup(path, std::ios::trunc);
    }

    Pub::publish(envelope_, &PublisherFixture::to_view);

    EXPECT_EQ(adapter_call_count_, 1);

    std::ifstream in(path);
    ASSERT_TRUE(in.is_open());

    std::string file_content;
    std::getline(in, file_content);

    EXPECT_EQ(file_content, "REQ");
}
