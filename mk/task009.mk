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
	$(call DOCKER_MAKE,$(DOCKER_TOOLCHAIN_VOLUME),TARGET=kindle KINDLE_TC=$(KINDLE_TC) KINDLE_SYSROOT=$(KINDLE_SYSROOT) KINDLE_CC=$(KINDLE_CC) task009-artifacts)

deploy-task009: docker-build-task009
	@mkdir -p build/task009-deploy
	cp build/task009/inkboy build/task009/inkboy-game-region build/task009/touch-failure.so \
		build/task009/power_probe build/task009/sameboy_libretro.so build/task009/SHA256SUMS build/task009-deploy/
