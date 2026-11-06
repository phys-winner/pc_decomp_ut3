package org.ut3.port;

import android.app.ActivityManager;
import android.app.GameManager;
import android.content.Context;
import android.content.Intent;
import android.content.IntentFilter;
import android.hardware.display.DisplayManager;
import android.os.BatteryManager;
import android.os.Build;
import android.os.PowerManager;
import android.system.Os;
import android.system.OsConstants;
import android.view.Display;
import java.io.File;
import java.io.IOException;
import java.nio.charset.StandardCharsets;
import java.nio.file.Files;
import java.util.ArrayList;
import java.util.Arrays;
import java.util.HashMap;
import java.util.List;
import java.util.Map;

/** The FPS counter's system lines (option fps_counter 2): what Android lets an app read, sampled once a
 *  second off the UI thread. CPU and GPU sensors are closed to apps (SELinux denies the thermal zones on
 *  the test phones), so heat is Android's thermal status and headroom and the battery's temperature. The
 *  GPU and its driver come from the launcher's Vulkan check (files/launcher/probe.txt). */
final class SystemInfo {
    private final Context context;
    private final String renderer;
    private final int width, height, fpsLimit;
    private final float refresh;
    private final Map<String, String> probe;
    private final String soc;
    private final long clockTick = Math.max(1, Os.sysconf(OsConstants._SC_CLK_TCK)), page = Math.max(1, Os.sysconf(OsConstants._SC_PAGESIZE));
    private final Map<String, Long> threadTicks = new HashMap<>();
    private long processTicks = -1, sampledNs;

    SystemInfo(Context context, String renderer, int width, int height, int fpsLimit, float refresh) {
        this.context = context.getApplicationContext();
        this.renderer = renderer;
        this.width = width;
        this.height = height;
        this.fpsLimit = fpsLimit;
        this.refresh = refresh;
        this.probe = FpsCounter.properties(read(new File(context.getFilesDir(), "launcher/probe.txt")));
        String model = Build.VERSION.SDK_INT >= 31 ? (Build.SOC_MANUFACTURER + " " + Build.SOC_MODEL).trim() : Build.HARDWARE;
        this.soc = model + " · " + cpuCount() + " cores";
    }

    /** One sample: {label, value} rows in the overlay's order; a row Android does not provide is left out. */
    String[][] sample() {
        List<String[]> rows = new ArrayList<>();
        rows.add(new String[]{"CPU:", soc});
        String clocks = FpsCounter.clocks(clusterKHz());
        if (!clocks.isEmpty()) rows.add(new String[]{"Clock:", clocks});
        String load = load();
        if (!load.isEmpty()) rows.add(new String[]{"Load:", load});
        /* OpenGL ES: the context's GL_RENDERER and GL_VERSION; DXVK: the Vulkan device and driver the
           launcher's check saw (the Turnip mode's own entries when it checked Turnip). */
        String gpu, driver;
        if ("gles".equals(renderer)) {
            String[] gl = (GameActivity.nativeGpuStrings() + "\n").split("\n", -1);
            gpu = gl[0];
            driver = gl[1];
        } else {
            String prefix = "dxvk-turnip".equals(renderer) && probe.containsKey("turnip.device") ? "turnip." : "stock.";
            gpu = probe.getOrDefault(prefix + "device", "");
            driver = probe.getOrDefault(prefix + "driver", "");
            if ("dxvk-turnip".equals(renderer) && !"turnip.".equals(prefix)) {
                String own = Os.getenv("PORT_TURNIP_DRIVER");
                driver = own != null && !own.isEmpty() ? "Turnip: " + new File(own).getName() : "Mesa Turnip of the port";
            }
        }
        if (!gpu.isEmpty()) rows.add(new String[]{"GPU:", FpsCounter.shorten(gpu, 44)});
        if (!driver.isEmpty()) rows.add(new String[]{"Driver:", FpsCounter.shorten(driver, 44)});
        rows.add(new String[]{"API:", api()});
        rows.add(new String[]{"RAM:", memory()});
        rows.add(new String[]{"Thermal:", thermal()});
        String battery = battery();
        if (!battery.isEmpty()) rows.add(new String[]{"Battery:", battery});
        rows.add(new String[]{"Screen:", FpsCounter.screen(width, height, refresh(), fpsLimit)});
        return rows.toArray(new String[0][]);
    }

    /** The display's current refresh rate (it follows the game's frame-rate wish after the start). */
    private float refresh() {
        DisplayManager displays = context.getSystemService(DisplayManager.class);
        Display display = displays != null ? displays.getDisplay(Display.DEFAULT_DISPLAY) : null;
        return display != null ? display.getRefreshRate() : refresh;
    }

    private String api() {
        if ("gles".equals(renderer)) return "Direct3D 9 of the port on OpenGL ES";
        String vulkan = probe.getOrDefault("dxvk-turnip".equals(renderer) ? "turnip.api" : "stock.api", "");
        String name = "dxvk-turnip".equals(renderer) ? "DXVK on Turnip" : "DXVK";
        return vulkan.isEmpty() ? name : name + " · Vulkan " + vulkan;
    }

