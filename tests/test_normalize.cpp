#include <gtest/gtest.h>

#include <string>

#include "logstat/normalize.hpp"

namespace {

std::string norm(std::string_view s) {
    std::string out;
    logstat::normalize_path(s, out);
    return out;
}

}  // namespace

TEST(Normalize, Ids) {
    EXPECT_EQ(norm("/api/users/42"), "/api/users/:id");
    EXPECT_EQ(norm("/api/users/42/orders/7"), "/api/users/:id/orders/:id");
    EXPECT_EQ(norm("/orders/3f2b8c1e-9d4a-4c7b-8e21-5a6f0c9d1b23"), "/orders/:uuid");
    EXPECT_EQ(norm("/obj/5f8d0d55b54764421b7156c9"), "/obj/:hash");
}

TEST(Normalize, KeepsWords) {
    EXPECT_EQ(norm("/api/v2/health"), "/api/v2/health");
    EXPECT_EQ(norm("/deadbeefdeadbeef"), "/deadbeefdeadbeef");  // hex без цифр — скорее слово
    EXPECT_EQ(norm("/static/app.js"), "/static/app.js");
}

TEST(Normalize, QueryAndFragment) {
    EXPECT_EQ(norm("/search?q=1&page=2"), "/search");
    EXPECT_EQ(norm("/page#top"), "/page");
    EXPECT_EQ(norm("/?x=1"), "/");
}

TEST(Normalize, EdgeCases) {
    EXPECT_EQ(norm(""), "/");
    EXPECT_EQ(norm("/"), "/");
    EXPECT_EQ(norm("/a/"), "/a/");
    EXPECT_EQ(norm("*"), "<invalid>");
    EXPECT_EQ(norm("http://evil.example/proxy/1"), "/proxy/:id");
    EXPECT_EQ(norm("http://evil.example"), "/");
}

TEST(Normalize, LongPathIsTruncated) {
    std::string path;
    for (int i = 0; i < 100; ++i) {
        path += "/segment";
    }
    const auto out = norm(path);
    EXPECT_EQ(out.size(), logstat::kMaxNormalizedPath + 3);
    EXPECT_EQ(out.substr(out.size() - 3), "...");
}
