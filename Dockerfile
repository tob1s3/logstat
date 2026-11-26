# syntax=docker/dockerfile:1
FROM gcc:14-bookworm AS build
# Версии пакетов задаёт базовый образ; сам образ обновляет Dependabot.
# hadolint ignore=DL3008
RUN apt-get update \
 && apt-get install -y --no-install-recommends cmake ninja-build \
 && rm -rf /var/lib/apt/lists/*
WORKDIR /src
COPY CMakeLists.txt ./
COPY include ./include
COPY src ./src
RUN cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Release -DLOGSTAT_BUILD_TESTS=OFF -DLOGSTAT_BUILD_BENCH=OFF \
      -DCMAKE_EXE_LINKER_FLAGS="-static-libstdc++ -static-libgcc" \
 && cmake --build build \
 && strip build/logstat

# distroless: в образе нет shell и пакетного менеджера; nonroot — uid 65532.
FROM gcr.io/distroless/cc-debian12:nonroot
COPY --from=build /src/build/logstat /usr/local/bin/logstat
ENTRYPOINT ["/usr/local/bin/logstat"]
