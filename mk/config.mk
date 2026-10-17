PROJECT      := inkboy
IMAGE       ?= inkboy-build:0.1
ROOT_DIR     := $(abspath .)
BUILD_DIR    := $(ROOT_DIR)/build
INSPECT_BINARY ?= build/$(PROJECT)
TOOLCHAIN    ?= $(ROOT_DIR)/.toolchain
SDK_DIR      ?= $(ROOT_DIR)/.sdk
DOCKER_CONFIG ?= $(ROOT_DIR)/.docker
DOCKER_ENV    := DOCKER_CONFIG=$(DOCKER_CONFIG)
DOCKER        ?= docker

KINDLE_TC    ?= /opt/x-tools/arm-kindlehf-linux-gnueabihf
KINDLE_SYSROOT ?= $(KINDLE_TC)/arm-kindlehf-linux-gnueabihf/sysroot
KINDLE_CC   ?= $(KINDLE_TC)/bin/arm-kindlehf-linux-gnueabihf-gcc
KINDLE_REMOTE_ROOT ?= /mnt/us/inkboy
KINDLE_SSH_ALIAS ?= kindle
export KINDLE_REMOTE_ROOT KINDLE_SSH_ALIAS

COMMON_CFLAGS := -std=c11 -Wall -Wextra -Wpedantic -O2
APP_SOURCES   := src/main.c src/layout.c src/render.c src/draw.c src/input.c src/libretro.c src/frame.c src/profile.c
APP_OBJECTS   := $(addprefix build/obj/$(TARGET)/,$(APP_SOURCES:src/%.c=%.o))
TEST_SOURCE   := tests/test_layout.c
TEST_OUTPUT   := build/test-layout
LIBRETRO_TEST_SOURCE := tests/test_libretro.c
LIBRETRO_TEST_OUTPUT := build/test-libretro
LIBRETRO_RUNTIME_TEST_SOURCE := tests/test_libretro_runtime.c
LIBRETRO_RUNTIME_TEST_OUTPUT := build/test-libretro-runtime
SAMEBOY_DIR   := third_party/sameboy
SAMEBOY_CORE_HOST := build/sameboy/sameboy_libretro.so
SAMEBOY_CORE_KINDLE := build/sameboy-kindle/sameboy_libretro.so
SAMEBOY_WORK_HOST := build/sameboy-work-host
SAMEBOY_WORK_KINDLE := build/sameboy-work-kindle
LIBRETRO_LIVE_SOURCE := tools/libretro_live.c
LIBRETRO_SCALE_TEST_SOURCE := tools/libretro_scale_test.c
LIBRETRO_SCALE_TEST := build/libretro_scale-test
ASSET_HEADER  := src/duck_bitmap.h
ASSET_TOOL    := build/svg_to_bitmap
FBINK_DIR     := third_party/fbink
FBINK_VERSION := 886f25f
FBINK_SOURCES := fbink.c cutef8/utf8.c cutef8/dfa.c qimagescale/qimagescale.c
FBINK_OBJECTS := $(addprefix build/fbink/$(TARGET)/,$(FBINK_SOURCES:.c=.o))
HOST_CC       ?= gcc
AR            ?= ar
RANLIB        ?= ranlib

FBINK_CFLAGS  := -std=c11 -O2 -fno-semantic-interposition -ftree-vectorize \
	-funroll-loops -fno-common -fPIC -Wno-unused-function \
	-Wno-format -Wno-format-signedness -Wno-sign-conversion \
	-Wno-cast-align -Wno-conversion
FBINK_CPPFLAGS := -I$(FBINK_DIR) -D_REENTRANT=1 -D_GNU_SOURCE \
	-DFBINK_MINIMAL -DFBINK_WITH_IMAGE -DFBINK_WITH_DRAW \
	-DFBINK_VERSION=\"$(FBINK_VERSION)\"

DOCKER_TOOLCHAIN_VOLUME := --volume "$(TOOLCHAIN):/opt/x-tools:ro"
define DOCKER_MAKE
$(DOCKER_ENV) $(DOCKER) run --rm --user "$$(id -u):$$(id -g)" \
	--volume "$(ROOT_DIR):/workspace" $(1) --workdir /workspace \
	$(IMAGE) make $(2)
endef

.PHONY: all docker-image bootstrap docker-build kindle host asset test sameboy-core-host sameboy-core-kindle libretro-runtime-test libretro-scale-test clean inspect
