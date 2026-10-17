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
