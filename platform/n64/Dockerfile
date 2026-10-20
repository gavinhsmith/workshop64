# N64 build image: libdragon's prebuilt MIPS toolchain, plus libdragon (preview branch) built
# into it. libdragon's own image (ghcr.io/dragonminded/libdragon) is no longer public, so
# `make n64` builds this one; Docker caches it after the first time.
FROM ubuntu:24.04

RUN apt-get update \
    && apt-get install -y --no-install-recommends build-essential git python3 curl ca-certificates \
    && rm -rf /var/lib/apt/lists/*

ARG TOOLCHAIN=https://github.com/DragonMinded/libdragon/releases/download/toolchain-continuous-prerelease/gcc-toolchain-mips64-x86_64.deb
RUN curl -fsSL -o /tmp/toolchain.deb "$TOOLCHAIN" && dpkg -i /tmp/toolchain.deb && rm /tmp/toolchain.deb

ENV N64_INST=/opt/libdragon
ENV PATH=$N64_INST/bin:$PATH

ARG LIBDRAGON=preview
RUN git clone --depth 1 --branch "$LIBDRAGON" https://github.com/DragonMinded/libdragon /tmp/libdragon \
    && make -C /tmp/libdragon -j"$(nproc)" install tools-install \
    && rm -rf /tmp/libdragon
