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

task010-artifacts: $(addprefix build/task010/kindle/inkboy-,$(TASK010_PROFILES)) $(SAMEBOY_CORE_KINDLE)
	cp $(SAMEBOY_CORE_KINDLE) build/task010/kindle/sameboy_libretro.so
	cd build/task010/kindle && sha256sum $(addprefix inkboy-,$(TASK010_PROFILES)) sameboy_libretro.so > SHA256SUMS

docker-build-task010: docker-image
	$(call DOCKER_MAKE,$(DOCKER_TOOLCHAIN_VOLUME),TARGET=kindle KINDLE_TC=$(KINDLE_TC) KINDLE_SYSROOT=$(KINDLE_SYSROOT) KINDLE_CC=$(KINDLE_CC) task010-artifacts)

docker-host-task010: docker-image
	$(call DOCKER_MAKE,,$(TASK010_HOST_GOALS))

TASK010_HOST_GOALS := TARGET=host $(addprefix -W ,$(wildcard tests/*.c) tools/libretro_scale_test.c) test libretro-runtime-test libretro-scale-test $(addprefix build/task010/host/inkboy-,$(TASK010_PROFILES))

deploy-task010: docker-build-task010
	@mkdir -p build/task010-deploy
	cp build/task010/kindle/inkboy-* build/task010/kindle/sameboy_libretro.so \
		build/task010/kindle/SHA256SUMS build/task010-deploy/
