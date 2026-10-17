# Stable developer-facing commands and isolated profiling packages.

PROFILE ?= baseline
RUN_ID ?= $(shell date -u +%Y%m%dT%H%M%SZ)-$(PROFILE)
REMOTE_RUNS_DIR ?= $(KINDLE_REMOTE_ROOT)/runs
PROFILE_DIR := build/profiles/$(RUN_ID)/$(PROFILE)
PROFILE_REMOTE_DIR := $(REMOTE_RUNS_DIR)/$(RUN_ID)
PROFILE_APP := $(PROFILE_DIR)/inkboy
PROFILE_OBJECTS := $(addprefix build/obj/profiles/$(RUN_ID)/$(PROFILE)/,$(APP_SOURCES:src/%.c=%.o))
PROFILE_CPPFLAGS := -Isrc -I$(FBINK_DIR) -D_POSIX_C_SOURCE=200809L \
	-DINKBOY_CORE_PATH='"$(PROFILE_REMOTE_DIR)/sameboy_libretro.so"'

PROFILE_FLAGS_baseline :=
$(foreach mode,auto gc16,$(foreach fps,1 2 4 8,$(foreach clean,30 60,$(eval PROFILE_FLAGS_$(mode)-$(fps)-$(clean) := -DINKBOY_EXPERIMENT_FPS=$(fps) -DINKBOY_EXPERIMENT_MODE=$(if $(filter auto,$(mode)),INKBOY_REFRESH_REGION_AUTO,INKBOY_REFRESH_REGION_GC16) -DINKBOY_EXPERIMENT_CLEAN_SECONDS=$(clean)))))
PROFILE_NAMES := baseline $(foreach mode,auto gc16,$(foreach fps,1 2 4 8,$(foreach clean,30 60,$(mode)-$(fps)-$(clean))))
PROFILE_FLAGS := $(PROFILE_FLAGS_$(PROFILE))

.PHONY: kindle package profile-artifact validate-profile validate-run-id docker-profile-artifact deploy run collect stop
kindle: docker-build

profile-artifact: validate-profile validate-run-id $(PROFILE_APP)

validate-profile:
	@case " $(PROFILE_NAMES) " in *" $(PROFILE) "*) ;; *) echo "Unknown profile: $(PROFILE)" >&2; exit 2;; esac

validate-run-id:
	@case "$(RUN_ID)" in ''|*[!a-zA-Z0-9._-]*) echo "RUN_ID may contain only letters, digits, dot, underscore and dash" >&2; exit 2;; esac

build/obj/profiles/$(RUN_ID)/$(PROFILE)/%.o: src/%.c $(wildcard src/*.h)
	@mkdir -p $(dir $@)
	$(CC) $(COMMON_CFLAGS) $(PROFILE_CPPFLAGS) $(PROFILE_FLAGS) -c $< -o $@

$(PROFILE_APP): $(PROFILE_OBJECTS) build/libfbink-$(TARGET).a
	@mkdir -p $(dir $@)
	$(CC) $(COMMON_CFLAGS) $^ -ldl -lm -o $@

docker-profile-artifact: validate-profile validate-run-id docker-image
	@test -x "$(TOOLCHAIN)/arm-kindlehf-linux-gnueabihf/bin/arm-kindlehf-linux-gnueabihf-gcc" || \
		(echo "Kindle toolchain not found. Run 'make bootstrap' first." >&2; exit 2)
	$(call DOCKER_MAKE,$(DOCKER_TOOLCHAIN_VOLUME),TARGET=kindle PROFILE=$(PROFILE) RUN_ID=$(RUN_ID) REMOTE_RUNS_DIR=$(REMOTE_RUNS_DIR) KINDLE_TC=$(KINDLE_TC) KINDLE_SYSROOT=$(KINDLE_SYSROOT) KINDLE_CC=$(KINDLE_CC) profile-artifact sameboy-core-kindle)

package: docker-profile-artifact
	@case "$(RUN_ID)" in ''|*[!a-zA-Z0-9._-]*) echo "RUN_ID may contain only letters, digits, dot, underscore and dash" >&2; exit 2;; esac
	@test ! -e "build/packages/$(RUN_ID)" || { echo "Package already exists: build/packages/$(RUN_ID)" >&2; exit 2; }
	@mkdir -p "build/packages/$(RUN_ID)"
	cp "$(PROFILE_APP)" "build/packages/$(RUN_ID)/inkboy"
	cp "$(SAMEBOY_CORE_KINDLE)" "build/packages/$(RUN_ID)/sameboy_libretro.so"
	@{ printf 'run_id=%s\nprofile=%s\nremote_dir=%s\n' '$(RUN_ID)' '$(PROFILE)' '$(PROFILE_REMOTE_DIR)'; \
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

stop:
	./bin/deploy stop "$(RUN_ID)"
