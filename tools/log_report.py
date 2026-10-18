#!/usr/bin/env python3
"""Print a compact, human-readable summary of InkBoy runtime logs."""

import argparse
import json
from pathlib import Path


def fields(line):
    return dict(token.split("=", 1) for token in line.split() if "=" in token)


def read_log(path):
    lines = path.read_text(errors="replace").splitlines()
    profiles = [fields(line) for line in lines if line.startswith("profile ")]
    summaries = [item for item in profiles if item.get("event") == "summary"]
    if not summaries:
        return {"name": path.name, "status": "incomplete", "path": str(path)}
    summary = summaries[-1]
    final = next((fields(line) for line in reversed(lines)
                  if line.startswith("runtime event=final ")), {})
    result = {"name": path.name, "path": str(path),
              "status": final.get("status", summary.get("status", "?"))}
    for key in ("emu_fps", "normal_fps", "cpu_pct", "rss_kib",
                "refresh_failures", "superseded_frames"):
        result[key] = summary.get(key, "-")
    result["wait_ms"] = summary.get("refresh_wait_avg_ms",
                                    summary.get("refresh_wait_last_ms", "-"))
    traces = [fields(line) for line in lines if line.startswith("display event=trace ")]
    for kind, key in (("normal", "wait_ms"), ("initial", "wait_ms"),
                      ("cleanup", "wait_ms")):
        waits = [float(item[key]) for item in traces
                 if item.get("kind") == kind and key in item]
        if waits:
            result[f"{kind}_wait_ms"] = sum(waits) / len(waits)
    if any(item.get("kind") == "normal" and item.get("completion") == "deferred"
           for item in traces):
        result["wait_mode"] = "deferred"
    else:
        result["wait_mode"] = "sync"
    result["gap_ms"] = summary.get("input_gap_max_ms", "-")
    result["restored"] = all(
        any(line.startswith(f"cleanup stage={stage} result=0") for line in lines)
        for stage in ("restore", "restore_wait")
    )
    return result


def discover(root):
    direct = sorted(root.glob("*.log"))
    return direct or sorted(root.glob("**/runtime.log"))


def display_value(value, key):
    if key == "restored":
        return "yes" if value else "no"
    if key == "status":
        return str(value)
    try:
        return f"{float(value):.2f}"
    except (TypeError, ValueError):
        return str(value)


def print_table(results):
    columns = (("log", "name"), ("status", "status"), ("emu", "emu_fps"),
               ("display", "normal_fps"), ("normal ms", "normal_wait_ms"),
               ("init ms", "initial_wait_ms"), ("mode", "wait_mode"),
               ("gap ms", "gap_ms"), ("drop", "superseded_frames"),
               ("fail", "refresh_failures"), ("restore", "restored"))
    rows = [[display_value(item.get(key, "-"), key) for _, key in columns]
            for item in results]
    widths = [max(len(title), *(len(row[index]) for row in rows))
              for index, (title, _) in enumerate(columns)]
    print("  ".join(title.ljust(width) for (title, _), width in zip(columns, widths)))
    print("  ".join("-" * width for width in widths))
    for row in rows:
        print("  ".join(value.ljust(width) for value, width in zip(row, widths)))


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("paths", nargs="*", type=Path)
    parser.add_argument("--json", action="store_true", dest="as_json")
    parser.add_argument("--root", type=Path, default=Path("build/logs"))
    args = parser.parse_args()
    paths = args.paths or discover(args.root)
    if not paths:
        parser.error(f"no logs found in {args.root}")
    results = [read_log(path) for path in paths]
    if args.as_json:
        print(json.dumps(results, indent=2))
    else:
        print_table(results)


if __name__ == "__main__":
    main()
