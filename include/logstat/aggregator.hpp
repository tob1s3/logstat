// Накопление статистики. Каждый поток пишет в свой Aggregator, в конце они сливаются —
// без блокировок на горячем пути.
#pragma once

#include <array>
#include <cstdint>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

#include "logstat/histogram.hpp"
#include "logstat/parser.hpp"

namespace logstat {

struct Limits {
    // Защита от логов, где каждая строка — новый путь (сканеры, атаки перебором).
    // Сверх лимита всё попадает в одну запись "(other)", память не растёт.
    std::size_t max_endpoints = 2000;
    std::size_t max_clients = 100000;
};

struct EndpointStats {
    std::uint64_t requests = 0;
    std::uint64_t server_errors = 0;  // 5xx
    std::uint64_t bytes = 0;
    LatencyHistogram latency;

    void merge(const EndpointStats& other);
};

struct NamedEndpoint {
    std::string key;  // "GET /users/:id"
    const EndpointStats* stats = nullptr;
};

inline constexpr std::string_view kOtherKey = "(other)";

class Aggregator {
public:
    explicit Aggregator(Limits limits = {}) : limits_(limits) {}

    void add(const LogEntry& entry);
    void add_malformed() noexcept { ++malformed_; }
    // Разбирает буфер построчно: удобно и для одного потока, и для куска файла в потоке.
    void consume(std::string_view buffer);
    void merge(const Aggregator& other);

    std::uint64_t requests() const noexcept { return requests_; }
    std::uint64_t malformed() const noexcept { return malformed_; }
    std::uint64_t bytes() const noexcept { return bytes_; }
    // Индекс — первая цифра статуса: [1] = 1xx ... [5] = 5xx; [0] не используется.
    const std::array<std::uint64_t, 6>& status_classes() const noexcept { return status_classes_; }
    std::uint64_t server_errors() const noexcept { return status_classes_[5]; }
    double error_rate() const noexcept;
    const LatencyHistogram& latency() const noexcept { return latency_; }
    std::size_t endpoint_count() const noexcept { return endpoints_.size(); }

    // Топ-N эндпоинтов, по убыванию числа запросов (при равенстве — по ключу, чтобы вывод был стабильным).
    std::vector<NamedEndpoint> top_endpoints(std::size_t n) const;
    std::vector<std::pair<std::string, std::uint64_t>> top_clients(std::size_t n) const;

private:
    template <typename Map>
    typename Map::mapped_type& slot(Map& map, std::string_view key, std::size_t limit);

    Limits limits_;
    std::uint64_t requests_ = 0;
    std::uint64_t malformed_ = 0;
    std::uint64_t bytes_ = 0;
    std::array<std::uint64_t, 6> status_classes_{};
    LatencyHistogram latency_;
    std::unordered_map<std::string, EndpointStats> endpoints_;
    std::unordered_map<std::string, std::uint64_t> clients_;
    std::string key_buf_;
    std::string path_buf_;
};

}  // namespace logstat
