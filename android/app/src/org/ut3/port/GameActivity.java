package org.ut3.port;

import android.app.Activity;
import android.app.AlertDialog;
import android.content.Intent;
import android.content.IntentFilter;
import android.os.BatteryManager;
import android.net.Uri;
import android.os.Build;
import android.os.Bundle;
import android.os.Environment;
import android.os.Handler;
import android.provider.Settings;
import android.view.Gravity;
import android.view.KeyEvent;
import android.view.MotionEvent;
import android.view.Surface;
import android.view.SurfaceHolder;
import android.view.SurfaceView;
import android.view.View;
import android.view.WindowManager;
import android.widget.FrameLayout;
import android.widget.TextView;
import android.widget.Toast;
import java.io.File;
import java.io.IOException;
import java.io.InputStream;
import java.io.ByteArrayOutputStream;
import java.nio.charset.StandardCharsets;
import java.util.concurrent.ExecutorService;
import java.util.concurrent.Executors;
import org.ut3.port.launcher.Launch;

public final class GameActivity extends Activity implements SurfaceHolder.Callback {
    static { System.loadLibrary("ut3"); }
    static native void nativeSurface(Surface surface);
    static native int nativeStart(String userdata, String renderer, boolean fixedSeed, int renderWidth, int renderHeight);
    static native int nativeStatus();
    static native void nativePause(boolean paused);
    static native void nativeKey(int vk, boolean down);
    static native boolean nativePointer(float x, float y, boolean relative);
    static native boolean nativeUiBegin(long context);
    static native int nativeUiControl(long context);
    static native boolean nativeUiScroll(int steps, long context);
    static native void nativeButton(boolean down);
    static native void nativePower(int plugged, int present, int level, int scale, int status);
    static native boolean nativeTouch(float moveX, float moveY, float lookX, float lookY, int heldMask, long context);
    /** A finger or a pad button went down: skips the movie the engine shows, if it may be skipped. */
    static native void nativeSkipMovie();
    /** Whether the engine shows a full-screen movie (startup, loading, campaign). */
    static native boolean nativeMoviePlaying();
    /** The game's rumble for the gamepad: left motor speed in the high 16 bits, right in the low, 0-65535. */
    static native int nativeRumble();
    static native boolean nativeTouchStick(float x, float y, long context);
    /** The physical gamepad's XINPUT_GAMEPAD (sticks up positive, triggers 0-255): the game's
     *  controller 0 during play. True while play shows it. */
    static native boolean nativeGamepad(boolean present, int buttons, int leftTrigger, int rightTrigger,
                                        int leftX, int leftY, int rightX, int rightY);
    /** Weapon bar swipe: 1 preview a slot, 2 switch to it, 3 cancel. */
    static native boolean nativeTouchWeapon(int phase, int slot, long context);
    /** Weapon bar slots (low word) and vehicle flags (high word) published by the game script. */
    static native long nativeTouchDetails();
    static native void nativeTouchReset();
    static native long nativeTouchContext();
    static native int nativeTouchVehicle();
    static native void nativeSettings();
    static native boolean nativeText(String text, long context);
    static native boolean nativeDelete(int count, long context);
    static native int nativeConfigure(int fps, int queryWaitUs, int precacheWaitUs, boolean benchmark);

    private final Handler handler = new Handler();
    private TextView status;
    private GameControls controls;
    private PortPreferences portPreferences;
    private final ExecutorService settingsIO = Executors.newSingleThreadExecutor();
    private boolean settingsRefreshPending;
    private boolean surfaceReady, started;
    private String renderer;
    private int powerTicks;
    private int displayWidth, displayHeight;
    /* The launcher's record of this start (first frame, exit) and its update notice; null when the
       APK carries no launcher. */
    private Launch launch;
    private FrameLayout root;
    /* Wi-Fi chips drop broadcast frames to save power unless an app holds this: the LAN game finds
       and answers its matches by UDP broadcast (the original LAN beacon, port 14001). */
    private android.net.wifi.WifiManager.MulticastLock lanLock;

