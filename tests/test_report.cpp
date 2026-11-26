#include <gtest/gtest.h>

#include <string>

#include "logstat/aggregator.hpp"
#include "logstat/report.hpp"

using logstat::Aggregator;

namespace {

Aggregator sample() {
    Aggregator agg;
    agg.consume(
        "1.1.1.1 - - [t] \"GET /users/5 HTTP/1.1\" 200 10 \"-\" \"ua\" 0.020\n"
        "1.1.1.1 - - [t] \"GET /users/6 HTTP/1.1\" 503 10 \"-\" \"ua\" 0.400\n"
        // nginx экранирует кавычку в $request как \x22 — обратная косая черта попадает в отчёт как есть.
        "2.2.2.2 - - [t] \"GET /a\\x22b HTTP/1.1\" 404 0 \"-\" \"ua\" 0.001\n");
    return agg;
}

}  // namespace

TEST(Escape, Json) {
    EXPECT_EQ(logstat::json_escape(R"(a"b\c)"), R"(a\"b\\c)");
    EXPECT_EQ(logstat::json_escape(std::string("x\ny\x01", 4)), "x\\ny\\u0001");
    EXPECT_EQ(logstat::json_escape("\x1b[31m"), "\\u001b[31m");
}

TEST(Escape, PrometheusLabel) {
    EXPECT_EQ(logstat::prom_label_escape("a\"b\\c\nd"), "a\\\"b\\\\c\\nd");
}

TEST(Report, Text) {
    const auto text = logstat::to_text(sample(), {});
    EXPECT_NE(text.find("requests: 3"), std::string::npos);
    EXPECT_NE(text.find("GET /users/:id"), std::string::npos);
    EXPECT_NE(text.find("5xx 1"), std::string::npos);
}

TEST(Report, TextSanitizesControlCharacters) {
    Aggregator agg;
    agg.consume(std::string("1.1.1.1 - - [t] \"GET /\x1b[2Jx HTTP/1.1\" 200 1 \"-\" \"ua\" 0.1\n"));
    const auto text = logstat::to_text(agg, {});
    EXPECT_EQ(text.find('\x1b'), std::string::npos);
    EXPECT_NE(text.find("\\x1b[2Jx"), std::string::npos);
}

TEST(Report, TextWithoutLatency) {
    Aggregator agg;
    agg.consume("1.1.1.1 - - [t] \"GET / HTTP/1.1\" 200 1 \"-\" \"ua\"\n");
    EXPECT_NE(logstat::to_text(agg, {}).find("n/a"), std::string::npos);
    EXPECT_NE(logstat::to_json(agg, {}).find("\"latency_ms\":null"), std::string::npos);
}

TEST(Report, Json) {
    const auto json = logstat::to_json(sample(), {});
    EXPECT_EQ(json.rfind("{\"requests\":3,\"malformed\":0", 0), 0U);
    EXPECT_NE(json.find(R"("path":"/users/:id")"), std::string::npos);
    EXPECT_NE(json.find(R"("path":"/a\\x22b")"), std::string::npos);  // обратная черта экранирована
    EXPECT_NE(json.find(R"("5xx":1)"), std::string::npos);
}

TEST(Report, Prometheus) {
    const auto prom = logstat::to_prometheus(sample(), {});
    EXPECT_NE(prom.find("logstat_requests_total{status_class=\"5xx\"} 1"), std::string::npos);
    EXPECT_NE(prom.find("logstat_request_duration_seconds_count 3"), std::string::npos);
    EXPECT_NE(prom.find("logstat_endpoint_requests_total{method=\"GET\",path=\"/users/:id\"} 2"), std::string::npos);
    EXPECT_NE(prom.find(R"(path="/a\\x22b")"), std::string::npos);
}

TEST(Report, TopLimitsCardinality) {
    const auto prom = logstat::to_prometheus(sample(), {1});
    EXPECT_EQ(prom.find("/a"), std::string::npos);
}
