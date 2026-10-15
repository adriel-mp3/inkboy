# InkBoy contribution instructions

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
