package org.ut3.port;

import android.app.Activity;
import android.app.ActivityManager;
import android.app.AlertDialog;
import android.app.ApplicationExitInfo;
import android.content.ClipData;
import android.content.ClipboardManager;
import android.content.Intent;
import android.os.Build;
import android.os.Bundle;
import android.os.Handler;
import android.widget.Toast;
import java.io.File;
import java.io.IOException;
import java.io.InputStream;
import java.io.ByteArrayOutputStream;
import java.nio.charset.StandardCharsets;
import java.util.Locale;
import org.ut3.port.launcher.Launch;

/** The task's parent in a separate process: Android returns here even after a native crash. */
public final class GameWatchActivity extends Activity {
    private long started;
    private boolean returned, shown;
    private int exitCode;

    private File userdata() { return new File(getExternalFilesDir(null), "userdata"); }
    private File session() { return new File(getFilesDir(), "ut3-crash-session.txt"); }
    private String say(String english, String russian) {
        String language = getIntent().getStringExtra("crash.language");
        if (language == null) language = Locale.getDefault().getLanguage();
        return language.startsWith("ru") ? russian : english;
    }

    @Override public void onCreate(Bundle state) {
        super.onCreate(state);
        if (state != null) {
            started = state.getLong("started");
            returned = state.getBoolean("returned");
            exitCode = state.getInt("exitCode");
            return;
        }
        try {
            String previous = CrashReport.tail(session(), 128).trim();
            if (!previous.isEmpty()) {
                started = Long.parseLong(previous);
                if (report(false)) return;
            }
            started = System.currentTimeMillis();
            CrashReport.write(session(), Long.toString(started));
            CrashReport.write(new File(userdata(), "ut3-crash.txt"), "");
        } catch (IOException | NumberFormatException error) {
            android.util.Log.e("UT3", "crash monitor storage", error);
            started = System.currentTimeMillis();
        }
        Intent game = new Intent(this, GameActivity.class);
        if (getIntent().getExtras() != null) game.putExtras(getIntent().getExtras());
        game.putExtra("crash.watched", true);
        startActivityForResult(game, 1);
    }

    @Override protected void onSaveInstanceState(Bundle state) {
        super.onSaveInstanceState(state);
        state.putLong("started", started);
        state.putBoolean("returned", returned);
        state.putInt("exitCode", exitCode);
    }

    @Override protected void onActivityResult(int request, int result, Intent data) {
        super.onActivityResult(request, result, data);
        if (request != 1) return;
        returned = true;
        exitCode = data != null ? data.getIntExtra("crash.exitCode", 0) : 0;
        if (result == RESULT_OK && exitCode == 0) { close(); return; }
        new Handler().postDelayed(() -> {
            if (!isFinishing() && !report(true)) close();
        }, 600);
    }

    @Override protected void onResume() {
        super.onResume();
        if (returned && !shown) new Handler().postDelayed(() -> {
            if (!isFinishing() && !shown && !report(true)) close();
        }, 650);
    }

    private ApplicationExitInfo exitInfo() {
        ActivityManager manager = (ActivityManager)getSystemService(ACTIVITY_SERVICE);
        for (ApplicationExitInfo info : manager.getHistoricalProcessExitReasons(getPackageName(), 0, 16))
            if (getPackageName().equals(info.getProcessName()) && info.getTimestamp() >= started) return info;
        return null;
    }

