#!/usr/bin/env python3
"""Run one isolated PW5 experiment, timing from its first completed update."""

import argparse
import os
from pathlib import Path
import re
import selectors
import subprocess
import time


DIRECTORY = "/mnt/us/inkboy/task010-runtime"
PROFILES = ["baseline"] + [f"{mode}-{fps}-{cleanup}"
    for mode in ("auto", "gc16") for fps in (1, 2, 4, 8) for cleanup in (30, 60)]
SSH = ["ssh", "-o", "BatchMode=yes", "-o", "ConnectTimeout=5", "kindle"]


def stop(pid, executable, signal):
    # Both values originate from constrained local arguments / a numeric PID.
    command = (f'test "$(readlink /proc/{pid}/exe)" = "{executable}" '
               f'&& kill -{signal} {pid}')
    subprocess.run(SSH + [command], check=True, timeout=10)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("profile", choices=PROFILES)
    parser.add_argument("log", type=Path)
    parser.add_argument("--seconds", type=int, default=60)
    parser.add_argument("--signal", choices=("INT", "TERM", "HUP"), default="TERM")
    parser.add_argument("--touch-failure", action="store_true")
    args = parser.parse_args()
    if not 1 <= args.seconds <= 600:
        parser.error("--seconds must be between 1 and 600")
    executable = f"{DIRECTORY}/inkboy-{args.profile}"
    preload = ("env LD_PRELOAD=/mnt/us/inkboy/task009-runtime/touch-failure.so "
               if args.touch_failure else "")
    command = (f'cd {DIRECTORY} && printf "control pid=%s\\n" "$$" '
               f'&& exec {preload}./inkboy-{args.profile}')
    pid = None
    started = None
    sent = False
    buffer = b""
    deadline = time.monotonic() + 60
    # Exclusive creation prevents accidentally overwriting prior evidence.
    with args.log.open("xb") as log:
        process = subprocess.Popen(SSH + [command], stdout=subprocess.PIPE,
                                   stderr=subprocess.STDOUT, stdin=subprocess.DEVNULL)
        selector = selectors.DefaultSelector()
        os.set_blocking(process.stdout.fileno(), False)
        selector.register(process.stdout, selectors.EVENT_READ)
        print(f"runner profile={args.profile} log={args.log}", flush=True)
        try:
            while selector.get_map():
                for key, _ in selector.select(timeout=0.2):
                    chunk = os.read(key.fd, 65536)
                    if not chunk:
                        selector.unregister(key.fileobj)
                        continue
                    log.write(chunk)
                    log.flush()
                    buffer += chunk
                    while b"\n" in buffer:
                        line, buffer = buffer.split(b"\n", 1)
                        match = re.fullmatch(rb"control pid=([1-9][0-9]*)", line)
                        if match:
                            pid = int(match[1])
                        if started is None and line.startswith(b"profile event=presentation phase=gameplay "):
                            started = time.monotonic()
                            deadline = started + args.seconds
                            print(f"runner gameplay_started pid={pid} seconds={args.seconds}", flush=True)
                if time.monotonic() >= deadline:
                    if sent or pid is None:
                        raise TimeoutError("PW5 experiment did not finish within its safety deadline")
                    stop(pid, executable, args.signal)
                    sent = True
                    deadline = time.monotonic() + 20
                    print(f"runner sent={args.signal} pid={pid}", flush=True)
            status = process.wait(timeout=5)
            print(f"runner final_status={status}", flush=True)
            return status
        finally:
            if process.poll() is None:
                if pid is not None and not sent:
                    stop(pid, executable, "TERM")
                try:
                    process.wait(timeout=10)
                except subprocess.TimeoutExpired:
                    process.terminate()
            selector.close()


if __name__ == "__main__":
    raise SystemExit(main())
