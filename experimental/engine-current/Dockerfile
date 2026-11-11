# syntax=docker/dockerfile:1
# Native amd64 compiler, i386 Bookworm libraries. No target executable runs
# while configuring/compiling; QEMU user is for the final CPU smoke test.
FROM --platform=linux/amd64 amd64/debian:bookworm-slim
RUN apt-get update \
    && dpkg --add-architecture i386 \
    && apt-get update \
    && apt-get install -y --no-install-recommends \
       build-essential g++-multilib cmake ninja-build pkg-config \
       python3 perl ruby unifdef libicu-dev:i386 qemu-user file ca-certificates \
    && rm -rf /var/lib/apt/lists/*
WORKDIR /work
