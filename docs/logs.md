# InkBoy log workflow

InkBoy has one primary place for collected runtime evidence: `build/logs/`.
Each collected device run uses the same run ID for three flat files:

```text
build/logs/<run-id>.log       runtime output
build/logs/<run-id>.manifest  binary, policy and source provenance
build/logs/<run-id>.sha256    package checksums
```

The collection command is:

```sh
make collect RUN_ID=<run-id>
```

The compact human-readable view is:

```sh
make logs
```

It reports the final status, emulation FPS, completed display FPS, normal and
initial presentation wait times, whether normal waits were deferred, maximum
input polling gap, superseded frames, refresh failures and framebuffer
restoration. It intentionally omits the per-presentation
profile stream; use `tools/profile_report.py` when a machine-readable JSON
summary or wait p95 is needed:

```sh
python3 tools/profile_report.py build/logs/<run-id>.log
python3 tools/log_report.py --json
```

The log sources are:

- `make run` starts the deployed InkBoy binary on the Kindle. Its stdout and
  stderr are retained remotely as `runtime.log`.
- `make collect` copies that remote runtime log and its provenance into the
  flat local directory above.
- The application emits `profile`, `runtime`, `display` and `cleanup` lines.
  `src/profile.c` produces stage timings and counters; `src/main.c` records
  scheduling and final status; `src/render.c` records display geometry and
  cleanup; `src/input.c` records touchscreen cleanup.
- `make test` writes host test output beside each test binary under `build/`.
  Those files are validation logs, not gameplay measurements.
- Historical probe logs remain useful for the continuous-rendering and 48 kHz
  investigations, but their standalone probe source has been removed.

For a first pass, use `make logs` and select only runs with status `0`,
`restore=yes`, and `fail=0`. Compare runs with the same ROM, binary, policy,
duration and device conditions. Partial smoke logs remain visible so an
incomplete cleanup is not silently mistaken for a successful run.
