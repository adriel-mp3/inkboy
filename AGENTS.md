# InkBoy contribution instructions

## Persistent project decisions

- InkBoy is an independent C application, not a Gambatte-K2 fork, frontend, or
  emulator. Gambatte-K2 is a technical reference only; do not copy its source.
- The project targets the Kindle Paperwhite 5 (11th generation), firmware
  5.19.2, and must preserve the existing cross-compilation and deployment flow.
- Stage 2 research targets the original adriel.cloud duck SVG and the text
  `InkBoy` in a restrained monochrome e-ink composition. Do not invent a
  replacement mascot when the original asset is unavailable.
- Prefer the smallest C rendering path with minimal dependencies. Do not
  modify the Kindle root filesystem or implement an emulator at this stage.
- Stage 2 uses a statically linked minimal FBInk build from upstream commit
  `886f25f`, with `IMAGE` and `DRAW` enabled. InkBoy uses FBInk's public API;
  it does not map `/dev/fb0` or issue Kindle-specific ioctls directly.
- The original duck asset is `assets/duck.svg`; the development build converts
  it to the generated `src/duck_bitmap.h`. The visual asset must not be
  redrawn or replaced.
- The renderer saves the prior framebuffer with `fbink_dump`, performs one
  explicit refresh, and restores the dump on normal termination. Visual
  rendering and normal restoration on the physical PW5 are verified.
- The rationale and conditions for eventually removing the vendored FBInk
  dependency are documented in [docs/architecture.md](docs/architecture.md).
- InkBoy uses GNU GPL-3.0-only. Preserve third-party notices and check
  compatibility before reusing code or assets.

## Commits

When the user asks for commits, use the Conventional Commits format described
in [docs/commits.md](docs/commits.md). Keep each commit focused on one logical
change and use a lowercase type and subject, for example:

```text
feat: add Kindle deployment helper
docs: document PW5 compatibility
fix: handle missing Kindle sysroot
```

Before committing:

1. Inspect `git status` and the diff.
2. Run the smallest relevant validation, such as `bash -n`, `make inspect`,
   or `git diff --check`.
3. Do not include generated build, SDK, toolchain, or Docker state.
4. Report the commit hash and validation performed.

Do not amend, rebase, reset, or push unless the user explicitly requests it.

## Version tags

Use annotated `vMAJOR.MINOR.PATCH` tags. The initial published release is
`v0.0.2`; later compatible features increment the minor version and fixes
increment the patch version. Create and push tags manually when a release is
requested. No release automation is required at this stage.
