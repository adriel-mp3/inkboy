ifeq ($(TARGET),host)
CC           := gcc
OUTPUT       := build/$(PROJECT)-host
FBINK_PLATFORM := -DFBINK_FOR_LINUX
else
CC           := $(KINDLE_CC)
OUTPUT       := build/$(PROJECT)
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

$(OUTPUT): $(APP_OBJECTS) build/libfbink-$(TARGET).a
	@mkdir -p $(BUILD_DIR)
	$(CC) $(COMMON_CFLAGS) -D_POSIX_C_SOURCE=200809L -Isrc -I$(FBINK_DIR) $(APP_OBJECTS) \
		build/libfbink-$(TARGET).a -ldl -lm -o $@

inspect: docker-image
	$(call DOCKER_MAKE,$(DOCKER_TOOLCHAIN_VOLUME),TARGET=kindle KINDLE_TC=/opt/x-tools/arm-kindlehf-linux-gnueabihf KINDLE_SYSROOT=/opt/x-tools/arm-kindlehf-linux-gnueabihf/arm-kindlehf-linux-gnueabihf/sysroot KINDLE_CC=/opt/x-tools/arm-kindlehf-linux-gnueabihf/bin/arm-kindlehf-linux-gnueabihf-gcc INSPECT_BINARY=$(INSPECT_BINARY) inspect-local)

.PHONY: inspect-local
inspect-local: $(INSPECT_BINARY)
	file $(INSPECT_BINARY)
	readelf -l $(INSPECT_BINARY) | grep 'Requesting program interpreter' || true
	readelf -d $(INSPECT_BINARY) | grep 'NEEDED' || true

clean:
	rm -rf $(BUILD_DIR)
