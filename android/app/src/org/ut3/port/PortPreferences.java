package org.ut3.port;

import java.io.File;
import java.io.FileInputStream;
import java.io.FileOutputStream;
import java.io.IOException;
import java.nio.file.AtomicMoveNotSupportedException;
import java.nio.file.Files;
import java.nio.file.StandardCopyOption;
import java.nio.charset.StandardCharsets;
import java.util.LinkedHashMap;
import java.util.Properties;
import java.util.Locale;

/** Settings live beside saves in the app's external files directory. */
final class PortPreferences {
    private final File file;
    private final File layoutFile;
    private final Properties values = new Properties();
    private static final String SECTION = "AndroidPort.AndroidPortSettings";
    private static final String LAYOUT_SECTION = "AndroidPort.TouchLayout";
    private long lastModified, lastLength;
    private long layoutModified, layoutLength;
    private boolean nativeDirty, layoutDirty;

    PortPreferences(File root) throws IOException {
        if (root == null) throw new IOException("App storage is unavailable");
        file = new File(root, "userdata/Documents/My Games/Unreal Tournament 3/UTGame/Config/UTAndroidPort.ini");
        layoutFile = new File(file.getParentFile(), "UTAndroidTouchLayout.ini");
        if (file.isFile() || layoutFile.isFile()) reloadIfChanged();
        else {
            File legacy = new File(root, "android-port.properties");
            if (legacy.isFile()) {
                Properties previous = new Properties();
                try (FileInputStream in = new FileInputStream(legacy)) { previous.load(in); }
                for (String name : previous.stringPropertyNames()) values.setProperty(key(name), previous.getProperty(name));
                nativeDirty = layoutDirty = true;
                save();
            }
        }
    }

    private static String key(String name) { return name.toLowerCase(Locale.ROOT); }
    synchronized String getString(String name, String fallback) { return values.getProperty(key(name), fallback); }
    synchronized int getInt(String name, int fallback) {
        try { return Integer.parseInt(values.getProperty(key(name))); }
        catch (NumberFormatException error) { return fallback; }
    }
    synchronized float getFloat(String name, float fallback) {
        try {
            float value = Float.parseFloat(values.getProperty(key(name)));
            return Float.isFinite(value) ? value : fallback;
        } catch (NumberFormatException | NullPointerException error) { return fallback; }
    }
    synchronized boolean getBoolean(String name, boolean fallback) {
        String value = values.getProperty(key(name));
        return "true".equalsIgnoreCase(value) || "1".equals(value) ||
            (!("false".equalsIgnoreCase(value) || "0".equals(value)) && fallback);
    }
    /** Android-only touch state: never part of the game's own config section. */
    private static boolean layoutKey(String name) {
        String field = key(name);
        return field.startsWith("touchx") || field.startsWith("touchy") || field.equals("lastlayoutrevision") ||
            field.equals("touchhints") || field.equals("touchinterfacedefault");
    }
    synchronized void put(String name, Object value) {
        String text = String.valueOf(value);
        name = key(name);
        if (text.equals(values.getProperty(name))) return;
        values.setProperty(name, text);
        if (layoutKey(name)) layoutDirty = true; else nativeDirty = true;
    }

    synchronized boolean reloadIfChanged() throws IOException {
        Properties before = new Properties(); before.putAll(values);
        if (!nativeDirty && file.isFile() && (file.lastModified() != lastModified || file.length() != lastLength)) {
            values.keySet().removeIf(key -> !layoutKey((String)key));
            values.putAll(parse(GameSettings.read(file), SECTION));
            lastModified = file.lastModified(); lastLength = file.length();
        }
        if (!layoutDirty && layoutFile.isFile() && (layoutFile.lastModified() != layoutModified || layoutFile.length() != layoutLength)) {
            values.keySet().removeIf(key -> layoutKey((String)key));
            values.putAll(parse(GameSettings.read(layoutFile), LAYOUT_SECTION));
            layoutModified = layoutFile.lastModified(); layoutLength = layoutFile.length();
        }
        boolean changed = !before.equals(values);
        return changed;
    }

    private static Properties parse(String text, String section) {
        Properties loaded = new Properties();
        boolean active = false;
        for (String line : text.split("\\r?\\n")) {
            String value = line.trim();
            if (value.startsWith("[") && value.endsWith("]")) active = value.equalsIgnoreCase("[" + section + "]");
            else if (active && !value.startsWith(";") && !value.startsWith("#")) {
                int equals = value.indexOf('=');
                if (equals <= 0) continue;
                String content = value.substring(equals + 1).trim();
                if (content.length() >= 2 && content.startsWith("\"") && content.endsWith("\"")) content = content.substring(1, content.length() - 1);
                loaded.setProperty(key(value.substring(0, equals).trim()), content);
            }
        }
        return loaded;
    }

    synchronized void save() throws IOException {
        if (nativeDirty) {
            saveFile(file, SECTION, false);
            lastModified = file.lastModified(); lastLength = file.length(); nativeDirty = false;
        }
        if (layoutDirty) {
            saveFile(layoutFile, LAYOUT_SECTION, true);
            layoutModified = layoutFile.lastModified(); layoutLength = layoutFile.length(); layoutDirty = false;
        }
    }

    private void saveFile(File file, String section, boolean layout) throws IOException {
        File parent = file.getParentFile();
        if (!parent.isDirectory() && !parent.mkdirs()) throw new IOException("Cannot create settings directory");
        File temporary = new File(parent, file.getName() + ".tmp");
        LinkedHashMap<String, String> entries = new LinkedHashMap<>();
        Properties expected = new Properties();
        for (String name : values.stringPropertyNames()) if (layoutKey(name) == layout) {
            entries.put(name, values.getProperty(name)); expected.setProperty(name, values.getProperty(name));
        }
        String text = GameSettings.section(file.isFile() ? GameSettings.read(file) : "", section, entries);
        byte[] body = text.getBytes(StandardCharsets.UTF_16LE);
        try (FileOutputStream out = new FileOutputStream(temporary)) {
            out.write(0xFF); out.write(0xFE); out.write(body);
            out.getFD().sync();
        }
        Properties check = parse(GameSettings.read(temporary), section);
        if (!check.equals(expected)) throw new IOException("Settings verification failed");
        try {
            Files.move(temporary.toPath(), file.toPath(), StandardCopyOption.ATOMIC_MOVE, StandardCopyOption.REPLACE_EXISTING);
        } catch (AtomicMoveNotSupportedException unsupported) {
            Files.move(temporary.toPath(), file.toPath(), StandardCopyOption.REPLACE_EXISTING);
        }
    }
}
