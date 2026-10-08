# Development toolchain for an abstract C++ puzzle kernel and a Three.js app.
# The same Dockerfile builds as linux/arm64 or linux/amd64.
ARG NODE_VERSION=24.21.0
FROM docker.io/library/node:${NODE_VERSION}-bookworm-slim

ARG EMSDK_VERSION=6.0.11

ENV DEBIAN_FRONTEND=noninteractive

RUN apt-get update \
    && apt-get install -y --no-install-recommends \
        build-essential \
        ca-certificates \
        ccache \
        clang \
        clangd \
        clang-format \
        clang-tidy \
        cmake \
        curl \
        gdb \
        git \
        less \
        ninja-build \
        openssh-client \
        pkg-config \
        python3 \
        python3-venv \
        ripgrep \
        tmux \
        unzip \
        xz-utils \
        zip \
    && rm -rf /var/lib/apt/lists/*

# emsdk selects the native Linux SDK architecture, including ARM64.
# Pin the compiler release instead of installing the moving "latest" alias.
RUN git clone --depth 1 https://github.com/emscripten-core/emsdk.git /opt/emsdk

WORKDIR /opt/emsdk
RUN ./emsdk install "${EMSDK_VERSION}" \
    && ./emsdk activate "${EMSDK_VERSION}" \
    && rm -rf /opt/emsdk/zips

# Make these tools available to shells, container exec, and editor processes.
# Keep the base image's Node.js first among Node installations for npm/Vite.
# Emscripten can still use its own SDK-selected Node via its configuration.
ENV EMSDK=/opt/emsdk \
    EM_CONFIG=/opt/emsdk/.emscripten \
    EM_CACHE=/opt/emsdk/upstream/emscripten/cache \
    PATH=/opt/emsdk:/opt/emsdk/upstream/emscripten:${PATH} \
    VITE_USE_POLLING=1

# Verify C++20 native compilation and actual WebAssembly execution at build time.
# A failing toolchain makes the image build fail, rather than failing later.
RUN printf '%s\n' \
        '#include <array>' \
        '#include <numeric>' \
        '#include <span>' \
        'int main() {' \
        '    std::array<int, 3> values{1, 2, 3};' \
        '    std::span<const int> view{values};' \
        '    return std::accumulate(view.begin(), view.end(), 0) == 6 ? 0 : 1;' \
        '}' > /tmp/toolchain-check.cpp \
    && g++ -std=c++20 /tmp/toolchain-check.cpp -o /tmp/native-check \
    && /tmp/native-check \
    && em++ -std=c++20 /tmp/toolchain-check.cpp \
        -sENVIRONMENT=node \
        -sWASM_ASYNC_COMPILATION=0 \
        -sEXIT_RUNTIME=1 \
        -o /tmp/wasm-check.cjs \
    && node /tmp/wasm-check.cjs \
    && node --version \
    && npm --version \
    && emcc --version \
    && rm -f /tmp/toolchain-check.cpp /tmp/native-check \
        /tmp/wasm-check.cjs /tmp/wasm-check.wasm

# Keep Codex in its own late layer so changing its version reuses the SDK layers.
# Include npm's optional platform package and verify that its native binary runs.
ARG CODEX_VERSION=0.160.1
RUN npm install --global --include=optional "@openai/codex@${CODEX_VERSION}" \
    && codex --version \
    && npm cache clean --force

# Mount only the project directory here. Toolchains stay outside that mount.
WORKDIR /workspace

# EXPOSE documents these ports; container run --publish makes them reachable.
EXPOSE 5173 4173

# Interactive development by default; run/exec can supply another command.
CMD ["/bin/bash"]
