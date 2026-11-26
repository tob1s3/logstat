#include "logstat/normalize.hpp"

#include <algorithm>
#include <cctype>

namespace logstat {
namespace {

bool is_hex(char c) noexcept {
    return std::isxdigit(static_cast<unsigned char>(c)) != 0;
}
bool is_digit(char c) noexcept {
    return c >= '0' && c <= '9';
}

bool is_uuid(std::string_view s) noexcept {
    if (s.size() != 36) {
        return false;
    }
    for (std::size_t i = 0; i < s.size(); ++i) {
        const bool dash = i == 8 || i == 13 || i == 18 || i == 23;
        if (dash ? s[i] != '-' : !is_hex(s[i])) {
            return false;
        }
    }
    return true;
}

std::string_view classify(std::string_view seg) noexcept {
    if (!seg.empty() && std::all_of(seg.begin(), seg.end(), is_digit)) {
        return ":id";
    }
    if (is_uuid(seg)) {
        return ":uuid";
    }
    // Хэши, токены, ObjectId (24 hex) — длинные hex-строки, в которых есть хотя бы одна цифра.
    if (seg.size() >= 16 && std::all_of(seg.begin(), seg.end(), is_hex) &&
        std::any_of(seg.begin(), seg.end(), is_digit)) {
        return ":hash";
    }
    return seg;
}

}  // namespace

void normalize_path(std::string_view target, std::string& out) {
    out.clear();
    // Абсолютная форма "GET http://host/path" встречается у прокси-сканеров.
    if (const auto scheme = target.find("://"); scheme != std::string_view::npos && scheme < 8) {
        const auto slash = target.find('/', scheme + 3);
        target = slash == std::string_view::npos ? std::string_view{"/"} : target.substr(slash);
    }
    target = target.substr(0, target.find_first_of("?#"));
    if (target.empty() || target.front() != '/') {
        out = target.empty() ? "/" : "<invalid>";
        return;
    }

    std::size_t pos = 1;
    while (pos <= target.size()) {
        const auto next = std::min(target.find('/', pos), target.size());
        const auto seg = target.substr(pos, next - pos);
        out += '/';
        out += classify(seg);
        if (out.size() > kMaxNormalizedPath) {
            out.resize(kMaxNormalizedPath);
            out += "...";
            return;
        }
        pos = next + 1;
    }
}

}  // namespace logstat
