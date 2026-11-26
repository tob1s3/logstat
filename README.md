# logstat — быстрый анализатор access-логов nginx на C++17

[![CI](https://github.com/tob1s3/logstat/actions/workflows/ci.yml/badge.svg)](https://github.com/tob1s3/logstat/actions/workflows/ci.yml)

Когда сервис тормозит или сыпет 5xx, первый вопрос дежурного — какие эндпоинты, с какой задержкой и с какой частотой
ошибок. `logstat` отвечает на него по сырым access-логам за доли секунды. Утилита считает статусы, перцентили
задержки (p50/p90/p99) в целом и по каждому эндпоинту, а также топ клиентов. Результат она отдаёт текстом, в JSON
или в формате Prometheus. Может работать как проверка SLO в cron или CI.

```text
$ logstat -n 5 /var/log/nginx/access.log
requests: 2000000   malformed lines: 0   bytes sent: 20025605897
status:   2xx 1827355   3xx 0   4xx 151935   5xx 20710   error rate (5xx): 1.04%
latency:  p50 30.1 ms   p90 108.0 ms   p99 309.2 ms   max 3690.0 ms

top endpoints:
    requests      5xx     p50 ms     p99 ms  endpoint
      167128     1737       30.1      305.2  GET /api/products/:id
      166839     1698       30.1      309.2  GET /health
      166827     1701       30.1      309.2  GET /api/users/:id
      ...
```

## Производительность

Синтетический лог на 2 млн строк (218 МБ), Release-сборка, виртуальная машина с 2 vCPU (Intel Xeon 2,1 ГГц):

| | Время | Скорость |
|---|---|---|
| `logstat_bench`, 1 поток (только анализ, данные в памяти) | 0,56 с | 3,6 млн строк/с, ~390 МБ/с |
| `logstat_bench`, 2 потока | 0,33 с | 6,1 млн строк/с, ~665 МБ/с |
| `logstat -j 2 access.log` (чтение файла + полный отчёт) | 0,49 с | |
| для сравнения: `awk '{c[$9]++}'` — только подсчёт статусов | 0,50 с | |

То есть полный отчёт с перцентилями по каждому эндпоинту строится за то же время, за которое awk считает одну колонку.
Бенчмарк повторяется в CI на каждом коммите, результат выводится в сводку job.

## За счёт чего

- **Без копирования.** Файл отображается в память (`mmap` + `MADV_SEQUENTIAL`). Парсер возвращает `std::string_view`
  на исходный буфер, числа разбираются через `std::from_chars`, без локалей и исключений.
- **Многопоточность без блокировок.** Буфер делится на куски по границам строк. Каждый поток копит статистику
  в своём `Aggregator`, в конце результаты сливаются. Результат не зависит от числа потоков, и это проверяется тестом.
- **Перцентили с гарантированной точностью и фиксированной памятью.** Лог-линейная гистограмма в духе HdrHistogram:
  значения до 64 мкс хранятся точно, дальше ошибка не больше 1/64 ≈ 1,6 % при любом масштабе. Хранить все значения
  и сортировать не нужно, а слияние гистограмм из разных потоков точное.
- **Нормализация путей.** `/api/users/42?x=1` → `/api/users/:id`; UUID → `:uuid`, длинные hex-идентификаторы → `:hash`.
  Иначе каждый id стал бы отдельным «эндпоинтом».

## Устойчивость к враждебным логам

Access-лог — это данные, которые пишет внешний мир. Поэтому:

- **Лимит кардинальности.** После `--max-endpoints` уникальных эндпоинтов (по умолчанию 2000) новые попадают
  в `(other)`, то же для клиентов. Перебор путей сканером не съест память, а суммарные счётчики остаются точными.
- **Экранирование на выходе.** JSON-строки и метки Prometheus экранируются. В текстовом отчёте управляющие символы
  показываются как `\x1b`, поэтому ESC-последовательность из лога не перехватит терминал.
- **Метрики только для топ-N эндпоинтов.** Это ограничивает кардинальность меток в TSDB.
- **Нераспознанные строки не роняют разбор:** они считаются отдельно. Мусор в строке запроса
  (`"\x16\x03\x01"` — TLS-рукопожатие на HTTP-порт) тоже учитывается: статус и размер полезны.
- **Фаззинг.** Парсер и агрегатор проверяются libFuzzer под ASan и UBSan ([`fuzz/fuzz_parser.cpp`](fuzz/fuzz_parser.cpp)).

## Использование

```text
logstat [options] [FILE...]          # без файлов или "-" — читать stdin

  -f, --format text|json|prom   формат вывода
  -j, --threads N               число потоков (по умолчанию — число ядер)
  -n, --top N                   сколько эндпоинтов и клиентов показывать
      --max-endpoints N         лимит уникальных эндпоинтов
      --slo-p99-ms MS           код выхода 1, если общий p99 больше MS
      --slo-error-rate R        код выхода 1, если доля 5xx больше R (0..1)
```

Коды выхода: `0` — успех, `1` — нарушено SLO, `2` — ошибка аргументов или чтения (в том числе если ни одна строка
не подошла под формат: молча выдать «0 запросов» на чужом формате хуже, чем упасть).

Формат лога — `combined`, к которому можно добавить `$request_time` (без него не будет задержек):

```nginx
log_format timed '$remote_addr - $remote_user [$time_local] "$request" '
                 '$status $body_bytes_sent "$http_referer" "$http_user_agent" $request_time';
access_log /var/log/nginx/access.log timed;
```

Примеры:

```bash
# проверка SLO раз в 5 минут по свежему куску лога
tail -n 100000 /var/log/nginx/access.log | logstat --slo-p99-ms 500 --slo-error-rate 0.01 >/dev/null \
  || echo "SLO нарушено" | systemd-cat -p warning

# метрики для node_exporter (textfile collector)
logstat -f prom -n 20 /var/log/nginx/access.log > /var/lib/node_exporter/textfile/nginx.prom.$$ \
  && mv /var/lib/node_exporter/textfile/nginx.prom.$$ /var/lib/node_exporter/textfile/nginx.prom

# Docker: distroless-образ, запуск не от root
docker build -t logstat . && docker run --rm -i logstat --format json < access.log
```

## Сборка и тесты

```bash
cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build
ctest --test-dir build --output-on-failure
./build/logstat_bench 2000000

# с санитайзерами
cmake -B build-asan -DCMAKE_BUILD_TYPE=Debug "-DLOGSTAT_SANITIZE=address;undefined"
cmake -B build-tsan -DCMAKE_BUILD_TYPE=RelWithDebInfo -DLOGSTAT_SANITIZE=thread
```

Зависимостей нет; GoogleTest подтягивается через CMake FetchContent, закреплённый по коммиту.

Тесты (GoogleTest, 43 юнит-теста и 8 сквозных проверок бинарника через CTest):

- разбор форматов combined, common и combined + `$request_time`, CRLF, мусорных строк запроса и некорректных строк;
- точность гистограммы: на 200 000 значений из логнормального распределения p50/p90/p99/p99.9 расходятся
  с точными значениями не больше чем на 1/64; проверяются непрерывность корзин и точное слияние;
- одинаковый результат при 1, 2, 3 и 8 потоках;
- лимит кардинальности: ни один запрос не теряется;
- экранирование JSON, меток Prometheus и управляющих символов в терминале;
- коды выхода CLI: SLO, отсутствующий файл, неверный аргумент.

## CI

| Job | Что проверяет |
|---|---|
| `format` | clang-format |
| `build` | gcc и clang × (Debug + AddressSanitizer + UndefinedBehaviorSanitizer, Release), `-Werror` с `-Wconversion -Wshadow -Wold-style-cast` и др. |
| `tsan` | ThreadSanitizer на многопоточном разборе |
| `clang-tidy` | bugprone, performance, modernize — предупреждения считаются ошибками |
| `fuzz` | 60 секунд libFuzzer с ASan/UBSan; при падении входные данные сохраняются как артефакт |
| `bench` | замер скорости, результат — в сводке job |
| `docker` | сборка distroless-образа и smoke-тест |

Actions закреплены по SHA коммита, у `GITHUB_TOKEN` только чтение, обновления приносит Dependabot.

## Устройство

```text
include/logstat/
  parser.hpp       разбор строки → LogEntry (string_view, без аллокаций)
  histogram.hpp    лог-линейная гистограмма задержек
  normalize.hpp    /users/42 → /users/:id
  aggregator.hpp   счётчики, эндпоинты, клиенты, лимиты кардинальности
  analyze.hpp      деление на куски и параллельный разбор
  report.hpp       text / json / prometheus + экранирование
src/main.cpp       CLI, mmap, SLO
tests/  bench/  fuzz/
```

## Ограничения

- Если лимит `--max-endpoints` достигнут, то при нескольких потоках набор эндпоинтов, попавших в `(other)`, может
  отличаться от однопоточного запуска. Суммарные счётчики от этого не меняются.
- Сжатые логи (`.gz`) читаются через `zcat access.log.gz | logstat`.
- Время в логе (`$time_local`) разбирается, но для разбивки по интервалам пока не используется.

## Лицензия

MIT
