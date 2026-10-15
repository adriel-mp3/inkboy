PROJECT      := inkboy
IMAGE       ?= inkboy-build:0.1
ROOT_DIR     := $(abspath .)
BUILD_DIR    := $(ROOT_DIR)/build
TOOLCHAIN    ?= $(ROOT_DIR)/.toolchain
SDK_DIR      ?= $(ROOT_DIR)/.sdk
DOCKER_CONFIG ?= $(ROOT_DIR)/.docker
DOCKER_ENV    := DOCKER_CONFIG=$(DOCKER_CONFIG)
DOCKER        ?= docker

KINDLE_TC    ?= /opt/x-tools/arm-kindlehf-linux-gnueabihf
KINDLE_SYSROOT ?= $(KINDLE_TC)/arm-kindlehf-linux-gnueabihf/sysroot
KINDLE_CC   ?= $(KINDLE_TC)/bin/arm-kindlehf-linux-gnueabihf-gcc

COMMON_CFLAGS := -std=c11 -Wall -Wextra -Wpedantic -O2
SOURCE        := src/main.c
LIBRETRO_PROBE_SOURCE := tools/libretro_probe.c
SAMEBOY_DIR   := third_party/sameboy
SAMEBOY_CORE_HOST := build/sameboy/sameboy_libretro.so
SAMEBOY_CORE_KINDLE := build/sameboy-kindle/sameboy_libretro.so
SAMEBOY_WORK_HOST := build/sameboy-work-host
SAMEBOY_WORK_KINDLE := build/sameboy-work-kindle
LIBRETRO_PROBE_HOST := build/libretro_probe-host
LIBRETRO_PROBE_KINDLE := build/libretro_probe
LIBRETRO_FRAME_SOURCE := tools/libretro_frame.c
LIBRETRO_FRAME_HOST := build/libretro_frame-host
LIBRETRO_FRAME_KINDLE := build/libretro_frame
TOUCH_PROBE_SOURCE := tools/touch_probe.c
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

.PHONY: all docker-image bootstrap docker-build docker-build-libretro-frame host host-libretro-frame touch-probe asset sameboy-core-host sameboy-core-kindle libretro-probe-host libretro-probe-kindle deploy-libretro-probe libretro-frame-host libretro-frame-kindle deploy-libretro-frame clean inspect

# The default is deliberately the isolated Kindle build. It will stop with a
# useful message until a Kindle SDK/sysroot has been bootstrapped.
all: docker-build

docker-image:
	mkdir -p $(DOCKER_CONFIG)
	$(DOCKER_ENV) $(DOCKER) build --tag $(IMAGE) --file docker/Dockerfile .

bootstrap: docker-image
	mkdir -p $(TOOLCHAIN) $(SDK_DIR)
	$(DOCKER_ENV) $(DOCKER) run --rm --privileged \
		--volume "$(TOOLCHAIN):/opt/x-tools" \
		--volume "$(SDK_DIR):/opt/kindle-sdk" \
		$(IMAGE) /usr/local/bin/bootstrap-kindlehf

docker-build: docker-image
	@test -x "$(TOOLCHAIN)/arm-kindlehf-linux-gnueabihf/bin/arm-kindlehf-linux-gnueabihf-gcc" || \
		(echo "Kindle toolchain not found. Run 'make bootstrap' first." >&2; exit 2)
	$(DOCKER_ENV) $(DOCKER) run --rm --user "$$(id -u):$$(id -g)" \
		--volume "$(ROOT_DIR):/workspace" \
		--volume "$(TOOLCHAIN):/opt/x-tools:ro" \
		--workdir /workspace \
		$(IMAGE) make TARGET=kindle KINDLE_TC=$(KINDLE_TC) \
		KINDLE_SYSROOT=$(KINDLE_SYSROOT) KINDLE_CC=$(KINDLE_CC) \
		build/$(PROJECT) build/touch_probe build/libretro_probe

# A host build is only a source/UI smoke test. It does not establish Kindle
# ABI or runtime compatibility.
host: docker-image
	$(DOCKER_ENV) $(DOCKER) run --rm --user "$$(id -u):$$(id -g)" \
		--volume "$(ROOT_DIR):/workspace" --workdir /workspace \
		$(IMAGE) make TARGET=host build/$(PROJECT)-host build/touch_probe-host build/libretro_probe-host

