"""HOST-check the FPS counter's numbers (android/app/src/org/ut3/port/FpsCounter.java).

DXVK 2.4.1's HudFpsItem and HudFrameTimeItem (src/dxvk/hud/dxvk_hud_item.cpp) and encodeSrgb
(src/dxvk/hud/shaders/hud_frag_common.glsl) are transcribed below in float32 arithmetic; FpsCounter (plain
Java, javac --release 8) must give the same frame rate text, bar heights and colours and min/max text. The
system lines' parsing and text (SystemInfo's /proc and sysfs files, PowerManager, BatteryManager values)
are checked on fixed inputs. No device."""
import math
import os
from pathlib import Path
import struct
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]

CHECK = r'''
package org.ut3.port;
import java.util.*;
public class FpsCounterCheck {
    static void eq(Object got, Object want) {
        if (!String.valueOf(got).equals(String.valueOf(want))) throw new AssertionError("got <" + got + "> want <" + want + ">");
    }
    static void system() {
        Map<String, String> probe = FpsCounter.properties("google/frankel|6:1|\nstock.status=ok\nstock.device=PowerVR D-Series DXT-48-1536 MC1\n"
            + "stock.driver=PowerVR D-Series Vulkan Driver 26.1@7036548\nstock.api=1.4.344\nnot a pair\n");
        eq(probe.get("stock.device"), "PowerVR D-Series DXT-48-1536 MC1");
        eq(probe.get("stock.api"), "1.4.344");
        eq(probe.size(), 4);
        eq(FpsCounter.shorten("Mali-G715 v1.r54p3-00eac0.03d8d836cbf5c9f29d765e58a6bdfb98", 20), "Mali-G715 v1.r54p3-…");
        eq(FpsCounter.shorten("short", 20), "short");
        eq(FpsCounter.clocks(new long[]{729000, 921000, 921000, 2342000}), "0.73 · 0.92 · 0.92 · 2.34 GHz");
        eq(FpsCounter.clocks(new long[0]), "");
        String stat = "12906 (Thread-3) S 1189 1189 0 0 -1 4194624 92 0 0 0 6231 102 0 0 10 -10 61 0 1234 0 0";
        eq(FpsCounter.statTicks(stat), 6333);
        eq(FpsCounter.statName(stat), "Thread-3");
        String odd = "77 (a (b) c) R 1 1 0 0 -1 0 0 0 0 0 5 7 0 0 20 0 1 0 1 0 0";
        eq(FpsCounter.statTicks(odd), 12);
        eq(FpsCounter.statName(odd), "a (b) c");
        eq(FpsCounter.statTicks(""), -1);
        eq(FpsCounter.statTicks("1 (x) S 1 2"), -1);
        List<String[]> busy = new ArrayList<>();
        busy.add(new String[]{"AudioTrack", "24.4"});
        busy.add(new String[]{"Thread-5", "98.2"});
        busy.add(new String[]{"d3d9-render", "66.5"});
        busy.add(new String[]{"idle", "0.2"});
        eq(FpsCounter.load(1.94, busy), "game 1.9 cores · Thread-5 98%, d3d9-render 67%, AudioTrack 24%");
        eq(FpsCounter.load(0.04, Collections.singletonList(new String[]{"idle", "0.2"})), "game 0.0 cores");
        Map<String, Long> mem = FpsCounter.meminfo("MemTotal:       11821596 kB\nMemFree:          344460 kB\nMemAvailable:    1958796 kB\n");
        eq(mem.get("MemTotal"), 11821596L * 1024);
        eq(mem.get("MemAvailable"), 1958796L * 1024);
        eq(FpsCounter.memory(2481000000L, mem.get("MemAvailable"), mem.get("MemTotal")), "game 2.31 GB · free 1.87 / 11.27 GB");
        eq(FpsCounter.thermal(3, 1.04f, 1), "severe · 104% of the limit · mode standard");
        eq(FpsCounter.thermal(0, Float.NaN, 0), "none");
        eq(FpsCounter.thermal(9, 0.5f, 2), "unknown · 50% of the limit · mode performance");
        eq(FpsCounter.battery(78, 100, 334, 4012, -850000, false), "78% · 33.4 °C · 3.4 W");
        eq(FpsCounter.battery(50, 200, 301, 4400, 1200000, true), "25% · 30.1 °C · 5.3 W · charging");
        eq(FpsCounter.battery(-1, -1, Integer.MIN_VALUE, 0, Integer.MIN_VALUE, false), "");
        eq(FpsCounter.screen(2424, 1080, 120.00001f, 60), "2424x1080 · 120 Hz · limit 60 FPS");
        eq(FpsCounter.screen(2944, 1840, 0, 0), "2944x1840");
        eq(FpsCounter.cpuRangeCount("0-7"), 8);
        eq(FpsCounter.cpuRangeCount("0-3,6"), 5);
        eq(FpsCounter.cpuRangeCount("junk"), 0);
        System.out.println("PASS system");
    }
    public static void main(String[] args) {
        StringBuilder out = new StringBuilder();
        for (String a : args) {
            String[] p = a.split(":");
            switch (p[0]) {
            case "rate": out.append(FpsCounter.frameRate(Long.parseLong(p[1]), Long.parseLong(p[2]))); break;
            case "height": out.append(FpsCounter.height(Float.parseFloat(p[1]))); break;
            case "color": out.append(Integer.toHexString(FpsCounter.color(Float.parseFloat(p[1])))); break;
            case "ms": out.append(FpsCounter.milliseconds(FpsCounter.tenths(Integer.parseInt(p[1])))); break;
            case "scale": out.append(FpsCounter.hudScale(Float.parseFloat(p[1]))); break;
            case "srgb": out.append(FpsCounter.srgb(Integer.parseInt(p[1]))); break;
            case "system": system(); continue;
            default: throw new IllegalArgumentException(a);
            }
            out.append('\n');
        }
        System.out.print(out);
    }
}
'''


