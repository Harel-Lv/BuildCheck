FROM ubuntu:24.04 AS build

RUN apt-get update && apt-get install -y --no-install-recommends \
    build-essential \
    cmake \
    && rm -rf /var/lib/apt/lists/*

WORKDIR /workspace
COPY BuildCheck/API ./BuildCheck/API

RUN cmake -S BuildCheck/API -B /tmp/api-build \
    && cmake --build /tmp/api-build --config Release

FROM ubuntu:24.04

RUN apt-get update && apt-get install -y --no-install-recommends \
    libstdc++6 \
    && rm -rf /var/lib/apt/lists/*

COPY --from=build /tmp/api-build/api_server /usr/local/bin/api_server

ENV ENGINE_HOST=engine
ENV ENGINE_PORT=9090
ENV BUILDCHECK_SHARED_TMP=/shared-tmp

RUN mkdir -p /shared-tmp

EXPOSE 8080

CMD ["/usr/local/bin/api_server"]