host-libretro-frame: docker-image
	$(DOCKER_ENV) $(DOCKER) run --rm --user "$$(id -u):$$(id -g)" \
		--volume "$(ROOT_DIR):/workspace" --workdir /workspace \
		$(IMAGE) make TARGET=host libretro-frame-host

docker-build-libretro-frame: docker-image
	@test -x "$(TOOLCHAIN)/arm-kindlehf-linux-gnueabihf/bin/arm-kindlehf-linux-gnueabihf-gcc" || \
		(echo "Kindle toolchain not found. Run 'make bootstrap' first." >&2; exit 2)
	$(DOCKER_ENV) $(DOCKER) run --rm --user "$$(id -u):$$(id -g)" \
		--volume "$(ROOT_DIR):/workspace" \
		--volume "$(TOOLCHAIN):/opt/x-tools:ro" \
		--workdir /workspace \
		$(IMAGE) make TARGET=kindle KINDLE_TC=$(KINDLE_TC) \
		KINDLE_SYSROOT=$(KINDLE_SYSROOT) KINDLE_CC=$(KINDLE_CC) \
		libretro-frame-kindle

sameboy-core-host: $(SAMEBOY_CORE_HOST)

$(SAMEBOY_CORE_HOST):
	@rm -rf $(SAMEBOY_WORK_HOST)
	@mkdir -p $(dir $(SAMEBOY_WORK_HOST))
	cp -a $(SAMEBOY_DIR)/. $(SAMEBOY_WORK_HOST)/
	@rm -rf $(SAMEBOY_WORK_HOST)/build $(SAMEBOY_WORK_HOST)/.git
	$(MAKE) -C $(SAMEBOY_WORK_HOST) bootroms BIN=$(abspath build/sameboy-work-host)
	$(MAKE) MAKEOVERRIDES= -C $(SAMEBOY_WORK_HOST) libretro platform=unix CC="$(HOST_CC)" BIN=$(abspath build/sameboy-work-host)
	@mkdir -p $(dir $@)
	cp $(SAMEBOY_WORK_HOST)/sameboy_libretro.so $@

sameboy-core-kindle: $(SAMEBOY_CORE_KINDLE)

$(SAMEBOY_CORE_KINDLE):
	@rm -rf $(SAMEBOY_WORK_KINDLE)
	@mkdir -p $(dir $(SAMEBOY_WORK_KINDLE))
	cp -a $(SAMEBOY_DIR)/. $(SAMEBOY_WORK_KINDLE)/
	@rm -rf $(SAMEBOY_WORK_KINDLE)/build $(SAMEBOY_WORK_KINDLE)/.git
	$(MAKE) -C $(SAMEBOY_WORK_KINDLE) bootroms BIN=$(abspath build/sameboy-work-kindle)
	$(MAKE) MAKEOVERRIDES= -C $(SAMEBOY_WORK_KINDLE) libretro platform=unix-armv7-hardfloat-neon CC="$(KINDLE_CC)" AR="$(KINDLE_TC)/bin/arm-kindlehf-linux-gnueabihf-ar" BIN=$(abspath build/sameboy-work-kindle)
	@mkdir -p $(dir $@)
	cp $(SAMEBOY_WORK_KINDLE)/sameboy_libretro.so $@

$(LIBRETRO_PROBE_HOST): $(LIBRETRO_PROBE_SOURCE) $(SAMEBOY_CORE_HOST)
	@mkdir -p $(BUILD_DIR)
	$(HOST_CC) $(COMMON_CFLAGS) -I$(SAMEBOY_DIR)/libretro $< -ldl -o $@

$(LIBRETRO_PROBE_KINDLE): $(LIBRETRO_PROBE_SOURCE) $(SAMEBOY_CORE_KINDLE)
	@mkdir -p $(BUILD_DIR)
	$(KINDLE_CC) $(COMMON_CFLAGS) -I$(SAMEBOY_DIR)/libretro $< -ldl -o $@

libretro-probe-host: $(LIBRETRO_PROBE_HOST)
libretro-probe-kindle: $(LIBRETRO_PROBE_KINDLE)

