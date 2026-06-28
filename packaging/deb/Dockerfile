# SPDX-License-Identifier: Apache-2.0
#
# Multi-stage Dockerfile that builds and install-tests the Endo Debian package
# against a chosen Ubuntu release (default: the latest LTS, 26.04).
#
# The same flow runs locally (via scripts/package-deb.sh) and in CI, so the .deb
# is always produced and verified in an environment identical to the one users
# install it on — independent of whatever the GitHub-hosted runner happens to be.
#
# Targets:
#   builder  - compile Endo and run cpack to produce /out/*.deb + /out/*.ddeb
#   tester   - install the .deb into a clean image and run smoke tests
#   export   - scratch image holding only /out, for `buildx --output type=local`

ARG UBUNTU_VERSION=26.04

# ---------------------------------------------------------------------------
# Stage 1: build the package
# ---------------------------------------------------------------------------
FROM ubuntu:${UBUNTU_VERSION} AS builder

ENV DEBIAN_FRONTEND=noninteractive

# Toolchain and build prerequisites. Clang 21 is installed from apt.llvm.org to
# match the compiler used by the rest of Endo's CI.
RUN apt-get update && apt-get install -y --no-install-recommends \
        ca-certificates \
        wget \
        gnupg \
        lsb-release \
        software-properties-common \
        python3 \
        git \
        ninja-build \
        cmake \
        file \
        xz-utils \
        dpkg-dev \
        binutils \
    && wget -qO- https://apt.llvm.org/llvm.sh | bash -s -- 21 \
    && rm -rf /var/lib/apt/lists/*

# System -dev packages that exist in Ubuntu. find_package() picks these up so the
# resulting .deb declares proper runtime dependencies (via SHLIBDEPS) instead of
# statically bundling them. Dependencies that Ubuntu does not package (libunicode,
# boxed-cpp, reflection-cpp, llama.cpp) are built from source by CPM and linked
# statically, producing no runtime dependency — this is expected.
RUN apt-get update && apt-get install -y --no-install-recommends \
        libcurl4-openssl-dev \
        libyaml-cpp-dev \
        nlohmann-json3-dev \
        libmsgsl-dev \
    && rm -rf /var/lib/apt/lists/*

# Package version, supplied by the caller (scripts/package-deb.sh derives it from
# `git describe`). The repository's .git is excluded from the build context — it is
# often a worktree pointer that does not resolve inside the container — so the
# version is injected here instead. cmake/Version.cmake reads version.txt first.
ARG ENDO_VERSION=0.0.0

WORKDIR /src
COPY . .

# Record the version for cmake/Version.cmake, then fetch the vendored crispy &
# vtparser sources. get_contour_dirs.py locates the tree via `git rev-parse`, so a
# throwaway repo is initialised (the real .git is not in the build context); the
# script itself clones the sources it needs over the network.
RUN printf '%s\n' "${ENDO_VERSION}" > version.txt \
    && git init -q . \
    && python3 scripts/get_contour_dirs.py

# Configure, build, and package. RelWithDebInfo + --build-id (from the clang-deb
# preset) are what make the separate .ddeb debug-symbols package possible.
RUN cmake --preset clang-deb \
        -DCMAKE_C_COMPILER=clang-21 \
        -DCMAKE_CXX_COMPILER=clang++-21 \
    && cmake --build --preset clang-deb \
    && cpack --preset clang-deb

# Collect the artifacts in a predictable location for the later stages.
RUN mkdir -p /out \
    && cp build/clang-deb/*.deb /out/ \
    && cp build/clang-deb/*.ddeb /out/ \
    && ls -l /out

# ---------------------------------------------------------------------------
# Stage 2: install the package into a clean image and smoke-test it
# ---------------------------------------------------------------------------
FROM ubuntu:${UBUNTU_VERSION} AS tester

ENV DEBIAN_FRONTEND=noninteractive

# Both packages are installed so CI proves they are installable, not merely built.
# apt resolves the .deb's declared dependencies; the -dbgsym .ddeb is installed
# afterwards because it depends on the exact endo version installed by the .deb.
COPY --from=builder /out/ /tmp/pkgs/
RUN apt-get update \
    && apt-get install -y /tmp/pkgs/endo_*.deb \
    && apt-get install -y /tmp/pkgs/endo-dbgsym_*.ddeb \
    && rm -rf /var/lib/apt/lists/*

# Smoke tests — any failure fails the image build.
RUN set -eux; \
    endo --version; \
    test "$(endo -c 'println "hello"')" = "hello"; \
    test -d /usr/share/endo; \
    test -d /usr/lib/debug; \
    dpkg -s endo; \
    dpkg -s endo-dbgsym

# ---------------------------------------------------------------------------
# Stage 3: export-only image (artifacts at /), for `buildx --output type=local`
# ---------------------------------------------------------------------------
FROM scratch AS export
COPY --from=builder /out/ /
