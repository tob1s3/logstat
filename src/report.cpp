#include "logstat/report.hpp"

#include <array>
#include <cstdio>
#include <sstream>

namespace logstat {
namespace {

constexpr std::array<std::pair<const char*, double>, 4> kQuantiles{{
    {"0.5", 0.5},
    {"0.9", 0.9},
    {"0.99", 0.99},
    {"0.999", 0.999},
}};

std::string ms(std::uint64_t us) {
    std::array<char, 32> buf{};
    std::snprintf(buf.data(), buf.size(), "%.1f", static_cast<double>(us) / 1000.0);
    return buf.data();
}

std::string seconds(std::uint64_t us) {
    std::array<char, 32> buf{};
    std::snprintf(buf.data(), buf.size(), "%.6f", static_cast<double>(us) / 1e6);
    return buf.data();
}

std::string fixed(double v, int digits) {
    std::array<char, 64> buf{};
    std::snprintf(buf.data(), buf.size(), "%.*f", digits, v);
    return buf.data();
}

// "GET /users/:id" → ("GET", "/users/:id")
std::pair<std::string_view, std::string_view> split_key(std::string_view key) {
    const auto sp = key.find(' ');
    if (sp == std::string_view::npos) {
        return {"", key};
    }
    return {key.substr(0, sp), key.substr(sp + 1)};
}

// Для вывода в терминал: управляющие символы из лога (например, ESC-последовательности)
// не должны попадать в терминал как есть.
std::string printable(std::string_view s) {
    std::string out;
    out.reserve(s.size());
    for (const char ch : s) {
        const auto c = static_cast<unsigned char>(ch);
        if (c < 0x20 || c == 0x7f) {
            std::array<char, 8> buf{};
            std::snprintf(buf.data(), buf.size(), "\\x%02x", c);
            out += buf.data();
        } else {
            out += ch;
        }
    }
    return out;
}

}  // namespace

std::string json_escape(std::string_view s) {
    std::string out;
    out.reserve(s.size() + 2);
    for (const char ch : s) {
        const auto c = static_cast<unsigned char>(ch);
        switch (ch) {
            case '"': out += "\\\""; break;
            case '\\': out += "\\\\"; break;
            case '\n': out += "\\n"; break;
            case '\r': out += "\\r"; break;
            case '\t': out += "\\t"; break;
            default:
                if (c < 0x20 || c == 0x7f) {
                    std::array<char, 8> buf{};
                    std::snprintf(buf.data(), buf.size(), "\\u%04x", c);
                    out += buf.data();
                } else {
                    out += ch;
                }
        }
    }
    return out;
}

std::string prom_label_escape(std::string_view s) {
    std::string out;
    out.reserve(s.size());
    for (const char ch : s) {
        switch (ch) {
            case '\\': out += "\\\\"; break;
            case '"': out += "\\\""; break;
            case '\n': out += "\\n"; break;
            default: out += ch;
        }
    }
    return out;
}

std::string to_text(const Aggregator& agg, const ReportOptions& opts) {
    std::ostringstream o;
    const auto& lat = agg.latency();
    const auto& st = agg.status_classes();
    o << "requests: " << agg.requests() << "   malformed lines: " << agg.malformed() << "   bytes sent: " << agg.bytes()
      << '\n';
    o << "status:   2xx " << st[2] << "   3xx " << st[3] << "   4xx " << st[4] << "   5xx " << st[5]
      << "   error rate (5xx): " << fixed(agg.error_rate() * 100.0, 2) << "%\n";
    if (lat.count() > 0) {
        o << "latency:  p50 " << ms(lat.percentile(0.5)) << " ms   p90 " << ms(lat.percentile(0.9)) << " ms   p99 "
          << ms(lat.percentile(0.99)) << " ms   max " << ms(lat.max()) << " ms\n";
    } else {
        o << "latency:  n/a (no $request_time in log format)\n";
    }

    o << "\ntop endpoints:\n";
    std::array<char, 512> line{};
    std::snprintf(line.data(), line.size(), "  %10s %8s %10s %10s  %s\n", "requests", "5xx", "p50 ms", "p99 ms",
                  "endpoint");
    o << line.data();
    for (const auto& ep : agg.top_endpoints(opts.top)) {
        const auto& s = *ep.stats;
        const bool has_lat = s.latency.count() > 0;
        std::snprintf(line.data(), line.size(), "  %10llu %8llu %10s %10s  ",
                      static_cast<unsigned long long>(s.requests), static_cast<unsigned long long>(s.server_errors),
                      has_lat ? ms(s.latency.percentile(0.5)).c_str() : "-",
                      has_lat ? ms(s.latency.percentile(0.99)).c_str() : "-");
        o << line.data() << printable(ep.key) << '\n';
    }

    o << "\ntop clients:\n";
    for (const auto& [addr, count] : agg.top_clients(opts.top)) {
        std::snprintf(line.data(), line.size(), "  %10llu  ", static_cast<unsigned long long>(count));
        o << line.data() << printable(addr) << '\n';
    }
    return o.str();
}

std::string to_json(const Aggregator& agg, const ReportOptions& opts) {
    std::ostringstream o;
    const auto& lat = agg.latency();
    const auto& st = agg.status_classes();
    o << "{\"requests\":" << agg.requests() << ",\"malformed\":" << agg.malformed() << ",\"bytes\":" << agg.bytes()
      << ",\"status\":{\"1xx\":" << st[1] << ",\"2xx\":" << st[2] << ",\"3xx\":" << st[3] << ",\"4xx\":" << st[4]
      << ",\"5xx\":" << st[5] << "},\"error_rate\":" << fixed(agg.error_rate(), 6);
    o << ",\"latency_ms\":";
    if (lat.count() > 0) {
        o << "{\"p50\":" << ms(lat.percentile(0.5)) << ",\"p90\":" << ms(lat.percentile(0.9))
          << ",\"p99\":" << ms(lat.percentile(0.99)) << ",\"max\":" << ms(lat.max())
          << ",\"mean\":" << fixed(lat.mean() / 1000.0, 1) << '}';
    } else {
        o << "null";
    }
    o << ",\"endpoints\":[";
    bool first = true;
    for (const auto& ep : agg.top_endpoints(opts.top)) {
        const auto& s = *ep.stats;
        const auto [method, path] = split_key(ep.key);
        o << (first ? "" : ",") << "{\"method\":\"" << json_escape(method) << "\",\"path\":\"" << json_escape(path)
          << "\",\"requests\":" << s.requests << ",\"server_errors\":" << s.server_errors;
        if (s.latency.count() > 0) {
            o << ",\"p50_ms\":" << ms(s.latency.percentile(0.5)) << ",\"p99_ms\":" << ms(s.latency.percentile(0.99));
        }
        o << '}';
        first = false;
    }
    o << "],\"clients\":[";
    first = true;
    for (const auto& [addr, count] : agg.top_clients(opts.top)) {
        o << (first ? "" : ",") << "{\"addr\":\"" << json_escape(addr) << "\",\"requests\":" << count << '}';
        first = false;
    }
    o << "]}\n";
    return o.str();
}

std::string to_prometheus(const Aggregator& agg, const ReportOptions& opts) {
    std::ostringstream o;
    const auto& st = agg.status_classes();
    o << "# HELP logstat_requests_total Requests by status class.\n# TYPE logstat_requests_total counter\n";
    for (int c = 1; c <= 5; ++c) {
        o << "logstat_requests_total{status_class=\"" << c << "xx\"} " << st[static_cast<std::size_t>(c)] << '\n';
    }
    o << "# HELP logstat_malformed_lines_total Lines that could not be parsed.\n"
         "# TYPE logstat_malformed_lines_total counter\n"
      << "logstat_malformed_lines_total " << agg.malformed() << '\n';

    const auto& lat = agg.latency();
    o << "# HELP logstat_request_duration_seconds Request time ($request_time).\n"
         "# TYPE logstat_request_duration_seconds summary\n";
    for (const auto& [label, q] : kQuantiles) {
        o << "logstat_request_duration_seconds{quantile=\"" << label << "\"} " << seconds(lat.percentile(q)) << '\n';
    }
    o << "logstat_request_duration_seconds_sum " << fixed(lat.sum_seconds(), 6) << '\n'
      << "logstat_request_duration_seconds_count " << lat.count() << '\n';

    o << "# HELP logstat_endpoint_requests_total Requests per normalized endpoint (top N only).\n"
         "# TYPE logstat_endpoint_requests_total counter\n";
    const auto top = agg.top_endpoints(opts.top);
    for (const auto& ep : top) {
        const auto [method, path] = split_key(ep.key);
        o << "logstat_endpoint_requests_total{method=\"" << prom_label_escape(method) << "\",path=\""
          << prom_label_escape(path) << "\"} " << ep.stats->requests << '\n';
    }
    o << "# HELP logstat_endpoint_duration_p99_seconds p99 request time per endpoint (top N only).\n"
         "# TYPE logstat_endpoint_duration_p99_seconds gauge\n";
    for (const auto& ep : top) {
        if (ep.stats->latency.count() == 0) {
            continue;
        }
        const auto [method, path] = split_key(ep.key);
        o << "logstat_endpoint_duration_p99_seconds{method=\"" << prom_label_escape(method) << "\",path=\""
          << prom_label_escape(path) << "\"} " << seconds(ep.stats->latency.percentile(0.99)) << '\n';
    }
    return o.str();
}

}  // namespace logstat
