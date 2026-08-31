# Reproducible build/runtime environment for the LOB engine + deep hedging
# stack. This is the actual source of truth for "does it build" - not
# whatever happens to be on your laptop. Prefer developing inside this
# container (or an equivalent devcontainer) over relying on host-native
# toolchains that will drift between your machine, CI, and anyone
# reproducing your results from the paper.
FROM ubuntu:24.04

ENV DEBIAN_FRONTEND=noninteractive

# System toolchain: everything here is apt, deliberately - no vcpkg/Conan
# layered on top. The point of this image *is* the reproducibility layer;
# adding a second package manager inside it would just be yak-shaving on
# your own tooling.
RUN apt-get update && apt-get install -y --no-install-recommends \
    build-essential \
    cmake \
    ninja-build \
    git \
    catch2 \
    libbenchmark-dev \
    python3 \
    python3-dev \
    python3-pip \
    && rm -rf /var/lib/apt/lists/*

WORKDIR /workspace

# Python deps layered separately so this cache invalidates less often than
# the C++ source. Pin versions in requirements.txt once you add torch/numpy/
# pandas - "works on my machine" for a PyTorch deep-hedging model is a much
# worse failure mode than for the C++ side.
COPY python/requirements.txt python/requirements.txt
RUN pip install --break-system-packages --no-cache-dir -r python/requirements.txt

COPY . .

RUN cmake -S cpp -B build -G Ninja -DCMAKE_BUILD_TYPE=Release \
    && cmake --build build -j"$(nproc)" \
    && ctest --test-dir build --output-on-failure

CMD ["/bin/bash"]
