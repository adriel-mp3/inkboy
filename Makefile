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

COMMON_CFLAGS := -std=c11 -Wall -Wextra -Wpedantic -O2
APP_SOURCES   := src/main.c src/layout.c src/render.c src/draw.c src/input.c src/libretro.c src/frame.c src/profile.c
APP_OBJECTS   := $(addprefix build/obj/$(TARGET)/,$(APP_SOURCES:src/%.c=%.o))
TEST_SOURCE   := tests/test_layout.c
TEST_OUTPUT   := build/test-layout
LIBRETRO_TEST_SOURCE := tests/test_libretro.c
LIBRETRO_TEST_OUTPUT := build/test-libretro
LIBRETRO_RUNTIME_TEST_SOURCE := tests/test_libretro_runtime.c
LIBRETRO_RUNTIME_TEST_OUTPUT := build/test-libretro-runtime
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
LIBRETRO_LIVE_SOURCE := tools/libretro_live.c
LIBRETRO_LIVE_HOST := build/libretro_live-host
LIBRETRO_LIVE_KINDLE := build/libretro_live
LIBRETRO_SCALE_TEST_SOURCE := tools/libretro_scale_test.c
LIBRETRO_SCALE_TEST := build/libretro_scale-test
TOUCH_PROBE_SOURCE := tools/touch_probe.c
ASSET_HEADER  := src/duck_bitmap.h
ASSET_TOOL    := build/svg_to_bitmap
FBINK_DIR     := third_party/fbink
FBINK_VERSION := 886f25f
FBINK_SOURCES := fbink.c cutef8/utf8.c cutef8/dfa.c qimagescale/qimagescale.c
FBINK_OBJECTS := $(addprefix build/fbink/$(TARGET)/,$(FBINK_SOURCES:.c=.o))
HOST_CC       ?= gcc
TASK009_FULL_OBJECTS := $(addprefix build/obj/task009-full/,$(APP_SOURCES:src/%.c=%.o))
TASK009_REGION_OBJECTS := $(addprefix build/obj/task009-region/,$(APP_SOURCES:src/%.c=%.o))
TASK009_CPPFLAGS := -Isrc -I$(FBINK_DIR) -D_POSIX_C_SOURCE=200809L \
	-DINKBOY_CORE_PATH='"/mnt/us/inkboy/task009-runtime/sameboy_libretro.so"'
TASK009_TESTS := build/test-input build/test-runtime build/test-frame build/test-render build/test-render-region build/test-profile build/test-touch-failure
AR            ?= ar
RANLIB        ?= ranlib

FBINK_CFLAGS  := -std=c11 -O2 -fno-semantic-interposition -ftree-vectorize \
	-funroll-loops -fno-common -fPIC -Wno-unused-function \
	-Wno-format -Wno-format-signedness -Wno-sign-conversion \
	-Wno-cast-align -Wno-conversion
FBINK_CPPFLAGS := -I$(FBINK_DIR) -D_REENTRANT=1 -D_GNU_SOURCE \
	-DFBINK_MINIMAL -DFBINK_WITH_IMAGE -DFBINK_WITH_DRAW \
	-DFBINK_VERSION=\"$(FBINK_VERSION)\"

.PHONY: all docker-image bootstrap docker-build docker-build-task008 docker-build-libretro-frame docker-build-libretro-live host host-libretro-frame host-libretro-live touch-probe asset test sameboy-core-host sameboy-core-kindle libretro-probe-host libretro-probe-kindle deploy-libretro-probe libretro-frame-host libretro-frame-kindle deploy-libretro-frame libretro-live-host libretro-live-kindle deploy-libretro-live libretro-scale-test libretro-runtime-test deploy-task008 clean inspect
.PHONY: task009-test task009-artifacts docker-build-task009 deploy-task009

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

