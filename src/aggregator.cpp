#include "logstat/aggregator.hpp"

#include <algorithm>

#include "logstat/normalize.hpp"

namespace logstat {

void EndpointStats::merge(const EndpointStats& other) {
    requests += other.requests;
    server_errors += other.server_errors;
    bytes += other.bytes;
    latency.merge(other.latency);
}

template <typename Map>
typename Map::mapped_type& Aggregator::slot(Map& map, std::string_view key, std::size_t limit) {
    // Поиск по string_view требует C++20 (heterogeneous lookup), поэтому ключ копируется
    // в переиспользуемый буфер — без аллокации на каждой строке.
    key_buf_.assign(key);
    if (auto it = map.find(key_buf_); it != map.end()) {
        return it->second;
    }
    if (map.size() >= limit) {
        key_buf_.assign(kOtherKey);
    }
    return map[key_buf_];
}

void Aggregator::add(const LogEntry& entry) {
    ++requests_;
    bytes_ += entry.bytes;
    const int cls = entry.status / 100;
    if (cls >= 1 && cls <= 5) {
        ++status_classes_[static_cast<std::size_t>(cls)];
    }

    normalize_path(entry.target, path_buf_);
    path_buf_.insert(0, " ");
    path_buf_.insert(0, entry.method.empty() ? std::string_view{"-"} : entry.method);
    auto& ep = slot(endpoints_, path_buf_, limits_.max_endpoints);
    ++ep.requests;
    ep.bytes += entry.bytes;
    if (cls == 5) {
        ++ep.server_errors;
    }
    if (entry.request_time_us) {
        latency_.record(*entry.request_time_us);
        ep.latency.record(*entry.request_time_us);
    }

    ++slot(clients_, entry.remote_addr, limits_.max_clients);
}

void Aggregator::consume(std::string_view buffer) {
    std::size_t pos = 0;
    while (pos < buffer.size()) {
        auto end = buffer.find('\n', pos);
        if (end == std::string_view::npos) {
            end = buffer.size();
        }
        const auto line = buffer.substr(pos, end - pos);
        if (!line.empty() && line != "\r") {
            if (const auto entry = parse_line(line)) {
                add(*entry);
            } else {
                add_malformed();
            }
        }
        pos = end + 1;
    }
}

void Aggregator::merge(const Aggregator& other) {
    requests_ += other.requests_;
    malformed_ += other.malformed_;
    bytes_ += other.bytes_;
    for (std::size_t i = 0; i < status_classes_.size(); ++i) {
        status_classes_[i] += other.status_classes_[i];
    }
    latency_.merge(other.latency_);
    for (const auto& [key, stats] : other.endpoints_) {
        slot(endpoints_, key, limits_.max_endpoints).merge(stats);
    }
    for (const auto& [key, count] : other.clients_) {
        slot(clients_, key, limits_.max_clients) += count;
    }
}

double Aggregator::error_rate() const noexcept {
    return requests_ ? static_cast<double>(server_errors()) / static_cast<double>(requests_) : 0.0;
}

std::vector<NamedEndpoint> Aggregator::top_endpoints(std::size_t n) const {
    std::vector<NamedEndpoint> all;
    all.reserve(endpoints_.size());
    for (const auto& [key, stats] : endpoints_) {
        all.push_back({key, &stats});
    }
    const auto by_requests = [](const NamedEndpoint& a, const NamedEndpoint& b) {
        return a.stats->requests != b.stats->requests ? a.stats->requests > b.stats->requests : a.key < b.key;
    };
    n = std::min(n, all.size());
    std::partial_sort(all.begin(), all.begin() + static_cast<std::ptrdiff_t>(n), all.end(), by_requests);
    all.resize(n);
    return all;
}

std::vector<std::pair<std::string, std::uint64_t>> Aggregator::top_clients(std::size_t n) const {
    std::vector<std::pair<std::string, std::uint64_t>> all(clients_.begin(), clients_.end());
    const auto by_count = [](const auto& a, const auto& b) {
        return a.second != b.second ? a.second > b.second : a.first < b.first;
    };
    n = std::min(n, all.size());
    std::partial_sort(all.begin(), all.begin() + static_cast<std::ptrdiff_t>(n), all.end(), by_count);
    all.resize(n);
    return all;
}

}  // namespace logstat
