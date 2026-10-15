# InkBoy v0.0.2

Minimal native GTK2 application for a jailbroken Kindle Paperwhite 5.

The first milestone displays an interactive `Hello, Kindle!` button. Pressing
it writes `Hello, Kindle!` to stdout and closing the window exits cleanly.

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

For a local UI/source smoke test only:

```sh
make host
```

The host build is not evidence that the Kindle ABI or runtime libraries are
compatible.

## Device deployment

The executable has been tested on a Kindle Paperwhite 5 (11th generation) with
firmware 5.19.2. The repository includes a small deployment helper that uses
the existing `kindle` SSH alias from `~/.ssh/config`:

```sh
./bin/deploy
./bin/deploy --run
```

The helper rebuilds the ARM executable, checks SSH connectivity, creates
`/mnt/us/inkboy`, transfers the executable with `scp -O`, and makes it
executable. `--run` starts it remotely. It does not modify the Kindle root
filesystem.

The window can also be started manually through the device's homebrew/KPM
mechanism:

```sh
/mnt/us/inkboy/inkboy
```

The window title uses the Kindle/Awesome WM application format. The first
hardware run displayed `Hello, Kindle!` successfully.

## Development workflow

Commit messages use Conventional Commits and releases use annotated semantic
version tags. See [AGENTS.md](AGENTS.md) for repository instructions and
[docs/commits.md](docs/commits.md) for the complete convention. Release and
tagging steps are manual for now.

## Compatibility

See [docs/compatibility.md](docs/compatibility.md) for the PW5 ABI, GTK2
runtime dependencies, and known v0.0.2 blockers.

## License

InkBoy is licensed under GNU GPL-3.0-only. See [LICENSE](LICENSE) for the
complete license text. Gambatte-K2 is used only as a technical reference for
Kindle rendering and input; InkBoy does not include its emulator or game
engine.
