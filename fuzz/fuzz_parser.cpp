// libFuzzer: парсер и нормализация не должны падать и читать за границы ни на каком входе.
//   cmake -B build-fuzz -DCMAKE_CXX_COMPILER=clang++ -DLOGSTAT_BUILD_FUZZ=ON && cmake --build build-fuzz
//   ./build-fuzz/fuzz_parser -max_total_time=60 fuzz/corpus
#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>

#include "logstat/aggregator.hpp"
#include "logstat/normalize.hpp"
#include "logstat/parser.hpp"

extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t* data, std::size_t size) {
    const std::string_view input(reinterpret_cast<const char*>(data), size);
    if (const auto entry = logstat::parse_line(input)) {
        std::string path;
        logstat::normalize_path(entry->target, path);
    }
    logstat::Aggregator agg(logstat::Limits{8, 8});
    agg.consume(input);
    (void)agg.top_endpoints(4);
    return 0;
}
