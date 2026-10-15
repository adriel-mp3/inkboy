# Commits and versioning

InkBoy follows [Conventional Commits](https://www.conventionalcommits.org/)
for commit messages and semantic version tags for releases. This is a manual
convention for now; CI and release automation are intentionally out of scope.

## Commit format

```text
<type>(<optional scope>): <short imperative description>
```

Use a lowercase type and a concise subject without a trailing period. Common
types for this project are:

- `feat`: a user-visible capability
- `fix`: a bug correction
- `docs`: documentation only
- `build`: build system, Docker, or toolchain changes
- `test`: tests or validation changes
- `refactor`: internal code restructuring without behavior change
- `chore`: maintenance that does not change the application

Examples:

```text
feat: add Kindle deployment helper
build: pin GTK2 Docker dependencies
docs: document PW5 runtime compatibility
fix: close the GTK window cleanly
```

For an incompatible change, add `!` after the type or scope and explain it in
the body, for example `feat!: change the device launch interface`. A body
should explain why when the subject is not sufficient. Keep unrelated changes
in separate commits.

## Version tags

Create annotated tags using `vMAJOR.MINOR.PATCH`:

```sh
git tag -a v0.0.2 -m "Release v0.0.2"
git push origin v0.0.2
```

Until the first stable release:

- increment `MINOR` for a compatible feature milestone;
- increment `PATCH` for a compatible bug fix or documentation-only release;
- increment `MAJOR` only when the project deliberately moves to a breaking
  release line.

The initial InkBoy release is `v0.0.2`. Tags should point to a reviewed
commit that has passed the relevant build or device validation. Pushing a tag
is a separate manual action and requires explicit authorization.
