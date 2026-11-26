#include <gtest/gtest.h>

#include <random>
#include <string>

#include "logstat/aggregator.hpp"
#include "logstat/analyze.hpp"

using logstat::Aggregator;
using logstat::analyze;
using logstat::Limits;

namespace {

std::string line(const std::string& ip, const std::string& method, const std::string& path, int status,
                 const std::string& rt) {
    return ip + " - - [26/Nov/2026:10:00:00 +0300] \"" + method + " " + path + " HTTP/1.1\" " + std::to_string(status) +
           " 100 \"-\" \"test\" " + rt + "\n";
}

std::string synthetic_log(std::size_t n) {
    std::mt19937 rng(7);
    std::string out;
    // Все пути заканчиваются числовым сегментом → нормализуются, и лимит эндпоинтов не достигается.
    const char* paths[] = {"/api/users/", "/api/orders/", "/api/items/", "/static/v/"};
    for (std::size_t i = 0; i < n; ++i) {
        const auto p = std::string(paths[rng() % 4]) + std::to_string(rng() % 1000);
        const int status = rng() % 50 == 0 ? 503 : 200;
        const auto rt = "0." + std::to_string(100 + rng() % 900);
        out += line("10.0.0." + std::to_string(rng() % 20), i % 3 ? "GET" : "POST", p, status, rt);
        if (i % 1000 == 0) {
            out += "broken line\n";
        }
    }
    return out;
}

}  // namespace

TEST(Aggregator, CountsAndEndpoints) {
    Aggregator agg;
    agg.consume(line("1.1.1.1", "GET", "/users/1", 200, "0.010") + line("1.1.1.1", "GET", "/users/2", 500, "0.030") +
                line("2.2.2.2", "POST", "/users", 201, "0.020") + "garbage\n\n\r\n");
    EXPECT_EQ(agg.requests(), 3U);
    EXPECT_EQ(agg.malformed(), 1U);  // пустые строки не считаются ошибкой
    EXPECT_EQ(agg.server_errors(), 1U);
    EXPECT_NEAR(agg.error_rate(), 1.0 / 3.0, 1e-9);
    EXPECT_EQ(agg.bytes(), 300U);

    const auto top = agg.top_endpoints(10);
    ASSERT_EQ(top.size(), 2U);
    EXPECT_EQ(top[0].key, "GET /users/:id");
    EXPECT_EQ(top[0].stats->requests, 2U);
    EXPECT_EQ(top[0].stats->server_errors, 1U);
    EXPECT_EQ(top[1].key, "POST /users");

    const auto clients = agg.top_clients(1);
    ASSERT_EQ(clients.size(), 1U);
    EXPECT_EQ(clients[0].first, "1.1.1.1");
    EXPECT_EQ(clients[0].second, 2U);
}

TEST(Aggregator, LastLineWithoutNewline) {
    Aggregator agg;
    auto text = line("1.1.1.1", "GET", "/", 200, "0.001");
    text.pop_back();
    agg.consume(text);
    EXPECT_EQ(agg.requests(), 1U);
}

TEST(Aggregator, EndpointLimitProtectsMemory) {
    Aggregator agg(Limits{3, 2});
    for (int i = 0; i < 100; ++i) {
        // Слова, а не числа — нормализация их не схлопнет: так выглядит перебор путей сканером.
        agg.consume(line("10.0.0." + std::to_string(i), "GET",
                         "/probe" + std::string(static_cast<std::size_t>(i % 26 + 1), 'x'), 404, "0.001"));
    }
    EXPECT_EQ(agg.requests(), 100U);
    EXPECT_LE(agg.endpoint_count(), 4U);  // 3 + "(other)"
    std::uint64_t total = 0;
    for (const auto& ep : agg.top_endpoints(100)) {
        total += ep.stats->requests;
    }
    EXPECT_EQ(total, 100U);  // ни один запрос не потерян
    EXPECT_LE(agg.top_clients(100).size(), 3U);
}

TEST(Analyze, ResultDoesNotDependOnThreads) {
    const auto log = synthetic_log(60000);  // ~6 МБ: хватает на несколько кусков
    const auto one = analyze(log, 1);
    for (const unsigned threads : {2U, 3U, 8U}) {
        const auto many = analyze(log, threads);
        EXPECT_EQ(many.requests(), one.requests()) << threads;
        EXPECT_EQ(many.malformed(), one.malformed()) << threads;
        EXPECT_EQ(many.server_errors(), one.server_errors()) << threads;
        EXPECT_EQ(many.latency().percentile(0.99), one.latency().percentile(0.99)) << threads;
        const auto a = one.top_endpoints(5);
        const auto b = many.top_endpoints(5);
        ASSERT_EQ(a.size(), b.size());
        for (std::size_t i = 0; i < a.size(); ++i) {
            EXPECT_EQ(a[i].key, b[i].key);
            EXPECT_EQ(a[i].stats->requests, b[i].stats->requests);
            EXPECT_EQ(a[i].stats->latency.percentile(0.5), b[i].stats->latency.percentile(0.5));
        }
    }
    EXPECT_EQ(one.requests(), 60000U);
    EXPECT_EQ(one.malformed(), 60U);
}

TEST(Analyze, EmptyAndTinyInputs) {
    EXPECT_EQ(analyze("", 4).requests(), 0U);
    EXPECT_EQ(analyze("\n\n", 4).requests(), 0U);
    EXPECT_EQ(analyze(line("1.1.1.1", "GET", "/", 200, "0.1"), 0).requests(), 1U);
}