docker-build-task008: docker-image
	@test -x "$(TOOLCHAIN)/arm-kindlehf-linux-gnueabihf/bin/arm-kindlehf-linux-gnueabihf-gcc" || \
		(echo "Kindle toolchain not found. Run 'make bootstrap' first." >&2; exit 2)
	$(DOCKER_ENV) $(DOCKER) run --rm --user "$$(id -u):$$(id -g)" \
		--volume "$(ROOT_DIR):/workspace" --volume "$(TOOLCHAIN):/opt/x-tools:ro" \
		--workdir /workspace $(IMAGE) make TARGET=kindle KINDLE_TC=$(KINDLE_TC) \
		KINDLE_SYSROOT=$(KINDLE_SYSROOT) KINDLE_CC=$(KINDLE_CC) \
		build/$(PROJECT) sameboy-core-kindle

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

host-libretro-live: docker-image
	$(DOCKER_ENV) $(DOCKER) run --rm --user "$$(id -u):$$(id -g)" \
		--volume "$(ROOT_DIR):/workspace" --workdir /workspace \
		$(IMAGE) make TARGET=host libretro-live-host

docker-build-libretro-live: docker-image
	@test -x "$(TOOLCHAIN)/arm-kindlehf-linux-gnueabihf/bin/arm-kindlehf-linux-gnueabihf-gcc" || \
		(echo "Kindle toolchain not found. Run 'make bootstrap' first." >&2; exit 2)
	$(DOCKER_ENV) $(DOCKER) run --rm --user "$$(id -u):$$(id -g)" \
		--volume "$(ROOT_DIR):/workspace" --volume "$(TOOLCHAIN):/opt/x-tools:ro" \
		--workdir /workspace $(IMAGE) make TARGET=kindle KINDLE_TC=$(KINDLE_TC) \
		KINDLE_SYSROOT=$(KINDLE_SYSROOT) KINDLE_CC=$(KINDLE_CC) libretro-live-kindle

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

$(LIBRETRO_LIVE_HOST): $(LIBRETRO_LIVE_SOURCE) $(SAMEBOY_CORE_HOST) build/libfbink-host.a
	@mkdir -p $(BUILD_DIR)
	$(HOST_CC) $(COMMON_CFLAGS) -D_POSIX_C_SOURCE=200809L -I$(SAMEBOY_DIR)/libretro -I$(FBINK_DIR) $< build/libfbink-host.a -ldl -lm -o $@

$(LIBRETRO_LIVE_KINDLE): $(LIBRETRO_LIVE_SOURCE) $(SAMEBOY_CORE_KINDLE) build/libfbink-kindle.a
	@mkdir -p $(BUILD_DIR)
	$(KINDLE_CC) $(COMMON_CFLAGS) -D_POSIX_C_SOURCE=200809L -I$(SAMEBOY_DIR)/libretro -I$(FBINK_DIR) $< build/libfbink-kindle.a -ldl -lm -o $@

libretro-live-host: $(LIBRETRO_LIVE_HOST)
libretro-live-kindle: $(LIBRETRO_LIVE_KINDLE)

$(LIBRETRO_SCALE_TEST): $(LIBRETRO_SCALE_TEST_SOURCE) $(LIBRETRO_LIVE_SOURCE) build/libfbink-host.a
	@mkdir -p $(BUILD_DIR)
	$(HOST_CC) $(COMMON_CFLAGS) -D_POSIX_C_SOURCE=200809L \
		-I$(SAMEBOY_DIR)/libretro -I$(FBINK_DIR) $< build/libfbink-host.a -ldl -lm -o $@

libretro-scale-test: $(LIBRETRO_SCALE_TEST)
	@$(LIBRETRO_SCALE_TEST)

deploy-libretro-probe: libretro-probe-kindle
	@mkdir -p build/libretro-probe-deploy
	cp $(LIBRETRO_PROBE_KINDLE) $(SAMEBOY_CORE_KINDLE) build/libretro-probe-deploy/

