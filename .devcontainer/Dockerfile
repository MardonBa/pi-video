FROM mcr.microsoft.com/devcontainers/base:ubuntu

RUN apt-get update && apt-get install -y --no-install-recommends \
    build-essential \
    clang \
    clangd \
    clang-format \
    gdb \
    make \
    cmake \
    bear \
    pkg-config \
    linux-libc-dev \
    libv4l-dev \
    v4l-utils \
    && rm -rf /var/lib/apt/lists/*
