// logstat — быстрый анализатор access-логов nginx.
//
// Коды выхода: 0 — успех, 1 — нарушено SLO (--slo-*), 2 — ошибка аргументов или чтения.
#include <fcntl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>

#include <cerrno>
#include <charconv>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <iterator>
#include <optional>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

#include "logstat/analyze.hpp"
#include "logstat/report.hpp"

#ifndef LOGSTAT_VERSION
#define LOGSTAT_VERSION "dev"
#endif

namespace {

constexpr int kExitOk = 0;
constexpr int kExitSlo = 1;
constexpr int kExitError = 2;

constexpr std::string_view kUsage = R"usage(usage: logstat [options] [FILE...]

Reads nginx access logs (combined format, optionally followed by $request_time)
from FILEs or stdin ("-") and prints request, status and latency statistics.

options:
  -f, --format text|json|prom   output format (default: text)
  -j, --threads N               worker threads (default: CPU count)
  -n, --top N                   endpoints and clients to show (default: 10)
      --max-endpoints N         distinct endpoints to track before "(other)" (default: 2000)
      --slo-p99-ms MS           exit 1 if overall p99 latency exceeds MS
      --slo-error-rate R        exit 1 if share of 5xx exceeds R (0..1)
  -h, --help                    show this help
      --version                 show version
)usage";

struct Options {
    std::string format = "text";
    unsigned threads = std::max(1U, std::thread::hardware_concurrency());
    std::size_t top = 10;
    logstat::Limits limits;
    std::optional<double> slo_p99_ms;
    std::optional<double> slo_error_rate;
    std::vector<std::string> files;
};

class UsageError : public std::runtime_error {
    using std::runtime_error::runtime_error;
};

template <typename T>
T parse_number(std::string_view flag, std::string_view text) {
    T value{};
    auto [ptr, ec] = std::from_chars(text.data(), text.data() + text.size(), value);
    if (ec != std::errc{} || ptr != text.data() + text.size()) {
        throw UsageError(std::string(flag) + ": invalid number '" + std::string(text) + "'");
    }
    return value;
}

double parse_double(std::string_view flag, const std::string& text) {
    char* end = nullptr;
    const double v = std::strtod(text.c_str(), &end);
    if (text.empty() || end != text.c_str() + text.size() || !std::isfinite(v) || v < 0) {
        throw UsageError(std::string(flag) + ": invalid value '" + text + "'");
    }
    return v;
}

Options parse_args(int argc, char** argv) {
    Options o;
    const std::vector<std::string> args(argv + 1, argv + argc);
    for (std::size_t i = 0; i < args.size(); ++i) {
        const std::string& a = args[i];
        auto value = [&]() -> const std::string& {
            if (i + 1 >= args.size()) {
                throw UsageError(a + ": value required");
            }
            return args[++i];
        };
        if (a == "-h" || a == "--help") {
            std::cout << kUsage;
            std::exit(kExitOk);
        } else if (a == "--version") {
            std::cout << "logstat " << LOGSTAT_VERSION << '\n';
            std::exit(kExitOk);
        } else if (a == "-f" || a == "--format") {
            o.format = value();
            if (o.format != "text" && o.format != "json" && o.format != "prom") {
                throw UsageError("--format: expected text, json or prom");
            }
        } else if (a == "-j" || a == "--threads") {
            o.threads = std::max(1U, parse_number<unsigned>(a, value()));
        } else if (a == "-n" || a == "--top") {
            o.top = parse_number<std::size_t>(a, value());
        } else if (a == "--max-endpoints") {
            o.limits.max_endpoints = std::max<std::size_t>(1, parse_number<std::size_t>(a, value()));
        } else if (a == "--slo-p99-ms") {
            o.slo_p99_ms = parse_double(a, value());
        } else if (a == "--slo-error-rate") {
            o.slo_error_rate = parse_double(a, value());
        } else if (a.size() > 1 && a[0] == '-') {
            throw UsageError("unknown option: " + a);
        } else {
            o.files.push_back(a);
        }
    }
    if (o.files.empty()) {
        o.files.emplace_back("-");
    }
    return o;
}

