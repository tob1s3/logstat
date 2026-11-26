// Нормализация пути: /users/42/orders?page=2 → /users/:id/orders.
// Без неё каждый id превращается в отдельный «эндпоинт», и статистика (и память) разваливается.
#pragma once

#include <string>
#include <string_view>

namespace logstat {

inline constexpr std::size_t kMaxNormalizedPath = 256;

// Пишет результат в out (буфер переиспользуется, чтобы не аллоцировать на каждой строке).
void normalize_path(std::string_view target, std::string& out);

}  // namespace logstat
