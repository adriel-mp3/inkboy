# InkBoy v0.1

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

## Device launch

Copy `build/inkboy` to the jailbroken Kindle and launch it through the device's
homebrew/KPM mechanism. The application does not modify Kindle system files.

The window title uses the Kindle/Awesome WM application format. A real PW5
test is still required to confirm the exact launcher setup and e-ink refresh
behavior.

## Compatibility

See [docs/compatibility.md](docs/compatibility.md) for the PW5 ABI, GTK2
runtime dependencies, and known v0.1 blockers.
