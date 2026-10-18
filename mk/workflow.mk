# Stable developer-facing build, package, and deployment commands.

RUN_ID ?= $(shell date -u +%Y%m%dT%H%M%SZ)
REMOTE_RUNS_DIR ?= $(KINDLE_REMOTE_ROOT)/runs
RUN_DIR := build/runs/$(RUN_ID)
RUN_REMOTE_DIR := $(REMOTE_RUNS_DIR)/$(RUN_ID)
RUN_APP := $(RUN_DIR)/inkboy
RUN_OBJECTS := $(addprefix build/obj/runs/$(RUN_ID)/,$(APP_SOURCES:src/%.c=%.o))
RUN_CPPFLAGS := -Isrc -I$(FBINK_DIR) -D_POSIX_C_SOURCE=200809L \
	-DINKBOY_CORE_PATH='"$(RUN_REMOTE_DIR)/sameboy_libretro.so"'

.PHONY: kindle package app-artifact docker-app-artifact validate-run-id deploy run collect stop logs
kindle: docker-build

app-artifact: validate-run-id $(RUN_APP)

validate-run-id:
	@case "$(RUN_ID)" in ''|*[!a-zA-Z0-9._-]*) echo "RUN_ID may contain only letters, digits, dot, underscore and dash" >&2; exit 2;; esac

build/obj/runs/$(RUN_ID)/%.o: src/%.c $(wildcard src/*.h)
	@mkdir -p $(dir $@)
	$(CC) $(COMMON_CFLAGS) $(RUN_CPPFLAGS) -c $< -o $@

$(RUN_APP): $(RUN_OBJECTS) build/libfbink-$(TARGET).a
	@mkdir -p $(dir $@)
	$(CC) $(COMMON_CFLAGS) $^ -ldl -lm -o $@

docker-app-artifact: validate-run-id docker-image
	@test -x "$(TOOLCHAIN)/arm-kindlehf-linux-gnueabihf/bin/arm-kindlehf-linux-gnueabihf-gcc" || \
		(echo "Kindle toolchain not found. Run 'make bootstrap' first." >&2; exit 2)
	$(call DOCKER_MAKE,$(DOCKER_TOOLCHAIN_VOLUME),TARGET=kindle RUN_ID=$(RUN_ID) REMOTE_RUNS_DIR=$(REMOTE_RUNS_DIR) KINDLE_TC=$(KINDLE_TC) KINDLE_SYSROOT=$(KINDLE_SYSROOT) KINDLE_CC=$(KINDLE_CC) app-artifact sameboy-core-kindle)

package: docker-app-artifact
	@test ! -e "build/packages/$(RUN_ID)" || { echo "Package already exists: build/packages/$(RUN_ID)" >&2; exit 2; }
	@mkdir -p "build/packages/$(RUN_ID)"
	cp "$(RUN_APP)" "build/packages/$(RUN_ID)/inkboy"
	cp "$(SAMEBOY_CORE_KINDLE)" "build/packages/$(RUN_ID)/sameboy_libretro.so"
	@{ printf 'run_id=%s\npolicy=2-fps-regional-gc16-clean60\nremote_dir=%s\n' '$(RUN_ID)' '$(RUN_REMOTE_DIR)'; \
	  printf 'git_commit=%s\n' "$$(git rev-parse HEAD 2>/dev/null || printf unknown)"; \
	  printf 'git_dirty=%s\n' "$$(test -z "$$(git status --porcelain 2>/dev/null)" && printf false || printf true)"; \
	} > "build/packages/$(RUN_ID)/MANIFEST"
	@cd "build/packages/$(RUN_ID)" && sha256sum inkboy sameboy_libretro.so > SHA256SUMS

deploy: package
	./bin/deploy install "$(RUN_ID)"

run:
	./bin/deploy run "$(RUN_ID)"

collect:
	./bin/deploy collect "$(RUN_ID)"

logs:
	python3 tools/log_report.py

stop:
	./bin/deploy stop "$(RUN_ID)"