    PortPreferences portSettings() { return portPreferences; }

    /** The activity's own messages in the launcher's language (English without the launcher). */
    private String say(String english, String russian, String german, String polish) {
        String language = launch != null ? launch.language() : java.util.Locale.getDefault().getLanguage();
        switch (language) {
        case "ru": case "uk": case "be": return russian;
        case "de": return german;
        case "pl": return polish;
        default: return english;
        }
    }

    /** The game data folder: the launcher's part, else the port's fixed folder. The native loader
     *  makes the PE images from its Binaries. */
    private File gameFolder() {
        File folder = launch != null ? launch.folder("game") : null;
        return folder != null ? folder : new File("/sdcard/UT3/game");
    }

    private android.os.ParcelFileDescriptor archive;

    /** The game part played straight from a stored ZIP (PORT_MOUNT_GAME): Java's reads of it go through
     *  the archive, like the native file layer's. */
    private void attachArchive() {
        String index = android.system.Os.getenv("PORT_MOUNT_GAME"), descriptor = android.system.Os.getenv("PORT_MOUNT_FD_GAME");
        if (index == null || index.isEmpty() || descriptor == null || descriptor.isEmpty()) return;
        try {
            archive = android.os.ParcelFileDescriptor.fromFd(Integer.parseInt(descriptor));
            GameFiles.mount(new File(index), new java.io.FileInputStream(archive.getFileDescriptor()).getChannel());
        } catch (IOException | RuntimeException e) {
            android.util.Log.w("UT3", "game archive: " + e);
        }
    }

    /** A start the game refuses before native code: shown here and on the launcher's card. */
    private void refuse(String message) {
        status.setText(message);
        status.setVisibility(View.VISIBLE);
        if (launch != null) launch.failed(message);
    }

    private byte[] asset(String name) throws IOException {
        try (InputStream in = getAssets().open(name); ByteArrayOutputStream out = new ByteArrayOutputStream()) {
            byte[] buffer = new byte[8192];
            for (int count; (count = in.read(buffer)) != -1;) out.write(buffer, 0, count);
            return out.toByteArray();
        }
    }

    void savePortSettings(Runnable complete) {
        settingsIO.execute(() -> {
            try {
                portPreferences.save();
                handler.post(() -> { if (complete != null && !isFinishing()) complete.run(); });
            } catch (IOException error) {
                handler.post(() -> Toast.makeText(this, say("Could not save the settings: ", "Не удалось сохранить настройки: ",
                    "Die Einstellungen konnten nicht gespeichert werden: ", "Nie udało się zapisać ustawień: ") + error.getMessage(),
                    Toast.LENGTH_LONG).show());
            }
        });
    }

    void openPortSettings() {
        controls.release();
        if ((nativeTouchContext() & 3) == 2) {
            nativeKey(27, true);
            nativeKey(27, false);
        }
        nativeSettings();
    }

    private void refreshPortSettings() {
        if (settingsRefreshPending) return;
        settingsRefreshPending = true;
        settingsIO.execute(() -> {
            try {
                boolean changed = portPreferences.reloadIfChanged();
                handler.post(() -> {
                    settingsRefreshPending = false;
                    if (!isFinishing() && controls != null && changed) controls.requestSettingsReload();
                });
            } catch (IOException error) {
                handler.post(() -> { settingsRefreshPending = false; status.setText(say("Could not read the control settings: ",
                    "Не удалось прочитать настройки управления: ", "Die Steuerungseinstellungen konnten nicht gelesen werden: ",
                    "Nie udało się odczytać ustawień sterowania: ") + error.getMessage()); });
            }
        });
    }

