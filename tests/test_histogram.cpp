#include <gtest/gtest.h>

#include <algorithm>
#include <cmath>
#include <random>
#include <vector>

#include "logstat/histogram.hpp"

using logstat::LatencyHistogram;

TEST(Histogram, Empty) {
    const LatencyHistogram h;
    EXPECT_EQ(h.count(), 0U);
    EXPECT_EQ(h.percentile(0.99), 0U);
    EXPECT_EQ(h.min(), 0U);
    EXPECT_DOUBLE_EQ(h.mean(), 0.0);
}

TEST(Histogram, SmallValuesAreExact) {
    LatencyHistogram h;
    for (std::uint64_t v = 0; v < 64; ++v) {
        h.record(v);
    }
    EXPECT_EQ(h.percentile(0.0), 0U);
    EXPECT_EQ(h.percentile(0.5), 31U);
    EXPECT_EQ(h.percentile(1.0), 63U);
}

TEST(Histogram, BucketsAreContiguous) {
    // Каждая корзина начинается там, где закончилась предыдущая, и индекс значения попадает в свою корзину.
    for (std::size_t i = 1; i < 64 * 30; ++i) {
        ASSERT_EQ(LatencyHistogram::bucket_lower(i),
                  LatencyHistogram::bucket_lower(i - 1) + LatencyHistogram::bucket_width(i - 1))
            << i;
    }
    for (std::uint64_t v : {0ULL, 63ULL, 64ULL, 65ULL, 127ULL, 128ULL, 1000ULL, 123456789ULL, (1ULL << 40) - 1}) {
        const auto idx = LatencyHistogram::bucket_index(v);
        EXPECT_LE(LatencyHistogram::bucket_lower(idx), v);
        EXPECT_LT(v, LatencyHistogram::bucket_lower(idx) + LatencyHistogram::bucket_width(idx));
    }
}

TEST(Histogram, PercentilesWithinRelativeError) {
    // Логнормальное распределение похоже на реальные задержки: медиана ~20 мс, длинный хвост.
    std::mt19937_64 rng(42);
    std::lognormal_distribution<double> dist(std::log(20000.0), 1.2);
    LatencyHistogram h;
    std::vector<std::uint64_t> values;
    for (int i = 0; i < 200000; ++i) {
        const auto v = static_cast<std::uint64_t>(dist(rng));
        values.push_back(v);
        h.record(v);
    }
    std::sort(values.begin(), values.end());
    for (const double q : {0.5, 0.9, 0.99, 0.999}) {
        const auto rank = static_cast<std::size_t>(std::ceil(q * static_cast<double>(values.size())));
        const auto exact = static_cast<double>(values[rank - 1]);
        const auto approx = static_cast<double>(h.percentile(q));
        EXPECT_NEAR(approx, exact, exact / 64.0) << "q=" << q;
    }
    EXPECT_EQ(h.min(), values.front());
    EXPECT_EQ(h.max(), values.back());
}

TEST(Histogram, MergeEqualsSingle) {
    LatencyHistogram a;
    LatencyHistogram b;
    LatencyHistogram all;
    for (std::uint64_t v = 1; v < 100000; v += 37) {
        (v % 2 ? a : b).record(v);
        all.record(v);
    }
    a.merge(b);
    a.merge(LatencyHistogram{});
    EXPECT_EQ(a.count(), all.count());
    for (const double q : {0.1, 0.5, 0.99}) {
        EXPECT_EQ(a.percentile(q), all.percentile(q));
    }
    EXPECT_EQ(a.min(), all.min());
    EXPECT_EQ(a.max(), all.max());
    EXPECT_DOUBLE_EQ(a.sum_seconds(), all.sum_seconds());
}

TEST(Histogram, MergeIntoEmpty) {
    LatencyHistogram empty;
    LatencyHistogram h;
    h.record(500);
    empty.merge(h);
    EXPECT_EQ(empty.min(), 500U);
    EXPECT_EQ(empty.count(), 1U);
}

TEST(Histogram, HugeValuesAreClamped) {
    LatencyHistogram h;
    h.record(~0ULL);
    EXPECT_EQ(h.max(), LatencyHistogram::kMaxValue);
    EXPECT_LE(h.percentile(1.0), LatencyHistogram::kMaxValue);
}
