FROM debian:bookworm-slim

ENV DEBIAN_FRONTEND=noninteractive

RUN apt-get update \
    && apt-get install --yes --no-install-recommends \
        aria2 \
        bsdextrautils \
        bc \
        bison \
        build-essential \
        ca-certificates \
        cmake \
        curl \
        file \
        git \
        libarchive-dev \
        libgtk2.0-dev \
        libpng-dev \
        librsvg2-2 \
        libncurses-dev \
        nettle-dev \
        pkg-config \
        sudo \
        unzip \
        wget \
    && rm -rf /var/lib/apt/lists/*

ARG RGBDS_VERSION=0.9.4
ARG RGBDS_SHA256=29a0bdea2c07ae7e7af9f313de5deaa3ab0557e0251eac9b1e418cc18ebc0ba4
RUN set -eux; \
    curl --fail --location --silent --show-error \
        "https://github.com/gbdev/rgbds/archive/refs/tags/v${RGBDS_VERSION}.tar.gz" \
        --output /tmp/rgbds.tar.gz; \
    echo "${RGBDS_SHA256}  /tmp/rgbds.tar.gz" | sha256sum --check -; \
    mkdir /tmp/rgbds; tar -xzf /tmp/rgbds.tar.gz --strip-components=1 -C /tmp/rgbds; \
    cmake -S /tmp/rgbds -B /tmp/rgbds-build -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX=/usr/local; \
    cmake --build /tmp/rgbds-build --parallel; \
    cmake --install /tmp/rgbds-build; \
    rgbasm --version; rgbgfx --version; \
    rm -rf /tmp/rgbds /tmp/rgbds-build /tmp/rgbds.tar.gz

COPY docker/bootstrap-kindlehf /usr/local/bin/bootstrap-kindlehf
RUN chmod 0755 /usr/local/bin/bootstrap-kindlehf

WORKDIR /workspace