$(LIBRETRO_FRAME_HOST): $(LIBRETRO_FRAME_SOURCE) $(SAMEBOY_CORE_HOST) build/libfbink-host.a
	@mkdir -p $(BUILD_DIR)
	$(HOST_CC) $(COMMON_CFLAGS) -D_POSIX_C_SOURCE=200809L \
		-I$(SAMEBOY_DIR)/libretro -I$(FBINK_DIR) $< build/libfbink-host.a -ldl -lm -o $@

$(LIBRETRO_FRAME_KINDLE): $(LIBRETRO_FRAME_SOURCE) $(SAMEBOY_CORE_KINDLE) build/libfbink-kindle.a
	@mkdir -p $(BUILD_DIR)
	$(KINDLE_CC) $(COMMON_CFLAGS) -D_POSIX_C_SOURCE=200809L \
		-I$(SAMEBOY_DIR)/libretro -I$(FBINK_DIR) $< build/libfbink-kindle.a -ldl -lm -o $@

libretro-frame-host: $(LIBRETRO_FRAME_HOST)
libretro-frame-kindle: $(LIBRETRO_FRAME_KINDLE)

deploy-libretro-probe: libretro-probe-kindle
	@mkdir -p build/libretro-probe-deploy
	cp $(LIBRETRO_PROBE_KINDLE) $(SAMEBOY_CORE_KINDLE) build/libretro-probe-deploy/

deploy-libretro-frame: libretro-frame-kindle
	@mkdir -p build/libretro-frame-deploy
	cp $(LIBRETRO_FRAME_KINDLE) $(SAMEBOY_CORE_KINDLE) build/libretro-frame-deploy/

asset: $(ASSET_HEADER)

$(ASSET_TOOL): tools/svg_to_bitmap.c
	@mkdir -p $(dir $@)
	$(HOST_CC) -std=c11 -Wall -Wextra -Wpedantic \
		$$(pkg-config --cflags cairo gobject-2.0) $< \
		$$(pkg-config --libs cairo gobject-2.0) -ldl -o $@

$(ASSET_HEADER): assets/duck.svg $(ASSET_TOOL)
	$(ASSET_TOOL) $< $@ 256 256

ifeq ($(TARGET),host)
CC           := gcc
OUTPUT       := build/$(PROJECT)-host
TOUCH_PROBE_OUTPUT := build/touch_probe-host
FBINK_PLATFORM := -DFBINK_FOR_LINUX
else
CC           := $(KINDLE_CC)
OUTPUT       := build/$(PROJECT)
TOUCH_PROBE_OUTPUT := build/touch_probe
FBINK_PLATFORM := -DFBINK_FOR_KINDLE
endif

build/fbink/$(TARGET)/%.o: $(FBINK_DIR)/%.c
	@mkdir -p $(dir $@)
	$(CC) $(FBINK_CPPFLAGS) $(FBINK_PLATFORM) $(FBINK_CFLAGS) \
		-c $< -o $@

build/libfbink-$(TARGET).a: $(FBINK_OBJECTS)
	@mkdir -p $(dir $@)
	$(AR) rcs $@ $^
	$(RANLIB) $@

$(OUTPUT): $(SOURCE) $(ASSET_HEADER) build/libfbink-$(TARGET).a
	@mkdir -p $(BUILD_DIR)
	$(CC) $(COMMON_CFLAGS) -D_POSIX_C_SOURCE=200809L -Isrc -I$(FBINK_DIR) $(SOURCE) \
		build/libfbink-$(TARGET).a -lm -o $@

$(TOUCH_PROBE_OUTPUT): $(TOUCH_PROBE_SOURCE)
	@mkdir -p $(BUILD_DIR)
	$(CC) $(COMMON_CFLAGS) $< -o $@

touch-probe: $(TOUCH_PROBE_OUTPUT)

inspect: build/$(PROJECT)
	file build/$(PROJECT)
	readelf -l build/$(PROJECT) | grep 'Requesting program interpreter' || true
	readelf -d build/$(PROJECT) | grep 'NEEDED' || true

clean:
	rm -rf $(BUILD_DIR)
