import argparse
import json
from pathlib import Path
import tempfile
from types import SimpleNamespace
import unittest
from unittest.mock import AsyncMock, patch
import tools.measure_frames as measurement
from tools.measure_frames import (INT64_MAX, PresentCapture, dxvk_metrics, foreground,
                                 installed_paths, parse_latency, parse_timestats, seconds)


LAYER = """displayRefreshRate = 60 fps
uid = 10574
layerName = SurfaceView[org.ut3.port/org.ut3.port.GameActivity]@abc(BLAST)#42
packageName = org.ut3.port
totalFrames = 100
averageFPS = 59.800
present2present histogram is as below:
0ms=0 16ms=85 33ms=10 50ms=4 66ms=1
acquire2present histogram is as below:
0ms=90 1ms=10
"""


class MeasureFramesTest(unittest.TestCase):
    def test_actual_present_overlap_dedup_zero_pending_and_sixty_fps(self):
        def ring(values, extras=""):
            return parse_latency("16666666\r\n" + "\r\n".join(
                f"{value - 2}\t{value}\t{value - 1}" for value in values) + "\r\n" + extras)
        origin = 1000000000
        frame = lambda index: origin + round(index * 1e9 / 60)
        baseline = ring([frame(-2), frame(-1), frame(0)],
                        f"0 0 0\r\n1 {INT64_MAX} 1\r\nOPlusFrameTracker\r\n")
        capture = PresentCapture(baseline)
        capture.add(ring([frame(index) for index in range(-1, 32)] + [frame(31)]))
        capture.add(ring([frame(index) for index in range(16, 62)]))
        result = capture.result()
        self.assertEqual(sorted(capture.records), [frame(index) for index in range(1, 62)])
        self.assertEqual(result["baseline_actual_ns"], frame(0))
        self.assertEqual((result["unique_post_baseline_presents"], result["observed_intervals"]), (61, 60))
        self.assertEqual(result["known_intervals"], 60)
        self.assertEqual(result["present_fps"], 60)
        self.assertEqual((result["p90_interval_ms"], result["p99_interval_ms"]), (16.666667, 16.666667))
        self.assertEqual(result["unknown_coverage_gaps"], [])
        self.assertEqual((result["zero_actual_records"], result["pending_actual_records"]), (1, 1))
        self.assertEqual(result["duplicate_actual_records"], 19)
        self.assertNotIn(str(INT64_MAX), capture.text())

    def test_unknown_ring_gap_is_not_misreported_as_slow_frame_or_full_fps(self):
        def ring(values):
            return parse_latency("16666666\n" + "\n".join(f"0 {value} {INT64_MAX}" for value in values))
        capture = PresentCapture(ring([100]))
        capture.add(ring([100, 200, 300]))
        capture.add(ring([1000, 1100, 1200]))
        capture.add(ring([1000, 1100, 1200]))
        result = capture.result()
        self.assertIsNone(result["present_fps"])
        self.assertEqual(result["unknown_coverage_gaps"], [{"poll": 2, "previous_last_ns": 300, "current_first_ns": 1000}])
        self.assertEqual((result["observed_intervals"], result["known_intervals"], result["polls_without_new_presents"]), (4, 3, 1))
        self.assertEqual(result["p99_interval_ms"], .0001)
        with self.assertRaises(ValueError): capture.add(ring([100, 200]))
        slow = PresentCapture(ring([100]))
        slow.add(ring([100, 200, 300, 1000]))
        self.assertEqual(slow.result()["p99_interval_ms"], .0007)
        self.assertEqual(slow.result()["unknown_coverage_gaps"], [])

    def test_latency_invalid_header_malformed_empty_and_no_new_presents_reject(self):
        for text in ("", "0\n1 2 3", "nan\n1 2 3", "16666666\n0 0 0",
                     f"16666666\n1 {INT64_MAX} 1", "16666666\n1 2", "16666666\n1 -1 1",
                     f"16666666\n1 {INT64_MAX + 1} 1", "16666666\nprivate unexpected diagnostic"):
            with self.subTest(text=text), self.assertRaises(ValueError): parse_latency(text)
        snapshot = parse_latency("16666666\n1 2 3")
        capture = PresentCapture(snapshot)
        capture.add(snapshot)
        with self.assertRaises(ValueError): capture.result()

    def test_oem_empty_package_requires_matching_uid_and_does_not_consume_next_field(self):
        oem = LAYER.replace("packageName = org.ut3.port", "packageName = \ngameMode = Unsupported")
        self.assertEqual(parse_timestats(oem, 10574)[0]["total_frames"], 100)
        for invalid in (oem.replace("uid = 10574\n", ""), oem.replace("10574", "10575"),
                        oem.replace("packageName = \n", "packageName = org.other.game\n")):
            with self.assertRaises(ValueError): parse_timestats(invalid, 10574)

    def test_crlf_installed_paths_deduplicate_elf_segments(self):
        root = "/data/app/~~M8tKWMHWcCiaU21oQFV8Sg==/org.ut3.port-06bETQ7NsOiDrAzF0JHYoA=="
        native = root + "/lib/arm64/libut3.so"
        maps = "\r\n".join(f"7bb3c51000-7bb4d20000 r--p 00000000 fd:33 206348 {native}" for _ in range(4)) + "\r\n"
        self.assertEqual(installed_paths(f"package:{root}/base.apk\r\n", maps), ([root + "/base.apk"], native))
        for invalid in (maps.replace("libut3.so", "libother.so"), maps.replace("libut3.so", "libut3.so (deleted)"),
                        maps + f"7bb3-7bb4 r--p 0 0 0 {root}/lib/other/libut3.so\r\n"):
            with self.assertRaises(ValueError): installed_paths(f"package:{root}/base.apk\r\n", invalid)

    def test_only_target_layer_and_histogram_quantiles(self):
        other = LAYER.replace("org.ut3.port", "org.other.game").replace("10574", "10001")
        fake_prefix = LAYER.replace("org.ut3.port", "org.ut3.port.fake")
        result = parse_timestats("SurfaceFlinger TimeStats:\nother global stats\n\n" + other + "\n" + LAYER + "\n" + fake_prefix, 10574)
        self.assertEqual(len(result), 1)
        self.assertNotIn("org.other", result[0]["text"])
        self.assertNotIn("org.ut3.port.fake", result[0]["text"])
        self.assertEqual((result[0]["p90_bucket_ms"], result[0]["p99_bucket_ms"]), (33, 50))
        self.assertEqual(result[0]["histogram_samples"], 100)

    def test_empty_invalid_wrong_uid_and_zero_samples_fail(self):
        for invalid in ("", "RequestedLayerState{SurfaceView[org.ut3.port/.GameActivity]}",
                        LAYER.replace("10574", "10575"), LAYER.replace("59.800", "nan"),
                        LAYER.replace("totalFrames = 100", "totalFrames = 0"),
                        LAYER.replace("0ms=0 16ms=85 33ms=10 50ms=4 66ms=1", "0ms=0 16ms=0"),
                        LAYER.replace("0ms=0", "0ms=1")):
            with self.subTest(invalid=invalid[:40]), self.assertRaises(ValueError):
                parse_timestats(invalid, 10574)

    def test_focus_exact_package_and_bounded_duration(self):
        self.assertEqual(foreground("mCurrentFocus=Window{abc u0 org.ut3.port/.GameActivity}"), "org.ut3.port")
        for value in ("mCurrentFocus=null", "mCurrentFocus=Window{abc u0 org.ut3.port.fake/.Activity}"):
            with self.assertRaises(ValueError): foreground(value)
        self.assertEqual(seconds("60"), 60)
        for value in ("0", "-1", "61", "nan", "inf"):
            with self.assertRaises(argparse.ArgumentTypeError): seconds(value)

    def test_metrics_exclude_other_pid_old_times_and_private_log_lines(self):
        line = ("100.500 123 124 I UT3 : [dxvk] frame 12: 60.0 fps; per frame 16.67 ms wall, "
                "10.00 ms render cpu, 0.10 ms machine wait, 12.00 ms process cpu, 0.01/0.02 ms native Present cpu/wall, "
                "100 calls, 20 draws, 3.0 query polls; accepted unique vkQueuePresentKHR")
        text = "\n".join([line, line.replace("123 124", "999 998"), line.replace("100.500", "90.000"),
                          "100.500 123 124 I UT3 : PRIVATE_CONFIG_MUST_NOT_BE_SAVED", line + " trailing unrecognized text"])
        self.assertEqual(dxvk_metrics(text, 123, 100, 101), [line])