def f32(x):
    return struct.unpack("<f", struct.pack("<f", x))[0]


def dxvk_rate(frames, elapsed_us):
    fps = (10_000_000 * frames) // elapsed_us
    return f"{fps // 10}.{fps % 10}"


def srgb(unorm):
    linear = unorm / 255.0
    encoded = linear * 12.92 if linear <= 0.0031308 else linear ** (5.0 / 12.0) * 1.055 - 0.055
    return round(min(max(encoded, 0.0), 1.0) * 255.0)


def dxvk_point(us):
    """HudFrameTimeItem::render for one data point: (height, (r, g, b) as encoded for an sRGB swapchain)."""
    target, min_us, max_us = f32(16666.6), f32(5000.0), f32(100000.0)
    us = f32(us)
    r = min(max(f32(-1.0 + us / target), 0.0), 1.0)
    g = min(max(f32(3.0 - us / target), 0.0), 1.0)
    length = f32(math.sqrt(r * r + g * g))
    red, green = int(f32(255.0 * f32(r / length))), int(f32(255.0 * f32(g / length)))
    h = math.log2(max(f32((us - min_us) / target + 1.0), 1.0)) / math.log2(f32((max_us - min_us) / target))
    return min(max(h, 1.0 / 40.0), 1.0), (srgb(red), srgb(green), 0)


class FpsCounterTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.folder = Path(tempfile.mkdtemp(prefix="ut3-fps-counter-"))
        java = Path(os.environ["JAVA_HOME"]) / "bin"
        source = cls.folder / "FpsCounterCheck.java"
        source.write_text(CHECK, encoding="utf-8")
        subprocess.run([java / "javac.exe", "--release", "8", "-encoding", "UTF-8", "-d", cls.folder,
                        ROOT / "android/app/src/org/ut3/port/FpsCounter.java", source], check=True, capture_output=True)
        cls.java = java / "java.exe"

    def run_check(self, *args):
        result = subprocess.run([self.java, "-ea", "-Dfile.encoding=UTF-8", "-Dstdout.encoding=UTF-8", "-cp", self.folder,
                                 "org.ut3.port.FpsCounterCheck", *args], capture_output=True, timeout=60)
        self.assertEqual(result.returncode, 0, result.stderr.decode("utf-8", "replace"))
        return result.stdout.decode("utf-8").splitlines()

    def test_frame_rate_text(self):
        cases = [(30, 500_000), (120, 2_000_000), (100, 1_500_000), (7, 1_000_000), (1, 734_211), (61, 501_337)]
        got = self.run_check(*(f"rate:{f}:{e}" for f, e in cases))
        self.assertEqual(got, [dxvk_rate(f, e) for f, e in cases])
        self.assertEqual(self.run_check("rate:0:0", "rate:5:0"), ["0.0", "0.0"])

    def test_graph_points_match_dxvk(self):
        times = [0, 1000, 5000, 8333, 11111, 16666, 16667, 20000, 25000, 33333, 40000, 50000, 66666, 80000, 100000, 250000, 10_000_000]
        heights = self.run_check(*(f"height:{us}" for us in times))
        colors = self.run_check(*(f"color:{us}" for us in times))
        for us, height, color in zip(times, heights, colors):
            want_height, (r, g, b) = dxvk_point(us)
            self.assertAlmostEqual(float(height), want_height, places=5, msg=us)
            got = int(color, 16)
            self.assertEqual(got >> 24 & 0xFF, 0xFF, us)
            for shift, want in ((16, r), (8, g), (0, b)):
                self.assertLessEqual(abs((got >> shift & 0xFF) - want), 1, (us, hex(got), (r, g, b)))
        # The known corners: green up to the 60 FPS frame, red from 15 FPS, the floor of 1/40, full above 100 ms.
        self.assertEqual(colors[5], "ff00ff00")
        self.assertEqual(colors[12], "ffff0000")
        self.assertEqual(float(heights[0]), 1 / 40)
        self.assertEqual(float(heights[-1]), 1.0)

    def test_min_max_text_and_srgb(self):
        self.assertEqual(self.run_check("ms:0", "ms:16666", "ms:8333", "ms:123456"), ["0.0", "16.6", "8.3", "123.4"])
        self.assertEqual(self.run_check("srgb:0", "srgb:64", "srgb:180", "srgb:255"), ["0", "137", "219", "255"])

    def test_hud_scale(self):
        self.assertEqual(self.run_check("scale:2.625", "scale:2.0", "scale:1.0", "scale:3.5", "scale:9"),
                         ["2.0", "1.5", "1.0", "2.75", "4.0"])

    def test_system_lines(self):
        self.assertEqual(self.run_check("system"), ["PASS system"])


if __name__ == "__main__":
    unittest.main()