deploy-libretro-frame: libretro-frame-kindle
	@mkdir -p build/libretro-frame-deploy
	cp $(LIBRETRO_FRAME_KINDLE) $(SAMEBOY_CORE_KINDLE) build/libretro-frame-deploy/

deploy-libretro-live: libretro-live-kindle
	@mkdir -p build/libretro-live-deploy
	cp $(LIBRETRO_LIVE_KINDLE) $(SAMEBOY_CORE_KINDLE) build/libretro-live-deploy/

asset: $(ASSET_HEADER)

test: $(TEST_OUTPUT) $(LIBRETRO_TEST_OUTPUT) task009-test
	@$(TEST_OUTPUT)
	@$(LIBRETRO_TEST_OUTPUT)

$(TEST_OUTPUT): $(TEST_SOURCE) src/layout.c src/layout.h
	@mkdir -p $(BUILD_DIR)
	$(HOST_CC) $(COMMON_CFLAGS) -Isrc $(TEST_SOURCE) src/layout.c -o $@

$(LIBRETRO_TEST_OUTPUT): $(LIBRETRO_TEST_SOURCE) src/libretro.c src/profile.c src/profile.h src/libretro.h src/frame.h src/layout.h
	@mkdir -p $(BUILD_DIR)
	$(HOST_CC) $(COMMON_CFLAGS) -Isrc -I$(SAMEBOY_DIR)/libretro \
		$(LIBRETRO_TEST_SOURCE) src/libretro.c src/profile.c -ldl -o $@

libretro-runtime-test: $(LIBRETRO_RUNTIME_TEST_OUTPUT) $(SAMEBOY_CORE_HOST)
	@$(LIBRETRO_RUNTIME_TEST_OUTPUT) $(SAMEBOY_CORE_HOST) \
		$(SAMEBOY_DIR)/.github/actions/dmg-acid2.gb

deploy-task008: docker-build-task008
	@mkdir -p build/task008-deploy
	cp build/$(PROJECT) $(SAMEBOY_CORE_KINDLE) build/task008-deploy/

$(LIBRETRO_RUNTIME_TEST_OUTPUT): $(LIBRETRO_RUNTIME_TEST_SOURCE) src/libretro.c src/profile.c src/profile.h src/libretro.h src/frame.h src/layout.h
	@mkdir -p $(BUILD_DIR)
	$(HOST_CC) $(COMMON_CFLAGS) -Isrc -I$(SAMEBOY_DIR)/libretro \
		$(LIBRETRO_RUNTIME_TEST_SOURCE) src/libretro.c src/profile.c -ldl -o $@

task009-test: $(TASK009_TESTS)
	@set -e; for test_binary in $(TASK009_TESTS); do \
		if $$test_binary > "$$test_binary.log" 2>&1; then \
			tail -n 1 "$$test_binary.log"; \
		else cat "$$test_binary.log"; exit 1; fi; \
	done

build/test-input: tests/test_input.c src/input.c src/input.h src/layout.c src/layout.h
	@mkdir -p $(BUILD_DIR)
	$(HOST_CC) $(COMMON_CFLAGS) -D_POSIX_C_SOURCE=200809L -Isrc $< src/layout.c -o $@