// Файл отображается в память: без копирования в буфер и без лишних системных вызовов.
class MappedFile {
public:
    explicit MappedFile(const std::string& path) {
        fd_ = ::open(path.c_str(), O_RDONLY | O_CLOEXEC);
        if (fd_ < 0) {
            throw std::runtime_error(path + ": " + std::strerror(errno));
        }
        struct stat st {};
        if (::fstat(fd_, &st) != 0) {
            const int err = errno;
            ::close(fd_);
            throw std::runtime_error(path + ": " + std::strerror(err));
        }
        if (!S_ISREG(st.st_mode)) {
            ::close(fd_);
            throw std::runtime_error(path + ": not a regular file");
        }
        size_ = static_cast<std::size_t>(st.st_size);
        if (size_ > 0) {
            data_ = ::mmap(nullptr, size_, PROT_READ, MAP_PRIVATE, fd_, 0);
            if (data_ == MAP_FAILED) {
                const int err = errno;
                ::close(fd_);
                throw std::runtime_error(path + ": mmap: " + std::strerror(err));
            }
            ::madvise(data_, size_, MADV_SEQUENTIAL);
        }
    }
    ~MappedFile() {
        if (data_ != nullptr && data_ != MAP_FAILED) {
            ::munmap(data_, size_);
        }
        if (fd_ >= 0) {
            ::close(fd_);
        }
    }
    MappedFile(const MappedFile&) = delete;
    MappedFile& operator=(const MappedFile&) = delete;

    std::string_view view() const noexcept {
        return size_ ? std::string_view(static_cast<const char*>(data_), size_) : std::string_view{};
    }

private:
    int fd_ = -1;
    void* data_ = nullptr;
    std::size_t size_ = 0;
};

logstat::Aggregator analyze_input(const std::string& path, const Options& o) {
    if (path == "-") {
        std::string data((std::istreambuf_iterator<char>(std::cin)), std::istreambuf_iterator<char>());
        return logstat::analyze(data, o.threads, o.limits);
    }
    const MappedFile file(path);
    return logstat::analyze(file.view(), o.threads, o.limits);
}

}  // namespace

int main(int argc, char** argv) {
    std::ios::sync_with_stdio(false);
    Options opts;
    try {
        opts = parse_args(argc, argv);
    } catch (const UsageError& e) {
        std::cerr << "logstat: " << e.what() << "\n\n" << kUsage;
        return kExitError;
    }

    logstat::Aggregator total(opts.limits);
    try {
        for (const auto& path : opts.files) {
            total.merge(analyze_input(path, opts));
        }
    } catch (const std::exception& e) {
        std::cerr << "logstat: " << e.what() << '\n';
        return kExitError;
    }

    const logstat::ReportOptions report{opts.top};
    if (opts.format == "json") {
        std::cout << logstat::to_json(total, report);
    } else if (opts.format == "prom") {
        std::cout << logstat::to_prometheus(total, report);
    } else {
        std::cout << logstat::to_text(total, report);
    }

    int code = kExitOk;
    const auto& lat = total.latency();
    if (opts.slo_p99_ms && lat.count() > 0) {
        const double p99_ms = static_cast<double>(lat.percentile(0.99)) / 1000.0;
        if (p99_ms > *opts.slo_p99_ms) {
            std::cerr << "logstat: SLO violated: p99 " << p99_ms << " ms > " << *opts.slo_p99_ms << " ms\n";
            code = kExitSlo;
        }
    }
    if (opts.slo_error_rate && total.error_rate() > *opts.slo_error_rate) {
        std::cerr << "logstat: SLO violated: error rate " << total.error_rate() << " > " << *opts.slo_error_rate
                  << '\n';
        code = kExitSlo;
    }
    if (total.requests() == 0 && total.malformed() > 0) {
        std::cerr << "logstat: no line matched the expected format (" << total.malformed() << " malformed)\n";
        return kExitError;
    }
    return code;
}