    private boolean report(boolean stopped) {
        if (shown) return true;
        try {
            String nativeReason = CrashReport.tail(new File(userdata(), "ut3-crash.txt"), 8192).trim();
            ApplicationExitInfo info = exitInfo();
            if (CrashReport.clean(nativeReason)) return false;
            if (info != null && (info.getReason() == ApplicationExitInfo.REASON_USER_REQUESTED
                || info.getReason() == ApplicationExitInfo.REASON_USER_STOPPED
                || info.getReason() == ApplicationExitInfo.REASON_PACKAGE_UPDATED
                || info.getReason() == ApplicationExitInfo.REASON_PACKAGE_STATE_CHANGE
                || (info.getReason() == ApplicationExitInfo.REASON_EXIT_SELF && info.getStatus() == 0))
                && nativeReason.isEmpty() && exitCode == 0) return false;
            if (!stopped && nativeReason.isEmpty() && info == null) return false;
            String reason = !nativeReason.isEmpty() ? nativeReason : exitCode != 0 ? "Game exited with code " + exitCode :
                info != null ? CrashReport.exitReason(info.getReason(), info.getStatus(), info.getDescription(),
                    say("en", "ru").equals("ru")) : say("Game process stopped unexpectedly", "Процесс игры неожиданно завершился");
            String summary = reason + (info != null ? "\nAndroid exit reason: " + info.getReason() + ", status: " + info.getStatus() : "");
            StringBuilder text = new StringBuilder("Unreal Tournament 3 crash report\nTime: ")
                .append(java.time.Instant.now()).append("\nVersion: ")
                .append(getPackageManager().getPackageInfo(getPackageName(), 0).versionName)
                .append("\nDevice: ").append(Build.MANUFACTURER).append(' ').append(Build.MODEL)
                .append("\nAndroid: ").append(Build.VERSION.RELEASE).append(" (API ").append(Build.VERSION.SDK_INT)
                .append(")\nRenderer: ").append(getIntent().getStringExtra("renderer"))
                .append("\n\n").append(summary);
            if (info != null) text.append("\nLast PSS (KiB): ").append(info.getPss())
                .append("\nLast RSS (KiB): ").append(info.getRss());
            for (String name : new String[]{"ut3.log", "Documents/My Games/Unreal Tournament 3/UTGame/Logs/Launch.log"}) {
                text.append("\n\n--- ").append(name).append(" (tail) ---\n");
                try { text.append(CrashReport.tail(new File(userdata(), name), 65536)); }
                catch (IOException error) { text.append(error.toString()); }
            }
            File saved = new File(userdata(), "crashes/ut3-" + System.currentTimeMillis() + ".txt");
            if (info != null) try (InputStream trace = info.getTraceInputStream()) {
                if (trace != null) {
                    if (info.getReason() == ApplicationExitInfo.REASON_CRASH_NATIVE && Build.VERSION.SDK_INT >= 31) {
                        File tombstone = new File(saved.getPath() + ".tombstone.pb");
                        if (!tombstone.getParentFile().isDirectory() && !tombstone.getParentFile().mkdirs())
                            throw new IOException("Cannot create tombstone directory");
                        java.nio.file.Files.copy(trace, tombstone.toPath());
                        text.append("\n\nAndroid native tombstone (protobuf): ").append(tombstone.getAbsolutePath());
                    } else {
                    ByteArrayOutputStream bytes = new ByteArrayOutputStream();
                    byte[] buffer = new byte[4096];
                    while (bytes.size() < 131072) {
                        int count = trace.read(buffer, 0, Math.min(buffer.length, 131072 - bytes.size()));
                        if (count < 0) break;
                        bytes.write(buffer, 0, count);
                    }
                    text.append("\n\n--- Android trace ---\n").append(new String(bytes.toByteArray(), StandardCharsets.UTF_8));
                    }
                }
            } catch (IOException error) { text.append("\nAndroid trace: ").append(error); }
            String report = text.toString();
            CrashReport.write(saved, report);
            if (session().exists() && !session().delete()) android.util.Log.w("UT3", "Cannot retire crash session");
            shown = true;
            AlertDialog dialog = new AlertDialog.Builder(this)
                .setTitle(say("The game crashed", "Игра завершилась с ошибкой"))
                .setMessage(summary + "\n\n" + say("Report saved: ", "Отчёт сохранён: ") + saved.getAbsolutePath())
                .setPositiveButton(say("Copy to clipboard", "Скопировать в буфер обмена"), null)
                .setNegativeButton(say("Exit", "Выйти"), (d, which) -> finishAndRemoveTask())
                .setCancelable(false).create();
            dialog.setOnShowListener(d -> dialog.getButton(AlertDialog.BUTTON_POSITIVE).setOnClickListener(v -> {
                ((ClipboardManager)getSystemService(CLIPBOARD_SERVICE)).setPrimaryClip(ClipData.newPlainText("UT3 crash report", report));
                Toast.makeText(this, say("Report copied", "Отчёт скопирован"), Toast.LENGTH_SHORT).show();
            }));
            dialog.show();
            return true;
        } catch (Exception error) {
            android.util.Log.e("UT3", "crash report", error);
            if (stopped) {
                shown = true;
                AlertDialog dialog = new AlertDialog.Builder(this).setTitle(say("The game crashed", "Игра завершилась с ошибкой"))
                    .setMessage(error.toString())
                    .setPositiveButton(say("Copy to clipboard", "Скопировать в буфер обмена"), null)
                    .setNegativeButton(say("Exit", "Выйти"), (d, w) -> finishAndRemoveTask())
                    .setCancelable(false).create();
                dialog.setOnShowListener(d -> dialog.getButton(AlertDialog.BUTTON_POSITIVE).setOnClickListener(v ->
                    ((ClipboardManager)getSystemService(CLIPBOARD_SERVICE)).setPrimaryClip(ClipData.newPlainText("UT3 crash", error.toString()))));
                dialog.show();
            }
            return stopped;
        }
    }

    private void close() {
        if (session().exists() && !session().delete()) android.util.Log.w("UT3", "Cannot retire crash session");
        Launch.openLauncher(this, getIntent().getStringExtra(Launch.EXTRA_DISTRIBUTION));
        finish();
    }
}
