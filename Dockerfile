FROM gcc:13-bookworm AS build
WORKDIR /app
COPY main.cpp .
RUN g++ -O2 -std=c++17 -o ping-server main.cpp

FROM debian:bookworm-slim
RUN apt-get update \
    && apt-get install -y --no-install-recommends iputils-ping ca-certificates \
    && rm -rf /var/lib/apt/lists/*
COPY --from=build /app/ping-server /usr/local/bin/ping-server
EXPOSE 8080
ENTRYPOINT ["/usr/local/bin/ping-server"]
