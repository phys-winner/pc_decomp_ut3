"""Bounded SurfaceFlinger timestats capture for one already running UT3 process."""
import argparse
import asyncio
from datetime import datetime, timezone
import hashlib
import json
import math
import os
from pathlib import Path
import re
import shlex
import shutil
import subprocess
import time

PACKAGE = "org.ut3.port"
SOURCE = "https://developer.android.com/games/optimize/framerate"
LATENCY_SOURCE = "https://android.googlesource.com/platform/frameworks/native/+/refs/heads/main/services/surfaceflinger/FrameTracker.cpp"
INT64_MAX = (1 << 63) - 1


def seconds(value):
    value = float(value)
    if not math.isfinite(value) or not 0 < value <= 60:
        raise argparse.ArgumentTypeError("seconds must be finite and in (0, 60]")
    return value


def foreground(text):
    lines = [line for line in text.splitlines() if line.strip().startswith("mCurrentFocus=")]
    if len(lines) != 1:
        raise ValueError("No unique current-focus window")
    found = re.search(r"\bu\d+\s+([\w.]+)/", lines[0])
    if not found or found[1] != PACKAGE:
        raise ValueError("UT3 is not the current-focus package")
    return PACKAGE


def parse_timestats(text, uid):
    selected = []
    for block in re.split(r"\n[ \t]*\n", text.replace("\r\n", "\n")):
        names = re.findall(r"(?m)^\s*layerName\s*=\s*(.+)$", block)
        if not names:
            continue
        if len(names) != 1:
            raise ValueError("Ambiguous layer block")
        name = names[0].strip()
        if not name.startswith((f"SurfaceView[{PACKAGE}/", f"SurfaceView - {PACKAGE}/")):
            continue
        fields = dict(re.findall(r"(?m)^[ \t]*(\w+)[ \t]*=[ \t]*([^\r\n]*)$", block))
        package = fields.get("packageName", "").strip()
        if package and package != PACKAGE:
            raise ValueError("Target layer has a different package")
        if not package and "uid" not in fields:
            raise ValueError("Unattributed target layer has no UID")
        if "uid" in fields and int(fields["uid"]) != uid:
            raise ValueError("Target layer has a different UID")
        frames, fps = int(fields.get("totalFrames", "0")), float(fields.get("averageFPS", "nan"))
        if frames <= 0 or not math.isfinite(fps) or fps <= 0:
            raise ValueError("Target layer has no valid frame samples")
        lines = block.splitlines()
        headers = [i for i, line in enumerate(lines) if re.fullmatch(
            r"\s*(?:present2present|presentToPresent) histogram is as below:\s*", line)]
        if len(headers) != 1:
            raise ValueError("No unique target present-to-present histogram")
        histogram = {}
        for line in lines[headers[0] + 1:]:
            if not re.fullmatch(r"\s*(?:\d+ms=\d+\s*)+", line):
                break
            for bucket, count in re.findall(r"(\d+)ms=(\d+)", line):
                bucket, count = int(bucket), int(count)
                if bucket in histogram or (bucket == 0 and count):
                    raise ValueError("Invalid histogram bucket")
                histogram[bucket] = count
        samples = sum(histogram.values())
        if not samples:
            raise ValueError("Empty target histogram")
        def percentile(q):
            cumulative = 0
            for bucket, count in sorted(histogram.items()):
                cumulative += count
                if cumulative >= math.ceil(samples * q):
                    return bucket
        p90, p99 = percentile(.90), percentile(.99)
        selected.append({"layer_name": name, "total_frames": frames, "average_fps_bucket_estimate": fps,
                         "histogram_samples": samples, "histogram_ms": histogram,
                         "p90_bucket_ms": p90, "p99_bucket_ms": p99,
                         "low_10_fps_bucket_estimate": 1000 / p90,
                         "low_1_fps_bucket_estimate": 1000 / p99, "text": block.strip()})
    if not selected:
        raise ValueError("No matching UT3 SurfaceView frame statistics")
    return selected