    /** A phone starts with the largest interface size: the original menus and HUD are drawn for a
     *  monitor. Phone means a screen under 7 inches across, by the display's physical dpi (or, where
     *  a device reports no usable dpi, its smallest width under 600 dp). Only before the player chose. */
    private void phoneInterface() {
        /* Once: the game saved its 100% default before this build, which the player did not choose. */
        if (portPreferences.getBoolean("touchInterfaceDefault", false)) return;
        portPreferences.put("touchInterfaceDefault", "1");
        String current = portPreferences.getString("uiScale", null);
        if (current != null && Math.abs(portPreferences.getFloat("uiScale", 1) - 1) > .001f) { save(); return; }
        android.util.DisplayMetrics metrics = new android.util.DisplayMetrics();
        getWindowManager().getDefaultDisplay().getRealMetrics(metrics);
        boolean known = metrics.xdpi > 50 && metrics.ydpi > 50;
        double inches = known ? Math.hypot(metrics.widthPixels / metrics.xdpi, metrics.heightPixels / metrics.ydpi) : 0;
        boolean phone = known ? inches < 7.0 : getResources().getConfiguration().smallestScreenWidthDp < 600;
        android.util.Log.i("UT3", String.format(java.util.Locale.ROOT, "screen %.1f in (%dx%d, dpi %.0fx%.0f): %s",
            inches, metrics.widthPixels, metrics.heightPixels, metrics.xdpi, metrics.ydpi, phone ? "phone, interface 150%" : "tablet"));
        if (phone) portPreferences.put("uiScale", "1.5");
        save();
    }

    private void save() {
        try {
            portPreferences.save();
        } catch (IOException error) {
            android.util.Log.w("UT3", "settings: " + error);
        }
    }

    /** The render scale (ScreenPercentage, 25-100). A render height chosen in an older build becomes
     *  the matching scale once. */
    private int renderScale(int displayHeight) throws IOException {
        if (portPreferences.getString("renderScale", null) == null) {
            int height = portPreferences.getInt("renderHeight", 0);
            if (height == 480 || height == 720 || height == 1080) {
                int scale = Math.max(GameSettings.MIN_SCALE, Math.min(100, Math.round(100f * height / displayHeight)));
                portPreferences.put("renderScale", scale);
                portPreferences.save();
            }
        }
        return Math.max(GameSettings.MIN_SCALE, Math.min(100, portPreferences.getInt("renderScale", 100)));
    }

    private int fpsLimit() {
        int fps = portPreferences.getInt("fpsLimit", 60);
        if (fps == 0) {
            float refresh = getWindowManager().getDefaultDisplay().getRefreshRate();
            for (android.view.Display.Mode mode : getWindowManager().getDefaultDisplay().getSupportedModes())
                if (mode.getPhysicalWidth() == getWindowManager().getDefaultDisplay().getMode().getPhysicalWidth() &&
                    mode.getPhysicalHeight() == getWindowManager().getDefaultDisplay().getMode().getPhysicalHeight())
                    refresh = Math.max(refresh, mode.getRefreshRate());
            return Math.max(1, Math.min(1000, Math.round(refresh)));
        }
        return fps == 30 || fps == 45 || fps == 60 ? fps : 60;
    }
    private final Runnable progress = new Runnable() {
        @Override public void run() {
            if (!started) return;
            refreshPortSettings();
            if (powerTicks++ % 60 == 0) updatePowerSnapshot();
            int result = nativeStatus();
            if (result == Integer.MAX_VALUE) {
                status.setVisibility(View.GONE);
                if (launch != null) launch.firstFrame(root);
            } else if (result != Integer.MIN_VALUE) {
                /* A clean exit returns to the launcher; another code stays on screen. */
                if (launch != null) launch.exited(result);
                status.setText(String.format(java.util.Locale.ROOT, say("The game stopped: code %d. Log: userdata/ut3.log",
                    "Игра остановилась: код %d. Журнал: userdata/ut3.log", "Das Spiel wurde beendet: Code %d. Protokoll: userdata/ut3.log",
                    "Gra zatrzymała się: kod %d. Dziennik: userdata/ut3.log"), result));
                status.setVisibility(View.VISIBLE);
                return;
            }
            handler.postDelayed(this, 500);
        }
    };

