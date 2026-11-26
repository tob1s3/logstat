#include "logstat/histogram.hpp"

#include <algorithm>
#include <cmath>
#include <limits>

namespace logstat {
namespace {

unsigned msb(std::uint64_t v) noexcept {
    unsigned r = 0;
    while (v >>= 1U) {
        ++r;
    }
    return r;
}

}  // namespace

// Корзины 0..63 — точные значения 0..63.
// Для v >= 64: e = msb(v) - 6, мантисса m = v >> e лежит в [64, 128),
// индекс = 64 * (e + 1) + (m - 64), корзина покрывает [m << e, (m + 1) << e).
std::size_t LatencyHistogram::bucket_index(std::uint64_t value) noexcept {
    if (value < kSubBuckets) {
        return static_cast<std::size_t>(value);
    }
    const unsigned e = msb(value) - kSubBucketBits;
    const std::uint64_t m = value >> e;
    return static_cast<std::size_t>(kSubBuckets * (e + 1) + (m - kSubBuckets));
}

std::uint64_t LatencyHistogram::bucket_lower(std::size_t index) noexcept {
    if (index < kSubBuckets) {
        return index;
    }
    const std::uint64_t e = index / kSubBuckets - 1;
    const std::uint64_t m = kSubBuckets + index % kSubBuckets;
    return m << e;
}

std::uint64_t LatencyHistogram::bucket_width(std::size_t index) noexcept {
    return index < kSubBuckets ? 1 : 1ULL << (index / kSubBuckets - 1);
}

void LatencyHistogram::record(std::uint64_t value_us) noexcept {
    const std::uint64_t v = std::min(value_us, kMaxValue);
    const std::size_t idx = bucket_index(v);
    if (idx >= buckets_.size()) {
        buckets_.resize(idx + 1, 0);
    }
    ++buckets_[idx];
    min_ = count_ ? std::min(min_, v) : v;
    max_ = std::max(max_, v);
    ++count_;
    // Переполнение суммы потребовало бы ~5·10^6 лет суммарной задержки — не проверяем.
    sum_us_ += v;
}

void LatencyHistogram::merge(const LatencyHistogram& other) {
    if (other.count_ == 0) {
        return;
    }
    if (other.buckets_.size() > buckets_.size()) {
        buckets_.resize(other.buckets_.size(), 0);
    }
    for (std::size_t i = 0; i < other.buckets_.size(); ++i) {
        buckets_[i] += other.buckets_[i];
    }
    min_ = count_ ? std::min(min_, other.min_) : other.min_;
    max_ = std::max(max_, other.max_);
    count_ += other.count_;
    sum_us_ += other.sum_us_;
}

std::uint64_t LatencyHistogram::percentile(double q) const noexcept {
    if (count_ == 0) {
        return 0;
    }
    q = std::clamp(q, 0.0, 1.0);
    // Ранг по определению nearest-rank: наименьшее значение, не меньше которого q·N наблюдений.
    auto rank = static_cast<std::uint64_t>(std::ceil(q * static_cast<double>(count_)));
    rank = std::clamp<std::uint64_t>(rank, 1, count_);
    std::uint64_t seen = 0;
    for (std::size_t i = 0; i < buckets_.size(); ++i) {
        seen += buckets_[i];
        if (seen >= rank) {
            const std::uint64_t mid = bucket_lower(i) + (bucket_width(i) - 1) / 2;
            return std::clamp(mid, min_, max_);
        }
    }
    return max_;
}

}  // namespace logstat
