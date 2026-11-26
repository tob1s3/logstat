#pragma once

#include <string_view>

#include "logstat/aggregator.hpp"

namespace logstat {

// Делит буфер на threads кусков по границам строк, разбирает их параллельно
// и сливает результаты. Результат не зависит от числа потоков (кроме того, какие
// именно редкие эндпоинты попадут в "(other)" при достижении лимита).
Aggregator analyze(std::string_view buffer, unsigned threads, Limits limits = {});

}  // namespace logstat
