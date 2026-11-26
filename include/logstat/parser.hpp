// Разбор строки access-лога nginx без копирования: все поля — string_view на исходный буфер.
#pragma once

#include <cstdint>
#include <optional>
#include <string_view>

namespace logstat {

struct LogEntry {
    std::string_view remote_addr;
    std::string_view time_local;
    std::string_view method;  // пусто, если строка запроса нестандартная
    std::string_view target;  // путь вместе с query string, как в запросе
    std::string_view protocol;
    int status = 0;
    std::uint64_t bytes = 0;
    // $request_time в микросекундах. Нет, если лог в формате combined без времени ответа.
    std::optional<std::uint64_t> request_time_us;
};

// Поддерживаемый формат (combined + $request_time в конце — его можно не писать):
//   log_format timed '$remote_addr - $remote_user [$time_local] "$request" '
//                    '$status $body_bytes_sent "$http_referer" "$http_user_agent" $request_time';
//
// Возвращает nullopt для строк, которые не удалось разобрать. Не бросает исключений и
// не читает за пределами line — функция проверяется фаззингом (fuzz/fuzz_parser.cpp).
std::optional<LogEntry> parse_line(std::string_view line) noexcept;

// "0.123" → 123000. Не больше 6 знаков после точки; nullopt для мусора и переполнения.
std::optional<std::uint64_t> parse_seconds_us(std::string_view text) noexcept;

}  // namespace logstat
