FROM debian:bookworm-slim

ENV DEBIAN_FRONTEND=noninteractive

RUN apt-get update \
    && apt-get install --yes --no-install-recommends \
        aria2 \
        build-essential \
        ca-certificates \
        curl \
        file \
        git \
        libarchive-dev \
        libgtk2.0-dev \
        libncurses-dev \
        nettle-dev \
        pkg-config \
        sudo \
        unzip \
        wget \
    && rm -rf /var/lib/apt/lists/*

COPY docker/bootstrap-kindlehf /usr/local/bin/bootstrap-kindlehf
RUN chmod 0755 /usr/local/bin/bootstrap-kindlehf

WORKDIR /workspace
