#include <gtest/gtest.h>
#include "server/metrics/metrics.hpp"

using server::metrics::MetricsMixin;

TEST(MetricsMixin, StartsAtZero) {
    MetricsMixin m;
    auto s = m.snapshot(0);
    EXPECT_EQ(s.accepted_total, 0u);
    EXPECT_EQ(s.rejected_full_total, 0u);
    EXPECT_EQ(s.rejected_stopped_total, 0u);
    EXPECT_EQ(s.invalid_total, 0u);
    EXPECT_EQ(s.error_total, 0u);
    EXPECT_EQ(s.inflight_snapshot, 0u);
    EXPECT_EQ(s.queue_depth_snapshot, 0u);
}

TEST(MetricsMixin, OnAcceptedIncrements) {
    MetricsMixin m;
    m.onAccepted();
    m.onAccepted();
    m.onAccepted();
    auto s = m.snapshot(0);
    EXPECT_EQ(s.accepted_total, 3u);
}

TEST(MetricsMixin, OnRejectedFullIncrements) {
    MetricsMixin m;
    m.onRejectedFull();
    EXPECT_EQ(m.snapshot(0).rejected_full_total, 1u);
}

TEST(MetricsMixin, OnRejectedStoppedIncrements) {
    MetricsMixin m;
    m.onRejectedStopped();
    EXPECT_EQ(m.snapshot(0).rejected_stopped_total, 1u);
}

TEST(MetricsMixin, OnInvalidIncrements) {
    MetricsMixin m;
    m.onInvalid();
    EXPECT_EQ(m.snapshot(0).invalid_total, 1u);
}

TEST(MetricsMixin, OnErrorIncrements) {
    MetricsMixin m;
    m.onError();
    EXPECT_EQ(m.snapshot(0).error_total, 1u);
}

TEST(MetricsMixin, InflightTracking) {
    MetricsMixin m;
    m.inflightInc();
    m.inflightInc();
    EXPECT_EQ(m.snapshot(0).inflight_snapshot, 2u);
    m.inflightDec();
    EXPECT_EQ(m.snapshot(0).inflight_snapshot, 1u);
}

TEST(MetricsMixin, SnapshotCapturesQueueDepth) {
    MetricsMixin m;
    auto s = m.snapshot(42);
    EXPECT_EQ(s.queue_depth_snapshot, 42u);
}

TEST(MetricsMixin, AllCountersIndependent) {
    MetricsMixin m;
    m.onAccepted();
    m.onRejectedFull();
    m.onRejectedStopped();
    m.onInvalid();
    m.onError();
    m.inflightInc();
    auto s = m.snapshot(99);
    EXPECT_EQ(s.accepted_total, 1u);
    EXPECT_EQ(s.rejected_full_total, 1u);
    EXPECT_EQ(s.rejected_stopped_total, 1u);
    EXPECT_EQ(s.invalid_total, 1u);
    EXPECT_EQ(s.error_total, 1u);
    EXPECT_EQ(s.inflight_snapshot, 1u);
    EXPECT_EQ(s.queue_depth_snapshot, 99u);
}