def parse_latency(text):
    lines = text.splitlines()
    if not lines or not re.fullmatch(r"\d+", lines[0].strip()):
        raise ValueError("Missing latency refresh-period header")
    refresh = int(lines[0])
    if not 0 < refresh < INT64_MAX:
        raise ValueError("Invalid latency refresh period")
    records, rows, zero, pending, invalid, duplicates = {}, 0, 0, 0, 0, 0
    for line in lines[1:]:
        if not line.strip() or line.strip() == "OPlusFrameTracker":
            continue
        if not re.fullmatch(r"\s*-?\d+\s+-?\d+\s+-?\d+\s*", line):
            raise ValueError("Malformed latency timestamp triple")
        triple = tuple(map(int, line.split()))
        if any(not -(1 << 63) <= value <= INT64_MAX for value in triple):
            raise ValueError("Latency timestamp outside signed 64-bit range")
        rows += 1
        actual = triple[1]
        if actual == 0:
            zero += 1
        elif actual == INT64_MAX:
            pending += 1
        elif actual < 0:
            invalid += 1
        else:
            duplicates += actual in records
            records.setdefault(actual, triple)
    if not records:
        raise ValueError("No valid actual-present timestamps")
    return {"refresh_period_ns": refresh, "records": records, "timestamp_triples": rows,
            "valid_actual_records": rows - zero - pending - invalid,
            "zero_actual_records": zero, "pending_actual_records": pending,
            "invalid_actual_records": invalid, "duplicate_actual_records": duplicates}


class PresentCapture:
    """Union overlapping rings, keeping coverage loss distinct from slow frames."""
    def __init__(self, baseline):
        self.baseline = max(baseline["records"])
        self.baseline_record = baseline["records"][self.baseline]
        self.previous = set(baseline["records"])
        self.seen = set(self.previous)
        self.records, self.gaps, self.refresh = {}, [], {baseline["refresh_period_ns"]}
        self.counts = {key: baseline[key] for key in ("timestamp_triples", "valid_actual_records",
            "zero_actual_records", "pending_actual_records", "invalid_actual_records", "duplicate_actual_records")}
        self.polls, self.no_progress = 0, 0

    def add(self, snapshot):
        current = set(snapshot["records"])
        if max(current) < max(self.previous):
            raise ValueError("Actual-present timestamp regression on the selected layer")
        self.polls += 1
        self.refresh.add(snapshot["refresh_period_ns"])
        if not self.previous.intersection(current):
            self.gaps.append({"poll": self.polls, "previous_last_ns": max(self.previous),
                              "current_first_ns": min(current)})
        added = [value for value in current if value > self.baseline and value not in self.records]
        self.no_progress += not added
        self.counts["duplicate_actual_records"] += len(current.intersection(self.seen))
        for key in self.counts:
            self.counts[key] += snapshot[key]
        for value in added:
            self.records[value] = snapshot["records"][value]
        self.seen.update(current)
        self.previous = current

    def result(self):
        actual = sorted(self.records)
        if len(actual) < 2:
            raise ValueError("Fewer than two unique post-baseline presents")
        intervals = [(left, right, right - left) for left, right in zip(actual, actual[1:])]
        known = [delta for left, right, delta in intervals
                 if not any(left <= gap["previous_last_ns"] < right for gap in self.gaps)]
        if not known:
            raise ValueError("No intervals with known ring coverage")
        ordered = sorted(known)
        span = actual[-1] - actual[0]
        def percentile(q):
            return ordered[math.ceil(len(ordered) * q) - 1] / 1e6
        return {**self.counts, "polls": self.polls, "polls_without_new_presents": self.no_progress,
                "refresh_periods_ns": sorted(self.refresh), "baseline_actual_ns": self.baseline,
                "first_actual_ns": actual[0], "last_actual_ns": actual[-1],
                "unique_post_baseline_presents": len(actual), "observed_intervals": len(intervals),
                "known_intervals": len(known), "present_span_seconds": span / 1e9,
                "present_fps": len(intervals) * 1e9 / span if not self.gaps else None,
                "known_interval_fps": len(known) * 1e9 / sum(known),
                "p90_interval_ms": percentile(.90), "p99_interval_ms": percentile(.99),
                "unknown_coverage_gaps": self.gaps,
                "percentile_method": "nearest rank of actual-present intervals with known ring coverage"}

    def text(self):
        return ("# desiredPresentTime_ns actualPresentTime_ns frameReadyTime_ns\n"
                "# baseline\n" + "\t".join(map(str, self.baseline_record)) + "\n# post-baseline\n" +
                "\n".join("\t".join(map(str, self.records[value])) for value in sorted(self.records)) + "\n")


async def latency(adb, layer):
    return parse_latency(await adb("shell", "dumpsys", "SurfaceFlinger", "--latency", shlex.quote(layer)))