    private void updatePowerSnapshot() {
        Intent battery = registerReceiver(null, new IntentFilter(Intent.ACTION_BATTERY_CHANGED));
        if (battery == null) { nativePower(-1, -1, -1, -1, 1); return; }
        nativePower(battery.getIntExtra(BatteryManager.EXTRA_PLUGGED, -1),
            battery.hasExtra(BatteryManager.EXTRA_PRESENT) ?
                (battery.getBooleanExtra(BatteryManager.EXTRA_PRESENT, false) ? 1 : 0) : -1,
            battery.getIntExtra(BatteryManager.EXTRA_LEVEL, -1),
            battery.getIntExtra(BatteryManager.EXTRA_SCALE, -1),
            battery.getIntExtra(BatteryManager.EXTRA_STATUS, BatteryManager.BATTERY_STATUS_UNKNOWN));
    }

    private void immersive() {
        getWindow().getDecorView().setSystemUiVisibility(View.SYSTEM_UI_FLAG_FULLSCREEN |
            View.SYSTEM_UI_FLAG_HIDE_NAVIGATION | View.SYSTEM_UI_FLAG_IMMERSIVE_STICKY |
            View.SYSTEM_UI_FLAG_LAYOUT_STABLE | View.SYSTEM_UI_FLAG_LAYOUT_FULLSCREEN |
            View.SYSTEM_UI_FLAG_LAYOUT_HIDE_NAVIGATION);
    }

    @Override public void onWindowFocusChanged(boolean focused) {
        super.onWindowFocusChanged(focused);
        if (focused) immersive();
        else if (controls != null) { controls.release(); controls.releaseGamepad(true); }
    }

    /* A gamepad that goes away mid-press sends no further edges. */
    private final android.hardware.input.InputManager.InputDeviceListener gamepads =
        new android.hardware.input.InputManager.InputDeviceListener() {
            @Override public void onInputDeviceAdded(int id) {}
            @Override public void onInputDeviceChanged(int id) {}
            @Override public void onInputDeviceRemoved(int id) {
                if (controls != null) controls.releaseGamepad(GameControls.gamepadAttached());
            }
        };

