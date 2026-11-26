#include "logstat/analyze.hpp"

#include <algorithm>
#include <thread>
#include <vector>

namespace logstat {
namespace {

// Меньше этого объёма потоки дороже, чем экономия.
constexpr std::size_t kMinChunk = 1U << 20;

}  // namespace

Aggregator analyze(std::string_view buffer, unsigned threads, Limits limits) {
    threads = std::max(1U, threads);
    threads = static_cast<unsigned>(std::min<std::size_t>(threads, buffer.size() / kMinChunk + 1));

    // Границы кусков сдвигаются вперёд до ближайшего '\n', чтобы строка не разрывалась.
    std::vector<std::size_t> bounds{0};
    for (unsigned i = 1; i < threads; ++i) {
        std::size_t pos = buffer.size() / threads * i;
        pos = std::max(pos, bounds.back());
        const auto nl = buffer.find('\n', pos);
        pos = nl == std::string_view::npos ? buffer.size() : nl + 1;
        bounds.push_back(pos);
    }
    bounds.push_back(buffer.size());

    std::vector<Aggregator> parts(bounds.size() - 1, Aggregator(limits));
    std::vector<std::thread> workers;
    workers.reserve(parts.size());
    for (std::size_t i = 0; i < parts.size(); ++i) {
        const auto chunk = buffer.substr(bounds[i], bounds[i + 1] - bounds[i]);
        if (i + 1 == parts.size()) {
            parts[i].consume(chunk);  // последний кусок — в текущем потоке
        } else {
            workers.emplace_back([&part = parts[i], chunk] { part.consume(chunk); });
        }
    }
    for (auto& w : workers) {
        w.join();
    }

    Aggregator result(limits);
    for (const auto& part : parts) {
        result.merge(part);
    }
    return result;
}

}  // namespace logstat