def dxvk_metrics(text, pid, start, end):
    metrics = []
    message = (r"\[dxvk\] frame \d+: [\d.]+ fps; per frame [\d.]+ ms wall, [\d.]+ ms render cpu, "
               r"[\d.]+ ms machine wait, [\d.]+ ms process cpu, [\d.]+/[\d.]+ ms native Present cpu/wall, "
               r"\d+ calls, \d+ draws, [\d.]+ query polls; accepted unique vkQueuePresentKHR")
    for line in text.splitlines():
        found = re.fullmatch(r"\s*(\d+\.\d+)\s+(\d+)\s+\d+\s+I\s+UT3\s*:\s*(" + message + r")", line)
        if found and int(found[2]) == pid and start <= float(found[1]) <= end:
            metrics.append(line.strip())
    return metrics


class Adb:
    def __init__(self, serial):
        sdk = Path(os.environ.get("ANDROID_SDK_ROOT", Path.home() / "AppData/Local/Android/Sdk"))
        self.path = sdk / "platform-tools" / ("adb.exe" if os.name == "nt" else "adb")
        if not self.path.is_file():
            self.path = shutil.which("adb")
        if not self.path:
            raise ValueError("Installed adb not found")
        self.serial = serial

    async def __call__(self, *arguments):
        process = await asyncio.create_subprocess_exec(str(self.path), "-s", self.serial, *map(str, arguments),
            stdout=asyncio.subprocess.PIPE, stderr=asyncio.subprocess.PIPE,
            creationflags=subprocess.CREATE_NO_WINDOW if os.name == "nt" else 0)
        try:
            out, _ = await asyncio.wait_for(process.communicate(), 15)
        except BaseException:
            if process.returncode is None:
                process.kill()
                await process.wait()
            raise
        if process.returncode:
            raise RuntimeError(f"adb {arguments[0]} failed, exit {process.returncode}")
        return out.decode("utf-8", errors="replace").replace("\r\n", "\n")


async def verify(adb, pid):
    package, status, command, stat, window = await asyncio.gather(
        adb("shell", "cmd", "package", "list", "packages", "-U", PACKAGE),
        adb("shell", "run-as", PACKAGE, "cat", f"/proc/{pid}/status"),
        adb("shell", "run-as", PACKAGE, "cat", f"/proc/{pid}/cmdline"),
        adb("shell", "run-as", PACKAGE, "cat", f"/proc/{pid}/stat"),
        adb("shell", "dumpsys", "window"))
    found = re.search(r"(?m)^package:org\.ut3\.port\s+uid:(\d+)\s*$", package)
    uid = int(found[1]) if found else -1
    actual = re.search(r"(?m)^Uid:\s+(\d+)\s+(\d+)\s+(\d+)\s+(\d+)", status)
    if uid < 10000 or not actual or any(int(value) != uid for value in actual.groups()):
        raise ValueError("PID UID does not match installed UT3 package")
    if command.split("\0")[0] != PACKAGE:
        raise ValueError("PID is not the main UT3 process")
    return {"pid": pid, "uid": uid, "start_ticks": int(stat.rsplit(")", 1)[1].split()[19]),
            "foreground": foreground(window)}


def installed_paths(paths, maps):
    apks = [line.removeprefix("package:").strip() for line in paths.splitlines() if line.startswith("package:")]
    native = {found[1] for line in maps.splitlines()
              if (found := re.search(r"\s(/data/app/[^\s]+/libut3\.so)$", line.strip()))}
    if not apks or len(native) != 1:
        raise ValueError("Missing APK paths or no unique loaded libut3.so file")
    return apks, native.pop()


async def receipt(adb, pid):
    paths, maps = await asyncio.gather(adb("shell", "pm", "path", PACKAGE),
        adb("shell", "run-as", PACKAGE, "cat", f"/proc/{pid}/maps"))
    apks, native = installed_paths(paths, maps)
    async def pin(path):
        if not re.fullmatch(r"/data/app/[A-Za-z0-9_./~+=-]+", path):
            raise ValueError("Unexpected installed binary path")
        digest = (await adb("shell", "run-as", PACKAGE, "sha256sum", path)).split()[0]
        if not re.fullmatch(r"[0-9a-f]{64}", digest):
            raise ValueError("Invalid installed binary SHA256")
        return {"path": path, "sha256": digest}
    pinned = await asyncio.gather(*(pin(path) for path in apks), pin(native))
    return {"apks": pinned[:-1], "loaded_native": pinned[-1]}