    @Override public void onCreate(Bundle state) {
        super.onCreate(state);
        try {
            launch = Launch.attach(this);
        } catch (RuntimeException noLauncher) {
            android.util.Log.w("UT3", "launcher: " + noLauncher);
        }
        attachArchive();
        try {
            PortCrypto.init(getExternalFilesDir(null));
            portPreferences = new PortPreferences(getExternalFilesDir(null));
        }
        catch (IOException error) {
            if (launch != null) launch.failed(error.getMessage());
            new AlertDialog.Builder(this).setTitle(say("UT3 storage", "Хранилище UT3", "UT3-Speicher", "Pamięć UT3")).setMessage(error.getMessage())
                .setPositiveButton(say("Close", "Закрыть", "Schließen", "Zamknij"), (dialog, which) -> {
                    if (launch != null) Launch.openLauncher(this, launch.distribution());
                    finish();
                }).setCancelable(false).show();
            return;
        }
        phoneInterface();
        try {
            android.net.wifi.WifiManager wifi = (android.net.wifi.WifiManager) getApplicationContext()
                .getSystemService(WIFI_SERVICE);
            if (wifi != null) {
                lanLock = wifi.createMulticastLock("UT3:lan");
                lanLock.setReferenceCounted(false);
            }
        } catch (RuntimeException e) {
            android.util.Log.w("UT3", "no Wi-Fi multicast lock: " + e);
        }
        getWindow().addFlags(WindowManager.LayoutParams.FLAG_KEEP_SCREEN_ON);
        WindowManager.LayoutParams windowOptions = getWindow().getAttributes();
        windowOptions.preferredRefreshRate = fpsLimit();
        windowOptions.layoutInDisplayCutoutMode = WindowManager.LayoutParams.LAYOUT_IN_DISPLAY_CUTOUT_MODE_SHORT_EDGES;
        getWindow().setAttributes(windowOptions);
        immersive();
        /* Target SDK 36+: the system's back is no longer a key or onBackPressed() on Android 16+. */
        if (Build.VERSION.SDK_INT >= 33)
            getOnBackInvokedDispatcher().registerOnBackInvokedCallback(android.window.OnBackInvokedDispatcher.PRIORITY_DEFAULT, this::back);
        root = new FrameLayout(this);
        SurfaceView surface = new SurfaceView(this);
        surface.getHolder().addCallback(this);
        root.addView(surface);
        controls = new GameControls(this);
        controls.setSettingsAction(this::openPortSettings);
        root.addView(controls);
        status = new TextView(this);
        status.setTextColor(0xFFFFFFFF);
        status.setBackgroundColor(0xC0000000);
        status.setTextSize(18);
        status.setPadding(24, 16, 24, 16);
        status.setGravity(Gravity.CENTER);
        FrameLayout.LayoutParams label = new FrameLayout.LayoutParams(-1, -2, Gravity.BOTTOM);
        root.addView(status, label);
        status.setOnClickListener(v -> {
            if (Build.VERSION.SDK_INT >= 30 && !Environment.isExternalStorageManager()) {
                Intent intent = new Intent(Settings.ACTION_MANAGE_APP_ALL_FILES_ACCESS_PERMISSION,
                                           Uri.parse("package:" + getPackageName()));
                startActivity(intent);
            }
        });
        setContentView(root);
        String requested = getIntent().getStringExtra("renderer");
        if ("gles".equals(requested) || "dxvk".equals(requested) || "dxvk-turnip".equals(requested)) {
            renderer = requested;
        } else {
            String saved = portPreferences.getString("renderer", "gles");
            renderer = "dxvk".equals(saved) || "dxvk-turnip".equals(saved) ? saved : "gles";
            startIfReady();
        }
    }