class CaptureCleanupTest(unittest.IsolatedAsyncioTestCase):
    async def test_disable_finally_and_no_raw_dump_saved_on_failures(self):
        class FakeAdb:
            def __init__(self, failure): self.failure, self.calls = failure, []
            async def __call__(self, *args):
                self.calls.append(args)
                if args[-1] == self.failure: raise RuntimeError("synthetic adb failure")
                return "unrelated invalid dump"
        identity = {"pid": 123, "uid": 10574, "start_ticks": 1, "foreground": "org.ut3.port"}
        for failure in ("-enable", "-dump", "invalid-data"):
            with self.subTest(failure=failure), tempfile.TemporaryDirectory() as temporary:
                fake = FakeAdb(failure)
                args = SimpleNamespace(serial="NO-DEVICE-CALLS", pid=123, seconds=.001, out=Path(temporary) / "capture")
                with patch.object(measurement, "Adb", return_value=fake), \
                        patch.object(measurement, "verify", new=AsyncMock(return_value=identity)), \
                        patch.object(measurement, "receipt", new=AsyncMock(return_value={})), \
                        patch.object(measurement, "epoch", new=AsyncMock(return_value="100.0")):
                    with self.assertRaises((ValueError, RuntimeError)):
                        await measurement.capture(args)
                self.assertEqual(sum(call[-1] == "-disable" for call in fake.calls), 1)
                self.assertEqual(json.loads((args.out / "receipt.json").read_text())["result"], "FAILED")
                self.assertFalse((args.out / "target-layer.txt").exists())
                self.assertFalse((args.out / "dxvk-metrics.txt").exists())
                self.assertFalse((args.out / "present-timestamps.txt").exists())

    async def test_latency_failure_cleanup_and_success_quoted_exact_target_only(self):
        class FakeAdb:
            def __init__(self, failure): self.failure, self.calls, self.latency_calls = failure, [], 0
            async def __call__(self, *args):
                self.calls.append(args)
                if args[-1] == "-dump": return LAYER
                if "--latency" in args:
                    self.latency_calls += 1
                    self_test_layer = LAYER.split("layerName = ")[1].splitlines()[0]
                    if args[-1] != "'" + self_test_layer + "'": raise AssertionError("Unquoted remote layer")
                    if self.latency_calls == self.failure: raise RuntimeError("synthetic latency failure")
                    values = [100, 200] if self.latency_calls == 1 else [100, 200, 300, 400]
                    return "16666666\n" + "\n".join(f"1 {value} 2" for value in values)
                return ""
        identity = {"pid": 123, "uid": 10574, "start_ticks": 1, "foreground": "org.ut3.port"}
        for failure in (1, 2, 0):
            with self.subTest(failure=failure), tempfile.TemporaryDirectory() as temporary:
                fake = FakeAdb(failure)
                args = SimpleNamespace(serial="NO-DEVICE-CALLS", pid=123, seconds=.001, out=Path(temporary) / "capture")
                with patch.object(measurement, "Adb", return_value=fake), \
                        patch.object(measurement, "verify", new=AsyncMock(return_value=identity)), \
                        patch.object(measurement, "receipt", new=AsyncMock(return_value={})), \
                        patch.object(measurement, "epoch", new=AsyncMock(return_value="100.0")):
                    if failure:
                        with self.assertRaises(RuntimeError): await measurement.capture(args)
                    else:
                        await measurement.capture(args)
                self.assertEqual(sum(call[-1] == "-disable" for call in fake.calls), 1)
                report = json.loads((args.out / "receipt.json").read_text())
                self.assertEqual(report["result"], "FAILED" if failure else "CAPTURED")
                if not failure:
                    self.assertEqual(report["actual_present"]["unique_post_baseline_presents"], 2)
                    self.assertEqual(report["layers"][0]["average_fps_bucket_estimate"], 59.8)
                    self.assertNotIn("average_fps", report["layers"][0])
                    self.assertEqual(len((args.out / "present-timestamps.txt").read_text().splitlines()), 6)


if __name__ == "__main__":
    unittest.main()
