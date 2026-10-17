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
