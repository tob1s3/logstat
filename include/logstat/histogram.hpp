// Гистограмма задержек с ограниченной относительной ошибкой (лог-линейные корзины, как в HdrHistogram).
//
// Значения до 64 мкс хранятся точно. Дальше каждая степень двойки делится на 64 корзины,
// поэтому ошибка перцентиля не больше 1/64 ≈ 1,6 % при любом масштабе: и для 2 мс, и для 20 с.
// Память растёт по мере появления больших значений: для задержек до 10 с — около 9 КБ.
// Слияние точное: перцентили по объединённой гистограмме не зависят от числа потоков.
#pragma once

#include <cstdint>
#include <vector>

namespace logstat {

class LatencyHistogram {
public:
    static constexpr unsigned kSubBucketBits = 6;
    static constexpr std::uint64_t kSubBuckets = 1ULL << kSubBucketBits;
    static constexpr std::uint64_t kMaxValue = (1ULL << 40) - 1;  // ~12,7 суток в микросекундах

    void record(std::uint64_t value_us) noexcept;
    void merge(const LatencyHistogram& other);

    // q в диапазоне [0, 1]. Для пустой гистограммы — 0.
    std::uint64_t percentile(double q) const noexcept;

    std::uint64_t count() const noexcept { return count_; }
    std::uint64_t min() const noexcept { return count_ ? min_ : 0; }
    std::uint64_t max() const noexcept { return max_; }
    double sum_seconds() const noexcept { return static_cast<double>(sum_us_) / 1e6; }
    double mean() const noexcept { return count_ ? static_cast<double>(sum_us_) / static_cast<double>(count_) : 0.0; }

    static std::size_t bucket_index(std::uint64_t value) noexcept;
    static std::uint64_t bucket_lower(std::size_t index) noexcept;
    static std::uint64_t bucket_width(std::size_t index) noexcept;

private:
    std::vector<std::uint64_t> buckets_;
    std::uint64_t count_ = 0;
    std::uint64_t sum_us_ = 0;
    std::uint64_t min_ = 0;
    std::uint64_t max_ = 0;
};

}  // namespace logstat
