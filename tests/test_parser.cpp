#include <gtest/gtest.h>

#include "logstat/parser.hpp"

using logstat::parse_line;
using logstat::parse_seconds_us;

namespace {

constexpr std::string_view kTimed =
    R"log(203.0.113.10 - alice [26/Nov/2026:10:00:01 +0300] "GET /api/users/42?x=1 HTTP/1.1" 200 512 "https://ref.example/" "Mozilla/5.0 (X11; Linux)" 0.012)log";

}  // namespace

TEST(Parser, TimedFormat) {
    const auto e = parse_line(kTimed);
    ASSERT_TRUE(e.has_value());
    EXPECT_EQ(e->remote_addr, "203.0.113.10");
    EXPECT_EQ(e->time_local, "26/Nov/2026:10:00:01 +0300");
    EXPECT_EQ(e->method, "GET");
    EXPECT_EQ(e->target, "/api/users/42?x=1");
    EXPECT_EQ(e->protocol, "HTTP/1.1");
    EXPECT_EQ(e->status, 200);
    EXPECT_EQ(e->bytes, 512U);
    ASSERT_TRUE(e->request_time_us.has_value());
    EXPECT_EQ(*e->request_time_us, 12000U);
}

TEST(Parser, CombinedWithoutRequestTime) {
    const auto e = parse_line(R"(::1 - - [01/Jan/2026:00:00:00 +0000] "HEAD / HTTP/1.0" 304 0 "-" "curl/8.0")");
    ASSERT_TRUE(e.has_value());
    EXPECT_EQ(e->remote_addr, "::1");
    EXPECT_EQ(e->status, 304);
    EXPECT_FALSE(e->request_time_us.has_value());
}

TEST(Parser, CommonFormatAndDashBytes) {
    const auto e = parse_line(R"(10.0.0.1 - - [01/Jan/2026:00:00:00 +0000] "GET /x HTTP/1.1" 499 -)");
    ASSERT_TRUE(e.has_value());
    EXPECT_EQ(e->status, 499);
    EXPECT_EQ(e->bytes, 0U);
}

TEST(Parser, ExtraFieldsAfterRequestTime) {
    // Часто после $request_time пишут $upstream_response_time — он может быть "-".
    const auto e = parse_line(R"(1.2.3.4 - - [t] "GET / HTTP/1.1" 200 1 "-" "ua" 0.250 0.248 "-")");
    ASSERT_TRUE(e.has_value());
    EXPECT_EQ(*e->request_time_us, 250000U);
}

TEST(Parser, GarbageRequestLineKeepsStatus) {
    const auto e = parse_line(R"(1.2.3.4 - - [t] "\x16\x03\x01" 400 157 "-" "-" 0.000)");
    ASSERT_TRUE(e.has_value());
    EXPECT_TRUE(e->method.empty());
    EXPECT_EQ(e->target, R"(\x16\x03\x01)");
    EXPECT_EQ(e->status, 400);
}

TEST(Parser, CrLfLineEnding) {
    const auto e = parse_line(R"(1.2.3.4 - - [t] "GET / HTTP/1.1" 200 1 "-" "ua" 0.5)"
                              "\r\n");
    ASSERT_TRUE(e.has_value());
    EXPECT_EQ(*e->request_time_us, 500000U);
}

class ParserRejects : public ::testing::TestWithParam<std::string_view> {};

TEST_P(ParserRejects, Malformed) {
    EXPECT_FALSE(parse_line(GetParam()).has_value());
}

INSTANTIATE_TEST_SUITE_P(Lines, ParserRejects,
                         ::testing::Values("", "garbage", "1.2.3.4 - - [unterminated",
                                           R"(1.2.3.4 - - [t] "GET / HTTP/1.1)",
                                           R"(1.2.3.4 - - [t] "GET / HTTP/1.1" abc 1 "-" "ua")",
                                           R"(1.2.3.4 - - [t] "GET / HTTP/1.1" 2000 1 "-" "ua")",
                                           R"(1.2.3.4 - - [t] "GET / HTTP/1.1" 200 -5 "-" "ua")",
                                           R"(1.2.3.4 - - [t] "GET / HTTP/1.1" 200 1 "-" ua)",
                                           R"(1.2.3.4 - - [t] "GET / HTTP/1.1" 200 1 "-" "ua"x)"));

TEST(Parser, BadRequestTimeIsIgnoredNotFatal) {
    const auto e = parse_line(R"(1.2.3.4 - - [t] "GET / HTTP/1.1" 200 1 "-" "ua" -)");
    ASSERT_TRUE(e.has_value());
    EXPECT_FALSE(e->request_time_us.has_value());
}

TEST(ParseSeconds, Values) {
    EXPECT_EQ(parse_seconds_us("0.000"), 0U);
    EXPECT_EQ(parse_seconds_us("0.001"), 1000U);
    EXPECT_EQ(parse_seconds_us("1.5"), 1500000U);
    EXPECT_EQ(parse_seconds_us("12"), 12000000U);
    EXPECT_EQ(parse_seconds_us("0.000001"), 1U);
}

TEST(ParseSeconds, Rejects) {
    for (const auto* bad : {"", ".5", "1.", "1.1234567", "-1", "1e3", "abc", "1.2.3", "99999999999999999999"}) {
        EXPECT_FALSE(parse_seconds_us(bad).has_value()) << bad;
    }
}