    private void startIfReady() {
        if (started || !surfaceReady || renderer == null || displayWidth <= 0 || displayHeight <= 0 || displayWidth < displayHeight) return;
        File game = gameFolder();
        if (Build.VERSION.SDK_INT >= 30 && !Environment.isExternalStorageManager()) {
            refuse(String.format(say("UT3 needs access to its files. Tap to allow access to %s.",
                "Нужен доступ к файлам UT3. Нажмите, чтобы разрешить доступ к %s.",
                "UT3 braucht Zugriff auf seine Dateien. Tippen Sie, um den Zugriff auf %s zu erlauben.",
                "UT3 potrzebuje dostępu do swoich plików. Dotknij, aby zezwolić na dostęp do %s."), game.getParent()));
            return;
        }
        if (Build.VERSION.SDK_INT == 29 && checkSelfPermission("android.permission.READ_EXTERNAL_STORAGE") != 0) {
            requestPermissions(new String[]{"android.permission.READ_EXTERNAL_STORAGE", "android.permission.WRITE_EXTERNAL_STORAGE"}, 1);
            return;
        }
        if (!GameFiles.isDirectory(game, "UTGame")) {
            refuse(String.format(say("Game data not found: %s.", "Не найдены данные игры: %s.",
                "Spieldaten nicht gefunden: %s.", "Nie znaleziono danych gry: %s."), game));
            return;
        }
        File external = getExternalFilesDir(null);
        if (external == null) {
            refuse(say("The app's storage is unavailable.", "Хранилище приложения недоступно.",
                "Der Speicher der App ist nicht verfügbar.", "Pamięć aplikacji jest niedostępna."));
            return;
        }
        File userdata = new File(external, "userdata");
        if (!userdata.isDirectory() && !userdata.mkdirs()) {
            refuse(say("Could not create the userdata folder.", "Не удалось создать папку userdata.",
                "Der Ordner userdata konnte nicht angelegt werden.", "Nie udało się utworzyć folderu userdata."));
            return;
        }
        int renderWidth, renderHeight;
        try {
            if ("dxvk-turnip".equals(renderer)) {
                File debug = new File(userdata, "tu-debug.txt");
                if (!debug.exists() && !debug.createNewFile()) throw new IOException("Cannot create driver diagnostics file");
            }
            /* The display's resolution: the render scale lowers only the 3D scene, never the interface. */
            renderHeight = displayHeight;
            renderWidth = GameSettings.renderWidth(displayWidth, displayHeight, renderHeight);
            File engine = new File(userdata, "Documents/My Games/Unreal Tournament 3/UTGame/Config/UTEngine.ini");
            GameSettings.prepare(engine, game, renderWidth, renderHeight, fpsLimit(), portPreferences.getString("language", "INT"),
                renderer, renderScale(displayHeight));
            String options = "RUS".equals(portPreferences.getString("language", "INT")) ? "AndroidPortOptions-RUS.ini" : "AndroidPortOptions.ini";
            GameSettings.prepareNativeUi(engine, game, userdata,
                asset("AndroidPort.u"), new String(asset(options), StandardCharsets.UTF_8));
        } catch (IOException | IllegalArgumentException error) {
            refuse(say("Could not prepare the game's settings: ", "Не удалось подготовить настройки игры: ",
                "Die Spieleinstellungen konnten nicht vorbereitet werden: ", "Nie udało się przygotować ustawień gry: ") + error.getMessage());
            return;
        }
        int error = nativeConfigure(fpsLimit(), getIntent().getIntExtra("queryWaitUs", 0),
            getIntent().getIntExtra("precacheWaitUs", 50), getIntent().getBooleanExtra("benchmark", false));
        if (error == 0) error = nativeStart(userdata.getAbsolutePath(), renderer, getIntent().getBooleanExtra("fixedSeed", false), renderWidth, renderHeight);
        if (error != 0) {
            refuse(say("Could not start the game thread: ", "Не удалось запустить игровой поток: ",
                "Der Spiel-Thread konnte nicht gestartet werden: ", "Nie udało się uruchomić wątku gry: ") + error);
            return;
        }
        started = true;
        if (launch != null) launch.starting();
        status.setClickable(false);
        status.setText(say("Loading Unreal Tournament 3 — ", "Загрузка Unreal Tournament 3 — ", "Unreal Tournament 3 wird geladen — ",
            "Wczytywanie Unreal Tournament 3 — ") + (renderer.startsWith("dxvk") ? "DXVK (Vulkan)" : "OpenGL ES") + "…");
        handler.post(progress);
    }

    @Override public void surfaceCreated(SurfaceHolder holder) {
        nativeSurface(holder.getSurface());
        surfaceReady = true;
        startIfReady();
    }
    @Override public void surfaceChanged(SurfaceHolder holder, int format, int width, int height) {
        displayWidth = width;
        displayHeight = height;
        nativeSurface(holder.getSurface());
        startIfReady();
    }
    @Override public void surfaceDestroyed(SurfaceHolder holder) {
        surfaceReady = false;
        controls.release();
        controls.releaseGamepad(true);
        nativeSurface(null);
    }
    @Override protected void onResume() {
        super.onResume();
        if (portPreferences != null) {
            ((android.hardware.input.InputManager) getSystemService(INPUT_SERVICE)).registerInputDeviceListener(gamepads, handler);
            if (lanLock != null && !lanLock.isHeld()) lanLock.acquire();
            updatePowerSnapshot(); nativePause(false); startIfReady();
        }
    }
    @Override protected void onPause() {
        if (portPreferences != null)
            ((android.hardware.input.InputManager) getSystemService(INPUT_SERVICE)).unregisterInputDeviceListener(gamepads);
        if (lanLock != null && lanLock.isHeld()) lanLock.release();
        if (controls != null) { controls.release(); controls.releaseGamepad(true); }
        nativePause(true); super.onPause();
    }
    @Override protected void onDestroy() { handler.removeCallbacks(progress); settingsIO.shutdown(); super.onDestroy(); }
    @Override public void onRequestPermissionsResult(int request, String[] permissions, int[] results) {
        super.onRequestPermissionsResult(request, permissions, results);
        if (request == 1 && results.length > 0 && results[0] == 0) startIfReady();
    }
    @Override public void onBackPressed() { back(); }

