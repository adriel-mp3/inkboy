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
	$(call DOCKER_MAKE,$(DOCKER_TOOLCHAIN_VOLUME),TARGET=kindle KINDLE_TC=$(KINDLE_TC) KINDLE_SYSROOT=$(KINDLE_SYSROOT) KINDLE_CC=$(KINDLE_CC) build/$(PROJECT) build/touch_probe build/libretro_probe)

docker-build-task008: docker-image
	@test -x "$(TOOLCHAIN)/arm-kindlehf-linux-gnueabihf/bin/arm-kindlehf-linux-gnueabihf-gcc" || \
		(echo "Kindle toolchain not found. Run 'make bootstrap' first." >&2; exit 2)
	$(call DOCKER_MAKE,$(DOCKER_TOOLCHAIN_VOLUME),TARGET=kindle KINDLE_TC=$(KINDLE_TC) KINDLE_SYSROOT=$(KINDLE_SYSROOT) KINDLE_CC=$(KINDLE_CC) build/$(PROJECT) sameboy-core-kindle)

# A host build is only a source/UI smoke test. It does not establish Kindle
# ABI or runtime compatibility.
host: docker-image
	$(call DOCKER_MAKE,,TARGET=host build/$(PROJECT)-host build/touch_probe-host build/libretro_probe-host)

host-libretro-frame: docker-image
	$(call DOCKER_MAKE,,TARGET=host libretro-frame-host)

host-libretro-live: docker-image
	$(call DOCKER_MAKE,,TARGET=host libretro-live-host)

docker-build-libretro-live: docker-image
	@test -x "$(TOOLCHAIN)/arm-kindlehf-linux-gnueabihf/bin/arm-kindlehf-linux-gnueabihf-gcc" || \
		(echo "Kindle toolchain not found. Run 'make bootstrap' first." >&2; exit 2)
	$(call DOCKER_MAKE,$(DOCKER_TOOLCHAIN_VOLUME),TARGET=kindle KINDLE_TC=$(KINDLE_TC) KINDLE_SYSROOT=$(KINDLE_SYSROOT) KINDLE_CC=$(KINDLE_CC) libretro-live-kindle)

docker-build-libretro-frame: docker-image
	@test -x "$(TOOLCHAIN)/arm-kindlehf-linux-gnueabihf/bin/arm-kindlehf-linux-gnueabihf-gcc" || \
		(echo "Kindle toolchain not found. Run 'make bootstrap' first." >&2; exit 2)
	$(call DOCKER_MAKE,$(DOCKER_TOOLCHAIN_VOLUME),TARGET=kindle KINDLE_TC=$(KINDLE_TC) KINDLE_SYSROOT=$(KINDLE_SYSROOT) KINDLE_CC=$(KINDLE_CC) libretro-frame-kindle)