build/test-runtime: tests/test_runtime.c src/main.c src/profile.c $(wildcard src/*.h)
	@mkdir -p $(BUILD_DIR)
	$(HOST_CC) $(COMMON_CFLAGS) -D_POSIX_C_SOURCE=200809L -Isrc $< src/profile.c -o $@

build/test-frame: tests/test_frame.c src/frame.c src/frame.h src/layout.c src/layout.h
	@mkdir -p $(BUILD_DIR)
	$(HOST_CC) $(COMMON_CFLAGS) -Isrc $< src/frame.c src/layout.c -o $@

build/test-render: tests/test_render.c src/render.c src/draw.c src/frame.c src/profile.c src/layout.c $(wildcard src/*.h)
	@mkdir -p $(BUILD_DIR)
	$(HOST_CC) $(COMMON_CFLAGS) -Isrc -I$(FBINK_DIR) $< src/render.c src/draw.c src/frame.c src/profile.c src/layout.c -o $@

build/test-render-region: tests/test_render.c src/render.c src/draw.c src/frame.c src/profile.c src/layout.c $(wildcard src/*.h)
	@mkdir -p $(BUILD_DIR)
	$(HOST_CC) $(COMMON_CFLAGS) -DINKBOY_PROFILE_GAME_REGION -Isrc -I$(FBINK_DIR) $< src/render.c src/draw.c src/frame.c src/profile.c src/layout.c -o $@

build/test-profile: tests/test_profile.c src/profile.c src/profile.h
	@mkdir -p $(BUILD_DIR)
	$(HOST_CC) $(COMMON_CFLAGS) -Isrc $< src/profile.c -o $@

build/test-touch-failure: tests/test_touch_failure.c tools/touch_failure.c
	@mkdir -p $(BUILD_DIR)
	$(HOST_CC) $(COMMON_CFLAGS) $< -ldl -o $@

# Diagnostic builds have independent objects and a fixed isolated core path.
$(TASK009_FULL_OBJECTS) $(TASK009_REGION_OBJECTS): $(ASSET_HEADER)
build/obj/task009-full/%.o: src/%.c $(wildcard src/*.h)
	@mkdir -p $(dir $@)
	$(KINDLE_CC) $(COMMON_CFLAGS) $(TASK009_CPPFLAGS) -c $< -o $@

build/obj/task009-region/%.o: src/%.c $(wildcard src/*.h)
	@mkdir -p $(dir $@)
	$(KINDLE_CC) $(COMMON_CFLAGS) $(TASK009_CPPFLAGS) -DINKBOY_PROFILE_GAME_REGION -c $< -o $@

build/task009/inkboy: $(TASK009_FULL_OBJECTS) build/libfbink-kindle.a
	@mkdir -p $(dir $@)
	$(KINDLE_CC) $(COMMON_CFLAGS) $^ -ldl -lm -o $@

build/task009/inkboy-game-region: $(TASK009_REGION_OBJECTS) build/libfbink-kindle.a
	@mkdir -p $(dir $@)
	$(KINDLE_CC) $(COMMON_CFLAGS) $^ -ldl -lm -o $@

build/task009/touch-failure.so: tools/touch_failure.c
	@mkdir -p $(dir $@)
	$(KINDLE_CC) $(COMMON_CFLAGS) -fPIC -shared $< -ldl -o $@

build/task009/power_probe: tools/power_probe.c
	@mkdir -p $(dir $@)
	$(KINDLE_CC) $(COMMON_CFLAGS) $< -o $@

task009-artifacts: build/task009/inkboy build/task009/inkboy-game-region build/task009/touch-failure.so build/task009/power_probe $(SAMEBOY_CORE_KINDLE)
	cp $(SAMEBOY_CORE_KINDLE) build/task009/sameboy_libretro.so
	cd build/task009 && sha256sum inkboy inkboy-game-region touch-failure.so power_probe sameboy_libretro.so > SHA256SUMS

docker-build-task009: docker-image
	@test -x "$(TOOLCHAIN)/arm-kindlehf-linux-gnueabihf/bin/arm-kindlehf-linux-gnueabihf-gcc" || \
		(echo "Kindle toolchain not found. Run 'make bootstrap' first." >&2; exit 2)
	$(DOCKER_ENV) $(DOCKER) run --rm --user "$$(id -u):$$(id -g)" \
		--volume "$(ROOT_DIR):/workspace" --volume "$(TOOLCHAIN):/opt/x-tools:ro" \
		--workdir /workspace $(IMAGE) make TARGET=kindle KINDLE_TC=$(KINDLE_TC) \
		KINDLE_SYSROOT=$(KINDLE_SYSROOT) KINDLE_CC=$(KINDLE_CC) task009-artifacts

deploy-task009: docker-build-task009
	@mkdir -p build/task009-deploy
	cp build/task009/inkboy build/task009/inkboy-game-region build/task009/touch-failure.so \
		build/task009/power_probe build/task009/sameboy_libretro.so build/task009/SHA256SUMS build/task009-deploy/

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

build/obj/$(TARGET)/%.o: src/%.c $(wildcard src/*.h)
	@mkdir -p $(dir $@)
	$(CC) $(COMMON_CFLAGS) -D_POSIX_C_SOURCE=200809L -Isrc -I$(FBINK_DIR) -c $< -o $@

build/fbink/$(TARGET)/%.o: $(FBINK_DIR)/%.c
	@mkdir -p $(dir $@)
	$(CC) $(FBINK_CPPFLAGS) $(FBINK_PLATFORM) $(FBINK_CFLAGS) \
		-c $< -o $@

build/libfbink-$(TARGET).a: $(FBINK_OBJECTS)
	@mkdir -p $(dir $@)
	$(AR) rcs $@ $^
	$(RANLIB) $@

$(OUTPUT): $(APP_OBJECTS) $(ASSET_HEADER) build/libfbink-$(TARGET).a
	@mkdir -p $(BUILD_DIR)
	$(CC) $(COMMON_CFLAGS) -D_POSIX_C_SOURCE=200809L -Isrc -I$(FBINK_DIR) $(APP_OBJECTS) \
		build/libfbink-$(TARGET).a -ldl -lm -o $@

$(TOUCH_PROBE_OUTPUT): $(TOUCH_PROBE_SOURCE)
	@mkdir -p $(BUILD_DIR)
	$(CC) $(COMMON_CFLAGS) $< -o $@

touch-probe: $(TOUCH_PROBE_OUTPUT)

inspect: $(INSPECT_BINARY)
	file $(INSPECT_BINARY)
	readelf -l $(INSPECT_BINARY) | grep 'Requesting program interpreter' || true
	readelf -d $(INSPECT_BINARY) | grep 'NEEDED' || true

clean:
	rm -rf $(BUILD_DIR)

# TASK-010 never rebuilds or stages TASK-009 executables. 8 FPS is buildable
# for exploration; running it is gated by the physical 4 FPS acceptance criteria.
TASK010_PROFILES := baseline $(foreach mode,auto gc16,$(foreach fps,1 2 4 8,$(foreach clean,30 60,$(mode)-$(fps)-$(clean))))
TASK010_FLAGS_baseline :=
$(foreach fps,1 2 4 8,$(foreach clean,30 60,$(eval TASK010_FLAGS_auto-$(fps)-$(clean) := -DINKBOY_EXPERIMENT_FPS=$(fps) -DINKBOY_EXPERIMENT_MODE=INKBOY_REFRESH_REGION_AUTO -DINKBOY_EXPERIMENT_CLEAN_SECONDS=$(clean))))
$(foreach fps,1 2 4 8,$(foreach clean,30 60,$(eval TASK010_FLAGS_gc16-$(fps)-$(clean) := -DINKBOY_EXPERIMENT_FPS=$(fps) -DINKBOY_EXPERIMENT_MODE=INKBOY_REFRESH_REGION_GC16 -DINKBOY_EXPERIMENT_CLEAN_SECONDS=$(clean))))
TASK010_CPPFLAGS := -Isrc -I$(FBINK_DIR) -D_POSIX_C_SOURCE=200809L \
	-DINKBOY_CORE_PATH='"/mnt/us/inkboy/task010-runtime/sameboy_libretro.so"'
TASK010_TESTS := build/task010/test-runtime-4 $(foreach mode,auto gc16,$(foreach clean,30 60,build/task010/test-$(mode)-4-$(clean)))

# Separate objects for each platform/profile prevent stale compile-time policy.
define TASK010_template
build/obj/task010/$(1)/$(2)/%.o: src/%.c $$(wildcard src/*.h) $$(ASSET_HEADER)
	@mkdir -p $$(dir $$@)
	$$(CC) $$(COMMON_CFLAGS) $$(TASK010_CPPFLAGS) $$(TASK010_FLAGS_$(2)) -c $$< -o $$@

build/task010/$(1)/inkboy-$(2): $$(addprefix build/obj/task010/$(1)/$(2)/,$$(APP_SOURCES:src/%.c=%.o)) build/libfbink-$(1).a
	@mkdir -p $$(dir $$@)
	$$(CC) $$(COMMON_CFLAGS) $$^ -ldl -lm -o $$@
endef
$(foreach profile,$(TASK010_PROFILES),$(eval $(call TASK010_template,$(TARGET),$(profile))))

define TASK010_test_template
build/task010/test-$(1): tests/test_render.c src/render.c src/draw.c src/frame.c src/profile.c src/layout.c $$(wildcard src/*.h)
	@mkdir -p $$(dir $$@)
	$$(HOST_CC) $$(COMMON_CFLAGS) $$(TASK010_FLAGS_$(1)) -Isrc -I$$(FBINK_DIR) $$< src/draw.c src/frame.c src/profile.c src/layout.c -o $$@
endef
$(foreach mode,auto gc16,$(foreach clean,30 60,$(eval $(call TASK010_test_template,$(mode)-4-$(clean)))))

.PHONY: task010-test task010-artifacts docker-build-task010 docker-host-task010 deploy-task010

build/task010/test-runtime-4: tests/test_runtime.c src/main.c src/profile.c $(wildcard src/*.h)
	@mkdir -p $(dir $@)
	$(HOST_CC) $(COMMON_CFLAGS) $(TASK010_FLAGS_auto-4-60) -D_POSIX_C_SOURCE=200809L -Isrc $< src/profile.c -o $@

task010-test: $(TASK010_TESTS)
	@set -e; for test_binary in $(TASK010_TESTS); do \
		if $$test_binary > "$$test_binary.log" 2>&1; then tail -n 1 "$$test_binary.log"; \
		else cat "$$test_binary.log"; exit 1; fi; done

test: task010-test

task010-artifacts: $(addprefix build/task010/kindle/inkboy-,$(TASK010_PROFILES)) $(SAMEBOY_CORE_KINDLE)
	cp $(SAMEBOY_CORE_KINDLE) build/task010/kindle/sameboy_libretro.so
	cd build/task010/kindle && sha256sum $(addprefix inkboy-,$(TASK010_PROFILES)) sameboy_libretro.so > SHA256SUMS

docker-build-task010: docker-image
	$(DOCKER_ENV) $(DOCKER) run --rm --user "$$(id -u):$$(id -g)" \
		--volume "$(ROOT_DIR):/workspace" --volume "$(TOOLCHAIN):/opt/x-tools:ro" \
		--workdir /workspace $(IMAGE) make TARGET=kindle KINDLE_TC=$(KINDLE_TC) \
		KINDLE_SYSROOT=$(KINDLE_SYSROOT) KINDLE_CC=$(KINDLE_CC) task010-artifacts

docker-host-task010: docker-image
	$(DOCKER_ENV) $(DOCKER) run --rm --user "$$(id -u):$$(id -g)" \
		--volume "$(ROOT_DIR):/workspace" --workdir /workspace $(IMAGE) \
		make TARGET=host $(addprefix -W ,$(wildcard tests/*.c) tools/libretro_scale_test.c) \
		test libretro-runtime-test libretro-scale-test \
		$(addprefix build/task010/host/inkboy-,$(TASK010_PROFILES))

deploy-task010: docker-build-task010
	@mkdir -p build/task010-deploy
	cp build/task010/kindle/inkboy-* build/task010/kindle/sameboy_libretro.so \
		build/task010/kindle/SHA256SUMS build/task010-deploy/
