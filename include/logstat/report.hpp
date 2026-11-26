#pragma once

#include <cstddef>
#include <string>
#include <string_view>

#include "logstat/aggregator.hpp"

namespace logstat {

struct ReportOptions {
    std::size_t top = 10;
};

std::string to_text(const Aggregator& agg, const ReportOptions& opts);
std::string to_json(const Aggregator& agg, const ReportOptions& opts);
// Текстовый формат Prometheus. Эндпоинты — только топ-N: метки из логов не должны
// раздувать кардинальность в TSDB.
std::string to_prometheus(const Aggregator& agg, const ReportOptions& opts);

// Экранирование данных из лога — они недоверенные.
std::string json_escape(std::string_view s);
std::string prom_label_escape(std::string_view s);

}  // namespace logstat
