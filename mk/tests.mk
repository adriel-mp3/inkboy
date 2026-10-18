PROFILE_TEST_NAMES := $(foreach mode,auto gc16,$(foreach clean,30 60,$(mode)-4-$(clean)))
$(foreach clean,30 60,$(eval PROFILE_TEST_FLAGS_auto-4-$(clean) := -DINKBOY_EXPERIMENT_FPS=4 -DINKBOY_EXPERIMENT_MODE=INKBOY_REFRESH_REGION_AUTO -DINKBOY_EXPERIMENT_CLEAN_SECONDS=$(clean)))
$(foreach clean,30 60,$(eval PROFILE_TEST_FLAGS_gc16-4-$(clean) := -DINKBOY_EXPERIMENT_FPS=4 -DINKBOY_EXPERIMENT_MODE=INKBOY_REFRESH_REGION_GC16 -DINKBOY_EXPERIMENT_CLEAN_SECONDS=$(clean)))
PROFILE_TEST_BINS := $(addprefix build/test-render-,$(PROFILE_TEST_NAMES))
TEST_BINS := $(TEST_OUTPUT) $(LIBRETRO_TEST_OUTPUT) build/test-input build/test-runtime \
	build/test-frame build/test-render build/test-profile $(PROFILE_TEST_BINS)

test: $(TEST_BINS)
	@set -e; for test_binary in $(TEST_BINS); do \
		if $$test_binary > "$$test_binary.log" 2>&1; then tail -n 1 "$$test_binary.log"; \
		else cat "$$test_binary.log"; exit 1; fi; done
	python3 tests/test_profile_report.py
	python3 tests/test_deploy.py

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

$(LIBRETRO_RUNTIME_TEST_OUTPUT): $(LIBRETRO_RUNTIME_TEST_SOURCE) src/libretro.c src/profile.c src/profile.h src/libretro.h src/frame.h src/layout.h
	@mkdir -p $(BUILD_DIR)
	$(HOST_CC) $(COMMON_CFLAGS) -Isrc -I$(SAMEBOY_DIR)/libretro \
		$(LIBRETRO_RUNTIME_TEST_SOURCE) src/libretro.c src/profile.c -ldl -o $@

build/test-input: tests/test_input.c src/input.c src/input.h src/layout.c src/layout.h
	@mkdir -p $(BUILD_DIR)
	$(HOST_CC) $(COMMON_CFLAGS) -D_POSIX_C_SOURCE=200809L -Isrc $< src/layout.c -o $@

build/test-runtime: tests/test_runtime.c src/main.c src/profile.c src/timing.c $(wildcard src/*.h)
	@mkdir -p $(BUILD_DIR)
	$(HOST_CC) $(COMMON_CFLAGS) -D_POSIX_C_SOURCE=200809L -Isrc $< src/profile.c src/timing.c -o $@

build/test-frame: tests/test_frame.c src/frame.c src/frame.h src/layout.c src/layout.h
	@mkdir -p $(BUILD_DIR)
	$(HOST_CC) $(COMMON_CFLAGS) -Isrc $< src/frame.c src/layout.c -o $@

build/test-render: tests/test_render.c src/render.c src/draw.c src/frame.c src/profile.c src/layout.c $(wildcard src/*.h)
	@mkdir -p $(BUILD_DIR)
	$(HOST_CC) $(COMMON_CFLAGS) -Isrc -I$(FBINK_DIR) $< src/render.c src/draw.c src/frame.c src/profile.c src/layout.c -o $@

define PROFILE_TEST_template
build/test-render-$(1): tests/test_render.c src/render.c src/draw.c src/frame.c src/profile.c src/layout.c $$(wildcard src/*.h)
	@mkdir -p $$(dir $$@)
	$$(HOST_CC) $$(COMMON_CFLAGS) $$(PROFILE_TEST_FLAGS_$(1)) -Isrc -I$$(FBINK_DIR) $$< src/draw.c src/frame.c src/profile.c src/layout.c -o $$@
endef
$(foreach profile,$(PROFILE_TEST_NAMES),$(eval $(call PROFILE_TEST_template,$(profile))))

build/test-profile: tests/test_profile.c src/profile.c src/profile.h
	@mkdir -p $(BUILD_DIR)
	$(HOST_CC) $(COMMON_CFLAGS) -Isrc $< src/profile.c -o $@
