# InkBoy v0.0.2

Minimal native C application for a jailbroken Kindle Paperwhite 5.

Stage 2 displays the original adriel.cloud duck mascot and `InkBoy` directly on
the e-ink display through a statically linked minimal FBInk renderer. The
renderer is intentionally static and has no input, animation, or emulator
code.

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

The process performs one draw and waits for SIGINT, SIGTERM, or SIGHUP so it
can restore the previous framebuffer state on normal exit. Hardware visual
verification of the Stage 2 duck screen and restoration after termination is
complete.

## Development workflow

Commit messages use Conventional Commits and releases use annotated semantic
version tags. See [AGENTS.md](AGENTS.md) for repository instructions and
[docs/commits.md](docs/commits.md) for the complete convention. Release and
tagging steps are manual for now.

## Compatibility

See [docs/compatibility.md](docs/compatibility.md) for the PW5 ABI and known
v0.0.2 blockers. See
[docs/experiments/02-rendering.md](docs/experiments/02-rendering.md) for the
FBInk dependency, refresh strategy, and hardware-test status.

## License

InkBoy is licensed under GNU GPL-3.0-only. See [LICENSE](LICENSE) for the
complete license text. Gambatte-K2 is used only as a technical reference for
Kindle rendering and input; InkBoy does not include its emulator or game
engine.
