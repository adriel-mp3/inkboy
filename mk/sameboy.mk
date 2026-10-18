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
