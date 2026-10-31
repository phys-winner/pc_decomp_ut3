package org.ut3.port;

import android.content.Context;
import java.io.File;
import java.io.IOException;
import java.util.ArrayList;
import java.util.Collections;
import java.util.List;
import org.ut3.port.launcher.PortHooks;

/** The launcher's link to UT3's own settings (port.json "app.hooks"): the renderer is an option of
 *  the ANDROID tab in the game's settings, stored in UTAndroidPort.ini, so the launcher reads and
 *  writes it there and both always agree; the game language chosen there decides which
 *  localization GameSettings.validateLanguage requires. Runs in the launcher's process: no
 *  GameActivity, no native library. */
public final class LauncherHooks implements PortHooks {
    @Override public String renderer(Context context) {
        try {
            return new PortPreferences(context.getExternalFilesDir(null)).getString("renderer", null);
        } catch (IOException error) {
            return null;
        }
    }

    @Override public void setRenderer(Context context, String mode) {
        try {
            PortPreferences preferences = new PortPreferences(context.getExternalFilesDir(null));
            preferences.put("renderer", mode);
            preferences.save();
        } catch (IOException error) {
            android.util.Log.w("UT3", "launcher: renderer setting: " + error);
        }
    }

    /** The chosen language's localization folder and startup package, as GameActivity checks them. */
    @Override public List<String> missing(Context context, String distribution, String part, File folder) {
        if (!"game".equals(part) || folder == null || !folder.isDirectory()) return Collections.emptyList();
        String language = "INT";
        try {
            language = new PortPreferences(context.getExternalFilesDir(null)).getString("language", "INT");
        } catch (IOException error) {
            android.util.Log.w("UT3", "launcher: language setting: " + error);
        }
        List<String> missing = new ArrayList<>();
        File game = child(folder, "UTGame");
        File localization = child(child(game, "Localization"), language);
        if (localization == null || !localization.isDirectory()) missing.add("UTGame/Localization/" + language + "/");
        File startup = child(child(game, "CookedPC"), "Startup_" + language + ".upk");
        if (startup == null || !startup.isFile()) missing.add("UTGame/CookedPC/Startup_" + language + ".upk");
        return missing;
    }

    /** A child by name ignoring case, as Windows games expect; null if absent. */
    private static File child(File parent, String name) {
        if (parent == null) return null;
        File direct = new File(parent, name);
        if (direct.exists()) return direct;
        File[] children = parent.listFiles();
        if (children != null) for (File item : children) if (item.getName().equalsIgnoreCase(name)) return item;
        return null;
    }
}
