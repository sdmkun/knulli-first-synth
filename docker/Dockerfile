# aarch64 cross toolchain for Knulli (TrimUI Brick, Allwinner A133P / Cortex-A53).
# The binary needs glibc >= 2.36 (static libstdc++ pulls in arc4random);
# Knulli builds that support the Brick ship a newer glibc.
FROM debian:bookworm

RUN dpkg --add-architecture arm64 \
 && apt-get update \
 && apt-get install -y --no-install-recommends \
      g++-aarch64-linux-gnu \
      libsdl2-dev:arm64 \
 && rm -rf /var/lib/apt/lists/*

WORKDIR /src