    /** Back before the game started (files or access missing) leads to the launcher; in the game it
     *  closes the touch layer's panel or is the game's Escape. */
    private void back() {
        if (!started) {
            if (launch != null) {
                Launch.openLauncher(this, launch.distribution());
                finish();
            } else {
                super.onBackPressed();
            }
            return;
        }
        if (controls != null && controls.handleBack()) return;
        nativeKey(27, true);
        nativeKey(27, false);
    }
    private long backDownTime = -1;
    private boolean backConsumed;
    @Override public boolean dispatchGenericMotionEvent(MotionEvent event) {
        if (started && controls != null && controls.gamepadMotion(event)) return true;
        return super.dispatchGenericMotionEvent(event);
    }
    @Override public boolean dispatchKeyEvent(KeyEvent event) {
        int key = event.getKeyCode(), vk = 0;
        if (started && controls != null && key != KeyEvent.KEYCODE_BACK && controls.gamepadKey(event)) return true;
        if (key == KeyEvent.KEYCODE_BACK) {
            if (!started) {
                if (event.getAction() == KeyEvent.ACTION_UP) back();
                return true;
            }
            if (event.getAction() == KeyEvent.ACTION_DOWN) {
                if (backDownTime != event.getDownTime()) {
                    backDownTime = event.getDownTime();
                    backConsumed = controls != null && controls.handleBack();
                }
                if (!backConsumed) nativeKey(27, true);
                return true;
            }
            if (event.getAction() == KeyEvent.ACTION_UP) {
                if (backDownTime == event.getDownTime()) {
                    if (!backConsumed) nativeKey(27, false);
                    backDownTime = -1;
                    backConsumed = false;
                }
                return true;
            }
        }
        if (key >= KeyEvent.KEYCODE_A && key <= KeyEvent.KEYCODE_Z) vk = 'A' + key - KeyEvent.KEYCODE_A;
        else if (key >= KeyEvent.KEYCODE_0 && key <= KeyEvent.KEYCODE_9) vk = '0' + key - KeyEvent.KEYCODE_0;
        else if (key >= KeyEvent.KEYCODE_F1 && key <= KeyEvent.KEYCODE_F12) vk = 0x70 + key - KeyEvent.KEYCODE_F1;
        else if (key == KeyEvent.KEYCODE_SPACE) vk = 32;
        else if (key == KeyEvent.KEYCODE_ENTER) vk = 13;
        else if (key == KeyEvent.KEYCODE_GRAVE) vk = 0xC0;
        else if (key == KeyEvent.KEYCODE_MINUS) vk = 0xBD;
        else if (key == KeyEvent.KEYCODE_PERIOD) vk = 0xBE;
        else if (key == KeyEvent.KEYCODE_DEL) vk = 8;
        else if (key == KeyEvent.KEYCODE_ESCAPE) vk = 27;
        else if (key == KeyEvent.KEYCODE_DPAD_UP) vk = 0x26;
        else if (key == KeyEvent.KEYCODE_DPAD_DOWN) vk = 0x28;
        else if (key == KeyEvent.KEYCODE_DPAD_LEFT) vk = 0x25;
        else if (key == KeyEvent.KEYCODE_DPAD_RIGHT) vk = 0x27;
        else if (key == KeyEvent.KEYCODE_TAB) vk = 9;
        if (vk != 0 && (event.getAction() == KeyEvent.ACTION_DOWN || event.getAction() == KeyEvent.ACTION_UP)) {
            nativeKey(vk, event.getAction() == KeyEvent.ACTION_DOWN);
            return true;
        }
        return super.dispatchKeyEvent(event);
    }
}
