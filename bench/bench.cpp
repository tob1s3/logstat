// Бенчмарк пропускной способности на синтетическом логе.
//   logstat_bench [LINES]             — замер для 1 потока и для всех ядер
//   logstat_bench --generate LINES    — вывести синтетический лог в stdout (для демонстрации)
#include <chrono>
#include <cstdlib>
#include <iostream>
#include <random>
#include <string>
#include <thread>

#include "logstat/analyze.hpp"

namespace {

std::string generate(std::size_t lines) {
    std::mt19937_64 rng(2026);
    std::lognormal_distribution<double> latency(std::log(0.030), 1.0);
    const char* methods[] = {"GET", "GET", "GET", "POST", "PUT", "DELETE"};
    const char* paths[] = {"/api/users/", "/api/orders/", "/api/products/", "/api/cart", "/health", "/static/app.js"};
    const char* agents[] = {"Mozilla/5.0 (X11; Linux x86_64)", "okhttp/4.12.0", "curl/8.5.0", "kube-probe/1.31"};
    std::string out;
    out.reserve(lines * 130);
    char rt[32];
    for (std::size_t i = 0; i < lines; ++i) {
        const auto r = rng();
        const auto* path = paths[r % 6];
        const int status = r % 97 == 0 ? 502 : (r % 13 == 0 ? 404 : 200);
        std::snprintf(rt, sizeof rt, "%.3f", latency(rng));
        out += "10.0.";
        out += std::to_string((r >> 8) % 32);
        out += '.';
        out += std::to_string((r >> 16) % 250);
        out += " - - [26/Nov/2026:10:00:00 +0300] \"";
        out += methods[(r >> 24) % 6];
        out += ' ';
        out += path;
        if (path[std::char_traits<char>::length(path) - 1] == '/') {
            out += std::to_string((r >> 32) % 100000);
        }
        out += " HTTP/1.1\" ";
        out += std::to_string(status);
        out += ' ';
        out += std::to_string((r >> 40) % 20000);
        out += " \"-\" \"";
        out += agents[(r >> 48) % 4];
        out += "\" ";
        out += rt;
        out += '\n';
    }
    return out;
}

void run(const std::string& log, unsigned threads) {
    const auto start = std::chrono::steady_clock::now();
    const auto agg = logstat::analyze(log, threads);
    const std::chrono::duration<double> elapsed = std::chrono::steady_clock::now() - start;
    const double secs = elapsed.count();
    std::cout << "threads=" << threads << "  lines=" << agg.requests() << "  time=" << secs << " s  "
              << static_cast<double>(agg.requests()) / secs / 1e6 << " M lines/s  "
              << static_cast<double>(log.size()) / secs / (1 << 20)
              << " MiB/s  p99=" << static_cast<double>(agg.latency().percentile(0.99)) / 1000.0 << " ms\n";
}

}  // namespace

int main(int argc, char** argv) {
    if (argc == 3 && std::string(argv[1]) == "--generate") {
        std::cout << generate(std::strtoull(argv[2], nullptr, 10));
        return 0;
    }
    const std::size_t lines = argc > 1 ? std::strtoull(argv[1], nullptr, 10) : 2'000'000;
    const auto log = generate(lines);
    std::cout << "synthetic log: " << lines << " lines, " << log.size() / (1 << 20) << " MiB\n";
    run(log, 1);
    const unsigned hw = std::max(1U, std::thread::hardware_concurrency());
    if (hw > 1) {
        run(log, hw);
    }
    return 0;
}
