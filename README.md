# InkBoy

Minimal native C application for a jailbroken Kindle Paperwhite 5.

The current M5 prototype integrates Pokémon Yellow through a pinned SameBoy
Libretro core, virtual touchscreen controls, and a minimal statically linked
FBInk renderer. Presentation remains single-threaded with a conservative 1 FPS
full-screen refresh. Gameplay, control response and normal shutdown/restoration
have been validated on the physical PW5; Power is not a controlled exit.
The original Stage 2 duck renderer remains a historical validated baseline.

## Build

Docker is the build environment; it is not needed on the Kindle.

```sh
# Download the KindleHF toolchain and generate its Kindle SDK sysroot.
# The SDK setup needs privileged Docker because it mounts the downloaded
# firmware image while extracting the runtime libraries.
make bootstrap

# Cross-compile build/inkboy for the KindleHF ABI.
make

# Inspect the resulting ARM ELF and its dynamic dependencies.
make inspect
```

`make bootstrap` is intentionally separate from `make`: it is a one-time,
networked setup step and can take a while. The generated toolchain and SDK are
ignored by Git. If privileged Docker is unavailable, generate the Kindle SDK
on a Linux host using the KindleModding instructions and point the build at
that toolchain/sysroot instead of using the bootstrap helper.

For a local source/link smoke test only:

```sh
make host
```

The host build is not evidence that the Kindle ABI or runtime libraries are
compatible.

## Device deployment

The repository targets a Kindle Paperwhite 5 (11th generation) with firmware
5.19.2. The repository includes a small deployment helper that uses
the existing `kindle` SSH alias from `~/.ssh/config`:

```sh
./bin/deploy
./bin/deploy --run
```

The helper rebuilds the ARM executable, checks SSH connectivity, creates
`/mnt/us/inkboy`, transfers the executable with `scp -O`, and makes it
executable. `--run` starts it remotely. It does not modify the Kindle root
filesystem.

The renderer can also be started manually through the device's homebrew/KPM
mechanism:

```sh
/mnt/us/inkboy/inkboy
```

The gameplay build uses fixed core/ROM paths; it stops through EXIT, SIGINT,
SIGTERM, SIGHUP or a runtime error and attempts framebuffer restoration. The
default core path is `/mnt/us/inkboy/task008-gameplay/sameboy_libretro.so`;
the ROM is the Pokémon Yellow file under `/mnt/us/games` specified in `main.c`.
The earlier Stage 2 duck screen and restoration have been physically verified;
the gameplay path's separate physical matrix is recorded in TASK-009.

TASK-009 adds structured runtime timing and cleanup logs. `make deploy-task009`
stages separate full-screen/experimental-region binaries and diagnostic helpers
without replacing TASK-008 deployments. These builds use the isolated
`/mnt/us/inkboy/task009-runtime/sameboy_libretro.so` core path. Build instructions,
measurement definitions, local/physical results and the repeatable device protocol are
in [the TASK-009 runtime report](docs/experiments/09-gameplay-runtime.md).

## Stage 5.4 benchmark status

Stage 5.4.4 is complete and physically validated on a Kindle Paperwhite 5
running firmware 5.19.2 with SameBoy 1.0.4 and Pokémon Yellow. The optimized
nearest-neighbor scaler reduced measured scaling time from 186.221 ms to
23.140 ms, approximately 87.6%; combined scaling and FBInk draw cost fell
from approximately 207.167 ms to 44.108 ms, approximately 78.7%.
All nearest-mode runs rendered successfully with zero refresh failures and
successful screen restoration. The user preferred nearest visually for its
pixelated Game Boy appearance. Smooth remains the CLI default pending final
confirmation of a future default switch. Full results and decisions are
recorded in [the Stage 5.4 experiment](docs/experiments/05.4-libretro-continuous-rendering.md).

The live probe is a benchmark frontend, not a playable InkBoy integration:
emulation remains below SameBoy's nominal 59.73 FPS, synchronous E-Ink
completion blocks the single-threaded loop, CPU usage remains relatively high,
sustainable higher display rates are unvalidated, and no objective ghosting
measurement has been performed.

## Development workflow

See [AGENTS.md](AGENTS.md) and [docs/commits.md](docs/commits.md) for the
project contribution and commit conventions.

## Compatibility

See [docs/decisions.md](docs/decisions.md) for the short architectural
decision summary.
See [docs/compatibility.md](docs/compatibility.md) for the PW5 ABI and known
compatibility blockers. See
[docs/experiments/02-rendering.md](docs/experiments/02-rendering.md) for the
FBInk dependency, refresh strategy, and hardware-test status. The rationale
for vendoring FBInk is recorded in
[docs/architecture.md](docs/architecture.md).

## License

InkBoy is licensed under GNU GPL-3.0-only. See [LICENSE](LICENSE) for the
complete license text. Gambatte-K2 is used only as a technical reference for
Kindle rendering and input; InkBoy does not include its emulator or game
engine.
