#!/usr/bin/env python3
"""Summarize completed TASK-010 updates; never grant visual acceptance."""

import argparse
import json
import math
from pathlib import Path


def fields(line):
    return dict(token.split("=", 1) for token in line.split() if "=" in token)


def summarize(path):
    lines = Path(path).read_text().splitlines()
    reports = [fields(line) for line in lines if line.startswith("profile ")
               and "phase=gameplay " in line]
    summaries = [r for r in reports if r["event"] == "summary"]
    finals = [fields(line) for line in lines if line.startswith("runtime event=final ")]
    if len(summaries) != 1 or len(finals) != 1:
        raise ValueError(f"{path}: incomplete run (missing gameplay summary/final status)")
    summary = summaries[-1]
    waits = {"normal": [], "cleanup": [], "initial": []}
    previous = {"normal": 0, "cleanup": 0, "initial": 0}
    for report in reports:
        if report["event"] not in ("presentation", "cleanup"):
            continue
        delta = sum(int(report[counter]) - previous[kind] for kind, counter in (
            ("normal", "normal_presentations"), ("cleanup", "cleanups"),
            ("initial", "initial_presentations")))
        if delta != 1:
            raise ValueError(f"{path}: missing or reordered completion reports")
        for kind, counter, stage in (
                ("normal", "normal_presentations", "refresh_wait"),
                ("cleanup", "cleanups", "clean_wait"),
                ("initial", "initial_presentations", "initial_wait")):
            count = int(report[counter])
            if count != previous[kind]:
                if count != previous[kind] + 1:
                    raise ValueError(f"{path}: missing or reordered completion reports")
                waits[kind].append(float(report[f"{stage}_last_ms"]))
                previous[kind] = count
    for kind, counter in (("normal", "normal_presentations"), ("cleanup", "cleanups"),
                          ("initial", "initial_presentations")):
        if len(waits[kind]) != int(summary[counter]):
            raise ValueError(f"{path}: incomplete completion samples")
    result = {"log": str(path)}
    for key in ("runs", "elapsed_s", "emu_fps", "normal_fps", "cleanup_fps", "cpu_pct", "rss_kib",
                "peak_rss_kib", "refresh_attempts", "refresh_failures", "presentations",
                "initial_presentations", "normal_presentations", "cleanups", "unchanged_images",
                "initial_refresh_failures", "normal_refresh_failures", "cleanup_refresh_failures",
                "input_gap_max_ms", "input_gap_clean_max_ms", "emulation_avg_ms"):
        result[key] = float(summary[key])
    if result["elapsed_s"] <= 0:
        raise ValueError(f"{path}: nonpositive gameplay interval")
    result["normal_fps"] = result["normal_presentations"] / result["elapsed_s"]
    result["emu_fps"] = result["runs"] / result["elapsed_s"]
    for kind, samples in waits.items():
        samples.sort()
        result[f"{kind}_wait_p95_ms"] = (
            samples[math.ceil(len(samples) * 0.95) - 1] if samples else None)
    result["final_status"] = int(finals[-1]["status"])
    result["restored"] = all(any(line.startswith(f"cleanup stage={stage} result=0 ")
                                for line in lines) for stage in ("restore", "restore_wait"))
    result["normal_flash_requests"] = sum(
        "kind=1 " in line and "flashing=1" in line
        for line in lines if line.startswith("display event=refresh "))
    return result


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("logs", nargs="+")
    parser.add_argument("--baseline", help="contemporaneous 1 FPS baseline log")
    parser.add_argument("--target", type=int, choices=(1, 2, 4, 8))
    args = parser.parse_args()
    baseline = summarize(args.baseline) if args.baseline else None
    results = []
    for path in args.logs:
        result = summarize(path)
        if args.target is not None and baseline is not None:
            if baseline["emu_fps"] <= 0 or baseline["final_status"] or baseline["refresh_failures"] or not baseline["restored"]:
                raise ValueError("baseline did not complete successfully")
            result["emulation_ratio"] = result["emu_fps"] / baseline["emu_fps"]
            result["numeric_checks_pass"] = (
                result["normal_fps"] >= args.target * 0.9
                and result["emulation_ratio"] >= 0.95
                and result["input_gap_max_ms"] < 300
                and result["refresh_failures"] == 0
                and result["normal_flash_requests"] == 0
                and result["final_status"] == 0 and result["restored"])
        result["visual_acceptance"] = "requires user confirmation during motion/text/scene transitions"
        results.append(result)
    print(json.dumps(results, indent=2, allow_nan=False))


if __name__ == "__main__":
    main()