    /** The game's CPU time over the last second in cores, and its three busiest threads. */
    private String load() {
        long now = System.nanoTime();
        String self = read(new File("/proc/self/stat"));
        long ticks = FpsCounter.statTicks(self);
        Map<String, Long> threads = new HashMap<>();
        File[] tasks = new File("/proc/self/task").listFiles();
        if (tasks != null) for (File task : tasks) {
            String stat = read(new File(task, "stat"));
            long t = FpsCounter.statTicks(stat);
            if (t >= 0) threads.put(task.getName() + "\t" + FpsCounter.statName(stat), t);
        }
        String result = "";
        if (processTicks >= 0 && ticks >= processTicks && now > sampledNs) {
            double seconds = (now - sampledNs) / 1e9;
            List<String[]> busy = new ArrayList<>();
            for (Map.Entry<String, Long> e : threads.entrySet()) {
                Long before = threadTicks.get(e.getKey());
                if (before == null || e.getValue() < before) continue;
                double percent = 100.0 * (e.getValue() - before) / clockTick / seconds;
                busy.add(new String[]{e.getKey().substring(e.getKey().indexOf('\t') + 1), Double.toString(percent)});
            }
            result = FpsCounter.load((ticks - processTicks) / (double) clockTick / seconds, busy);
        }
        processTicks = ticks;
        sampledNs = now;
        threadTicks.clear();
        threadTicks.putAll(threads);
        return result;
    }

    private String memory() {
        String statm = read(new File("/proc/self/statm"));
        long resident = 0;
        String[] fields = statm.trim().split("\\s+");
        if (fields.length > 1) {
            try { resident = Long.parseLong(fields[1]) * page; } catch (NumberFormatException ignored) { }
        }
        Map<String, Long> info = FpsCounter.meminfo(read(new File("/proc/meminfo")));
        long total = info.getOrDefault("MemTotal", 0L), available = info.getOrDefault("MemAvailable", -1L);
        if (total == 0 || available < 0) {
            ActivityManager.MemoryInfo m = new ActivityManager.MemoryInfo();
            ((ActivityManager) context.getSystemService(Context.ACTIVITY_SERVICE)).getMemoryInfo(m);
            total = m.totalMem;
            available = m.availMem;
        }
        return FpsCounter.memory(resident, available, total);
    }

    private String thermal() {
        PowerManager power = context.getSystemService(PowerManager.class);
        int status = power != null ? power.getCurrentThermalStatus() : -1;
        float headroom = power != null && Build.VERSION.SDK_INT >= 30 ? power.getThermalHeadroom(10) : Float.NaN;
        int mode = 0;
        if (Build.VERSION.SDK_INT >= 31) {
            GameManager games = context.getSystemService(GameManager.class);
            if (games != null) mode = games.getGameMode();
        }
        return FpsCounter.thermal(status, headroom, mode);
    }

    private String battery() {
        Intent sticky = context.registerReceiver(null, new IntentFilter(Intent.ACTION_BATTERY_CHANGED));
        if (sticky == null) return "";
        BatteryManager manager = context.getSystemService(BatteryManager.class);
        int current = manager != null ? manager.getIntProperty(BatteryManager.BATTERY_PROPERTY_CURRENT_NOW) : Integer.MIN_VALUE;
        return FpsCounter.battery(sticky.getIntExtra(BatteryManager.EXTRA_LEVEL, -1), sticky.getIntExtra(BatteryManager.EXTRA_SCALE, -1),
            sticky.getIntExtra(BatteryManager.EXTRA_TEMPERATURE, Integer.MIN_VALUE), sticky.getIntExtra(BatteryManager.EXTRA_VOLTAGE, 0),
            current, sticky.getIntExtra(BatteryManager.EXTRA_STATUS, -1) == BatteryManager.BATTERY_STATUS_CHARGING);
    }

    /** The current clock of every cpufreq policy (a cluster), in policy order. */
    private static long[] clusterKHz() {
        File[] policies = new File("/sys/devices/system/cpu/cpufreq").listFiles((dir, name) -> name.matches("policy\\d+"));
        if (policies == null) return new long[0];
        Arrays.sort(policies, (a, b) -> Integer.compare(Integer.parseInt(a.getName().substring(6)), Integer.parseInt(b.getName().substring(6))));
        long[] khz = new long[policies.length];
        int n = 0;
        for (File policy : policies) {
            try { khz[n] = Long.parseLong(read(new File(policy, "scaling_cur_freq")).trim()); n++; } catch (NumberFormatException ignored) { }
        }
        return Arrays.copyOf(khz, n);
    }

    private static int cpuCount() {
        String possible = read(new File("/sys/devices/system/cpu/possible")).trim();
        int count = FpsCounter.cpuRangeCount(possible);
        return count > 0 ? count : Runtime.getRuntime().availableProcessors();
    }

    private static String read(File file) {
        try {
            return new String(Files.readAllBytes(file.toPath()), StandardCharsets.UTF_8);
        } catch (IOException | RuntimeException e) {
            return "";
        }
    }
}
