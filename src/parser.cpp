#include "logstat/parser.hpp"

#include <charconv>
#include <limits>

namespace logstat {
namespace {

// Курсор по строке: каждая операция либо продвигается, либо сообщает об ошибке.
class Cursor {
public:
    explicit Cursor(std::string_view s) noexcept : s_(s) {}

    bool expect(char c) noexcept {
        if (pos_ < s_.size() && s_[pos_] == c) {
            ++pos_;
            return true;
        }
        return false;
    }

    // Токен до разделителя (разделитель пропускается). Пустой токен — ошибка.
    std::optional<std::string_view> until(char delim) noexcept {
        const auto end = s_.find(delim, pos_);
        if (end == std::string_view::npos || end == pos_) {
            return std::nullopt;
        }
        auto token = s_.substr(pos_, end - pos_);
        pos_ = end + 1;
        return token;
    }

    // Содержимое в кавычках. nginx экранирует '"' внутри полей как \x22,
    // поэтому первая следующая кавычка закрывает поле.
    std::optional<std::string_view> quoted() noexcept {
        if (!expect('"')) {
            return std::nullopt;
        }
        const auto end = s_.find('"', pos_);
        if (end == std::string_view::npos) {
            return std::nullopt;
        }
        auto token = s_.substr(pos_, end - pos_);
        pos_ = end + 1;
        return token;
    }

    std::string_view rest() const noexcept { return s_.substr(pos_); }
    bool done() const noexcept { return pos_ >= s_.size(); }

private:
    std::string_view s_;
    std::size_t pos_ = 0;
};

template <typename T>
std::optional<T> to_number(std::string_view text) noexcept {
    T value{};
    const auto* first = text.data();
    const auto* last = text.data() + text.size();
    auto [ptr, ec] = std::from_chars(first, last, value);
    if (ec != std::errc{} || ptr != last || text.empty()) {
        return std::nullopt;
    }
    return value;
}

void split_request(std::string_view request, LogEntry& e) noexcept {
    // Обычный случай: "GET /path HTTP/1.1". Сканеры и битые клиенты присылают что угодно —
    // такие строки не отбрасываем (статус и размер полезны), но метод оставляем пустым.
    const auto sp1 = request.find(' ');
    const auto sp2 = request.rfind(' ');
    if (sp1 == std::string_view::npos || sp1 == sp2 || sp1 == 0) {
        e.target = request;
        return;
    }
    e.method = request.substr(0, sp1);
    e.target = request.substr(sp1 + 1, sp2 - sp1 - 1);
    e.protocol = request.substr(sp2 + 1);
}

}  // namespace

std::optional<std::uint64_t> parse_seconds_us(std::string_view text) noexcept {
    const auto dot = text.find('.');
    const auto int_part = text.substr(0, dot);
    const auto frac_part = dot == std::string_view::npos ? std::string_view{} : text.substr(dot + 1);
    if (int_part.empty() || frac_part.size() > 6 || (dot != std::string_view::npos && frac_part.empty())) {
        return std::nullopt;
    }
    const auto seconds = to_number<std::uint64_t>(int_part);
    if (!seconds || *seconds > std::numeric_limits<std::uint64_t>::max() / 1'000'000) {
        return std::nullopt;
    }
    std::uint64_t micros = 0;
    if (!frac_part.empty()) {
        const auto frac = to_number<std::uint64_t>(frac_part);
        if (!frac) {
            return std::nullopt;
        }
        micros = *frac;
        for (auto i = frac_part.size(); i < 6; ++i) {
            micros *= 10;
        }
    }
    return *seconds * 1'000'000 + micros;
}

std::optional<LogEntry> parse_line(std::string_view line) noexcept {
    while (!line.empty() && (line.back() == '\r' || line.back() == '\n')) {
        line.remove_suffix(1);
    }
    Cursor c(line);
    LogEntry e;

    const auto addr = c.until(' ');
    if (!addr || !c.until(' ') /* ident, обычно "-" */) {
        return std::nullopt;
    }
    e.remote_addr = *addr;

    // $remote_user теоретически может содержать пробелы, поэтому ищем '['.
    if (!c.until('[')) {
        return std::nullopt;
    }
    const auto time = c.until(']');
    if (!time || !c.expect(' ')) {
        return std::nullopt;
    }
    e.time_local = *time;

    const auto request = c.quoted();
    if (!request || !c.expect(' ')) {
        return std::nullopt;
    }
    split_request(*request, e);

    const auto status_text = c.until(' ');
    const auto status = status_text ? to_number<int>(*status_text) : std::nullopt;
    if (!status || *status < 100 || *status > 999) {
        return std::nullopt;
    }
    e.status = *status;

    // Размер — до пробела или до конца строки (формат common без referer/UA).
    const auto rest = c.rest();
    const auto sp = rest.find(' ');
    const auto bytes_text = rest.substr(0, sp);
    if (const auto bytes = to_number<std::uint64_t>(bytes_text)) {
        e.bytes = *bytes;
    } else if (bytes_text != "-") {
        return std::nullopt;
    }
    if (sp == std::string_view::npos) {
        return e;
    }
    c.until(' ');

    // Referer и User-Agent не нужны для статистики, но формат проверяем.
    if (!c.quoted() || !c.expect(' ') || !c.quoted()) {
        return std::nullopt;
    }
    if (c.done()) {
        return e;
    }
    if (!c.expect(' ')) {
        return std::nullopt;
    }
    // Первое поле после User-Agent — $request_time; дальше могут идти другие поля.
    auto tail = c.rest();
    tail = tail.substr(0, tail.find(' '));
    e.request_time_us = parse_seconds_us(tail);
    return e;
}

}  // namespace logstat