async def epoch(adb):
    value = (await adb("shell", "date", "+%s.%N")).strip()
    if not re.fullmatch(r"\d+\.\d+", value):
        raise ValueError("Device epoch clock unavailable")
    return value


async def capture(args):
    adb = Adb(args.serial)
    before = await verify(adb, args.pid)
    binaries = await receipt(adb, args.pid)
    args.out.mkdir(parents=True, exist_ok=False)
    report = {"serial": args.serial, "requested_seconds": args.seconds, "target": before,
              "installed_binaries": binaries, "utc": datetime.now(timezone.utc).isoformat(),
              "method": SOURCE, "source_sha256": hashlib.sha256(Path(__file__).read_bytes()).hexdigest(),
              "latency_method": LATENCY_SOURCE,
              "latency_reference_blob": "93d0313e535f4219807dd75092a214e8be078406",
              "limits": "Focus/identity verified at endpoints; TimeStats FPS/quantiles are bucket estimates. Actual-present FPS uses timestamp intervals, excluding warmup and capture tail; coverage gaps invalidate full-span FPS. Layer data alone does not establish gameplay or graphics settings."}
    try:
        start_epoch = await epoch(adb)
        start = time.monotonic()
        try:
            await adb("shell", "dumpsys", "SurfaceFlinger", "--timestats", "-clear", "-enable")
            await asyncio.sleep(.5)
            discovery = parse_timestats(await adb("shell", "dumpsys", "SurfaceFlinger", "--timestats", "-dump"), before["uid"])
            if len(discovery) != 1:
                raise ValueError("No unique active target SurfaceView for latency capture")
            layer_name = discovery[0]["layer_name"]
            presents = PresentCapture(await latency(adb, layer_name))
            measurement_start = time.monotonic()
            report["warmup_host_seconds"] = measurement_start - start
            deadline, next_poll = measurement_start + args.seconds, measurement_start + .5
            poll_seconds = []
            while True:
                await asyncio.sleep(max(0, min(next_poll, deadline) - time.monotonic()))
                poll_start = time.monotonic()
                presents.add(await latency(adb, layer_name))
                poll_seconds.append(time.monotonic() - poll_start)
                if time.monotonic() >= deadline:
                    break
                next_poll += .5
            report["measurement_host_seconds"] = time.monotonic() - measurement_start
            report["latency_poll_max_host_seconds"] = max(poll_seconds)
            raw = await adb("shell", "dumpsys", "SurfaceFlinger", "--timestats", "-dump")
        finally:
            await adb("shell", "dumpsys", "SurfaceFlinger", "--timestats", "-disable")
        report["elapsed_host_seconds"] = time.monotonic() - start
        end_epoch = await epoch(adb)
        after = await verify(adb, args.pid)
        if after != before:
            raise ValueError("UT3 process identity or focus changed during capture")
        layers = parse_timestats(raw, before["uid"])
        if len(layers) != 1 or layers[0]["layer_name"] != layer_name:
            raise ValueError("Selected SurfaceView changed during capture")
        actual_metrics = presents.result()
        log = await adb("logcat", "-d", "--pid", args.pid, "-v", "epoch", "-T", start_epoch, "UT3:I", "*:S")
        metrics = dxvk_metrics(log, args.pid, float(start_epoch), float(end_epoch))
        (args.out / "target-layer.txt").write_text("\n\n".join(layer.pop("text") for layer in layers) + "\n", encoding="utf-8")
        (args.out / "dxvk-metrics.txt").write_text("\n".join(metrics) + ("\n" if metrics else ""), encoding="utf-8")
        (args.out / "present-timestamps.txt").write_text(presents.text(), encoding="utf-8")
        report.update({"result": "CAPTURED", "layers": layers, "actual_present": actual_metrics,
                       "dxvk_metric_lines": len(metrics),
                       "device_epoch_start": start_epoch, "device_epoch_end": end_epoch})
    except BaseException as error:
        report.update({"result": "FAILED", "error": type(error).__name__ + ": " + str(error)})
        raise
    finally:
        (args.out / "receipt.json").write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--serial", required=True)
    parser.add_argument("--pid", required=True, type=int)
    parser.add_argument("--seconds", required=True, type=seconds)
    parser.add_argument("--out", required=True, type=Path, help="New output directory; existing paths are not overwritten")
    args = parser.parse_args()
    if args.pid <= 0 or not args.serial.strip():
        parser.error("Explicit nonempty serial and positive PID required")
    asyncio.run(capture(args))


if __name__ == "__main__":
    main()
