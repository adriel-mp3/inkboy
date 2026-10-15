# Stage 5.2 — SameBoy Libretro probe

This experiment is an independent host/PW5 probe. It does not link FBInk,
touch input, audio output, or `src/main.c`. SameBoy is included as the official
Git submodule at `third_party/sameboy`, fixed to
`26a56bd6a122a6318a456abd80558408742d5adf` (upstream `LIJI32/SameBoy`).

The SameBoy project and its source retain their upstream licenses and notices;
the Libretro API header carries its own license. InkBoy does not copy SameBoy
source into the application. Review `third_party/sameboy/LICENSE` and
`third_party/sameboy/libretro/libretro.h` when redistributing the submodule.

## Build and run

The Docker image installs upstream RGBDS `0.9.4` from its GitHub source
tarball, verifies its SHA-256 checksum, and installs `rgbasm`, `rgblink`, and
`rgbgfx` in `/usr/local/bin`. RGBDS is a build prerequisite only, not a
runtime dependency. Rebuild the image after changing the pinned version or
checksum:

```sh
make docker-image
# Verify the tools inside the image (replace IMAGE if customized):
docker run --rm inkboy-build:0.1 sh -c 'rgbasm --version && rgbgfx --version && rgblink --version'

make host
build/libretro_probe-host build/sameboy/sameboy_libretro.so /path/to/test.gb 60

# With the existing Kindle toolchain:
make docker-build
make deploy-libretro-probe
```

The final argument is a positive finite frame count and defaults to 60. Only
`.gb` and `.gbc` content is intended; the probe passes the path and bytes to
the core, which performs the final cartridge validation. Missing cores/ROMs,
unreadable files, invalid frame counts, missing symbols, and failed loads are
reported as errors.

The frontend resolves the mandatory Libretro symbols with `dlopen`/`dlsym`,
installs no-op audio/input callbacks, and records the video dimensions and
pitch supplied by the core. Pixel format negotiation is initiated by the
core through `RETRO_ENVIRONMENT_SET_PIXEL_FORMAT`; this pinned SameBoy core
requests `RETRO_PIXEL_FORMAT_XRGB8888`, which the probe accepts. The probe
does not convert or otherwise interpret the discarded pixels. Output includes
API version, core name/version, negotiated format, dimensions, actual video
pitch, frame count, elapsed time, and maximum RSS when the host exposes it.

## ARMv7 and deployment

The ARM target uses SameBoy's existing `unix-armv7-hardfloat-neon` profile.
SameBoy's Libretro Makefile stores objects under its source-tree
`build/obj` directory. Host and ARM builds therefore must not share that
directory: the InkBoy Makefile copies the pinned submodule into the generated
`build/sameboy-work-host` or `build/sameboy-work-kindle` worktree before each
architecture build. The submodule itself is not modified. This prevents an
x86-64 `gb_libretro.c.o` from being passed to the ARM linker, which otherwise
fails with `file format not recognized`.

The ARM profile also invokes `bc` while selecting `-march` and static runtime
flags. The Docker image provides `bc`; it is a build-time dependency only.

Build the ARM artifacts through the existing container workflow:

```sh
make docker-build
file build/libretro_probe build/sameboy-kindle/sameboy_libretro.so
readelf -h build/libretro_probe build/sameboy-kindle/sameboy_libretro.so
readelf -l build/libretro_probe | grep interpreter
readelf -d build/libretro_probe
readelf -d build/sameboy-kindle/sameboy_libretro.so
```

Do not treat those flags as PW5 evidence until the device reports them:

```sh
ssh kindle 'uname -m; cat /proc/cpuinfo'
file build/libretro_probe build/sameboy-kindle/sameboy_libretro.so
readelf -l build/libretro_probe
readelf -d build/libretro_probe
nm -D build/libretro_probe
```

`make deploy-libretro-probe` only stages the two ARM artifacts in
`build/libretro-probe-deploy`; it does not alter the existing `bin/deploy`
flow or the Kindle root filesystem. A physical PW5 execution requires an
externally supplied homebrew/test ROM and must report the complete probe line.
No Kindle result is claimed by this repository without that evidence.
