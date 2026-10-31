package org.ut3.port;

import java.io.File;
import java.io.IOException;
import java.nio.charset.StandardCharsets;
import java.nio.charset.Charset;
import java.nio.ByteBuffer;
import java.nio.file.AtomicMoveNotSupportedException;
import java.nio.file.Files;
import java.nio.file.StandardCopyOption;
import java.util.ArrayList;
import java.util.LinkedHashMap;
import java.util.List;
import java.util.Locale;
import java.util.Map;
import java.util.Arrays;

final class GameSettings {
    static int renderWidth(int displayWidth, int displayHeight) {
        return renderWidth(displayWidth, displayHeight, 480);
    }

    static int renderWidth(int displayWidth, int displayHeight, int height) {
        if (displayWidth <= 0 || displayHeight <= 0 || displayWidth < displayHeight) throw new IllegalArgumentException("Invalid landscape display size");
        if (height < 8 || height > 8192) throw new IllegalArgumentException("Invalid render height");
        long width = ((Math.round((double)height * displayWidth / displayHeight) + 7) / 8) * 8;
        if (width < 8 || width > 8192) throw new IllegalArgumentException("Unsupported display aspect ratio");
        return (int)width;
    }

    // UTEngine is a resolved runtime INI: copying DefaultEngine skips its BaseEngine
    // inheritance, and LoadFile then treats +Paths/.Package as literal key names.
    static void prepare(File engine, File game, int width) throws IOException {
        prepare(engine, game, width, 480, 60, "INT");
    }

    static void prepare(File engine, File game, int width, int height, int fps, String language) throws IOException {
        prepare(engine, game, width, height, fps, language, "gles");
    }

    static void prepare(File engine, File game, int width, int height, int fps, String language, String renderer) throws IOException {
        prepare(engine, game, width, height, fps, language, renderer, 100);
    }

    /** scale: the render scale, the 3D scene's ScreenPercentage (25-100); the interface keeps the
     *  display's resolution, as the original renders its UI after the scene's upscale. */
    static void prepare(File engine, File game, int width, int height, int fps, String language, String renderer, int scale) throws IOException {
        boolean nativePacer = nativeFramePacer(renderer);
        validateVideo(width, height, fps);
        validateScale(scale);
        validateLanguage(game, language);
        retireConfigBackups(engine.getParentFile());
        String previous = engine.isFile() ? read(engine) : null;
        boolean repair = previous != null && legacySeed(previous);
        String current = previous;
        if (previous == null || repair) {
            String defaults = read(game, "UTGame/Config/DefaultEngine.ini");
            current = bootstrap(previous, read(game, "Engine/Config/BaseEngine.ini"), defaults);
        }
        String defaults = read(game, "UTGame/Config/DefaultCompat.ini");
        String result = minimum(current, defaults, width, height, fps, nativePacer, scale);
        LinkedHashMap<String, String> locale = new LinkedHashMap<>();
        locale.put("Language", language);
        result = section(result, "Engine.Engine", locale);
        LinkedHashMap<String, String> input = new LinkedHashMap<>();
        input.put("AllowJoystickInput", "1");
        result = section(result, "WinDrv.WindowsClient", input);
        File compatibility = new File(engine.getParentFile(), "UTCompat.ini");
        String compat = compatibility.isFile() ? read(compatibility) : null;
        if (compat == null) {
            if (!basedOn(defaults, "..\\Engine\\Config\\BaseCompat.ini"))
                throw new IllegalArgumentException("Unsupported DefaultCompat inheritance");
            compat = combine(read(game, "Engine/Config/BaseCompat.ini"), defaults);
        }
        // First-run hardware detection reapplies a bucket after SystemSettings.
        // Keep real hardware scores, but every chosen bucket uses our minimum.
        Map<String, String> video = minimumVideo(defaults, width, height, scale);
        for (int bucket = 1; bucket <= 5; bucket++)
            compat = section(compat, "AppCompatBucket" + bucket, video);
        write(compatibility, compat);
        if (repair) {
            File backup = new File(engine.getParentFile(), "UTEngine.before-bootstrap-repair.ini.bak");
            if (!backup.exists()) Files.copy(engine.toPath(), backup.toPath());
        }
        write(engine, result);
    }

    private static void retireConfigBackups(File directory) throws IOException {
        File[] files = directory.listFiles();
        if (files == null) return;
        for (File file : files) {
            String name = file.getName();
            if (!file.isFile() || !name.matches("UT(Engine|Compat|Game)\\.before-(widescreen-120|bootstrap-repair)\\.ini")) continue;
            File backup = new File(directory, name + ".bak");
            if (backup.exists()) {
                if (!Arrays.equals(Files.readAllBytes(file.toPath()), Files.readAllBytes(backup.toPath())))
                    throw new IOException("Conflicting legacy config backups: " + name);
                Files.delete(file.toPath());
            } else Files.move(file.toPath(), backup.toPath());
        }
    }

    private static boolean baseEngine(String text) {
        return basedOn(text, "..\\Engine\\Config\\BaseEngine.ini");
    }

    private static boolean basedOn(String text, String path) {
        for (String line : ini(text).getOrDefault("configuration", new ArrayList<>())) {
            int equals = line.indexOf('=');
            if (equals > 0 && line.substring(0, equals).trim().equalsIgnoreCase("BasedOn"))
                return line.substring(equals + 1).trim().replace('/', '\\').equalsIgnoreCase(path);
        }
        return false;
    }

    static boolean legacySeed(String text) {
        if (!baseEngine(text)) return false;
        // The old Android seed retained this directive; valid merged PC configs
        // can retain BasedOn too, but their arrays have ordinary Paths keys.
        for (String line : ini(text).getOrDefault("core.system", new ArrayList<>())) {
            int equals = line.indexOf('=');
            if (equals > 0 && line.substring(0, equals).trim().equalsIgnoreCase("+Paths")) return true;
        }
        return false;
    }

    private static LinkedHashMap<String, List<String>> ini(String text) {
        LinkedHashMap<String, List<String>> sections = new LinkedHashMap<>();
        List<String> lines = null;
        for (String line : text.split("\\r?\\n")) {
            String trimmed = line.trim();
            if (trimmed.startsWith("[") && trimmed.endsWith("]"))
                lines = sections.computeIfAbsent(trimmed.substring(1, trimmed.length() - 1).toLowerCase(Locale.ROOT), key -> new ArrayList<>());
            else if (lines != null) lines.add(line);
        }
        return sections;
    }

    private static String key(String line) {
        String trimmed = line.trim();
        int equals = trimmed.indexOf('=');
        return equals > 0 && !trimmed.startsWith(";") && !trimmed.startsWith("#") ? trimmed.substring(0, equals).trim() : "";
    }

    private static String value(String line) {
        String result = line.substring(line.indexOf('=') + 1).trim();
        return result.length() >= 2 && result.startsWith("\"") && result.endsWith("\"") ? result.substring(1, result.length() - 1) : result;
    }

    static String bootstrap(String current, String base, String defaults) {
        if (current != null && !legacySeed(current)) return current;
        if (!baseEngine(defaults)) throw new IllegalArgumentException("Unsupported DefaultEngine inheritance");
        // An old seed's edited derived layer takes precedence over stock defaults.
        return combine(base, current == null ? defaults : current);
    }

    private static String combine(String base, String layer) {
        LinkedHashMap<String, List<String>> result = ini(base.replace("%GAME%", "UT"));
        if (result.isEmpty()) throw new IllegalArgumentException("Base config is empty");
        // Keep .Map/.Package interleaving; grouping arrays loses it.
        for (Map.Entry<String, List<String>> section : ini(layer).entrySet()) {
            List<String> lines = result.computeIfAbsent(section.getKey(), name -> new ArrayList<>());
            for (String line : section.getValue()) {
                String name = key(line);
                if (name.isEmpty()) { lines.add(line); continue; }
                char operation = name.charAt(0);
                boolean directive = operation == '+' || operation == '.' || operation == '-' || operation == '!';
                String resolved = directive ? name.substring(1) : name;
                int first = -1;
                boolean exists = false;
                for (int index = 0; index < lines.size(); index++) {
                    String old = lines.get(index);
                    if (!key(old).equalsIgnoreCase(resolved)) continue;
                    if (first < 0) first = index;
                    if (value(old).equals(value(line))) exists = true;
                }
                if (operation == '+') {
                    if (!exists) lines.add(resolved + line.substring(line.indexOf('=')));
                } else if (operation == '.') lines.add(resolved + line.substring(line.indexOf('=')));
                else if (operation == '-' || operation == '!')
                    lines.removeIf(old -> key(old).equalsIgnoreCase(resolved) && (operation == '!' || value(old).equals(value(line))));
                else {
                    // shortcut: repeated plain derived keys keep the last value;
                    // revisit for non-PC cooker configs.
                    lines.removeIf(old -> key(old).equalsIgnoreCase(resolved));
                    lines.add(first < 0 ? lines.size() : first, line);
                }
            }
        }
        LinkedHashMap<String, String> names = new LinkedHashMap<>();
        for (String text : new String[]{base, layer})
            for (String line : text.split("\\r?\\n")) {
                String trimmed = line.trim();
                if (trimmed.startsWith("[") && trimmed.endsWith("]")) {
                    String name = trimmed.substring(1, trimmed.length() - 1);
                    names.putIfAbsent(name.toLowerCase(Locale.ROOT), name);
                }
            }
        StringBuilder out = new StringBuilder();
        for (Map.Entry<String, List<String>> section : result.entrySet()) {
            out.append('[').append(names.get(section.getKey())).append("]\r\n");
            for (String line : section.getValue()) out.append(line).append("\r\n");
        }
        return out.toString();
    }

    static String minimum(String current, String compatibility, int width) {
        return minimum(current, compatibility, width, 480, 60);
    }

    static String minimum(String current, String compatibility, int width, int height, int fps) {
        return minimum(current, compatibility, width, height, fps, false);
    }

    static boolean nativeFramePacer(String renderer) {
        if ("gles".equals(renderer)) return false;
        if ("dxvk".equals(renderer) || "dxvk-turnip".equals(renderer)) return true;
        throw new IllegalArgumentException("Unsupported renderer");
    }

    static String minimum(String current, String compatibility, int width, int height, int fps, boolean nativePacer) {
        return minimum(current, compatibility, width, height, fps, nativePacer, 100);
    }

    static String minimum(String current, String compatibility, int width, int height, int fps, boolean nativePacer, int scale) {
        validateVideo(width, height, fps);
        validateScale(scale);
        Map<String, String> video = minimumVideo(compatibility, width, height, scale);
        LinkedHashMap<String, String> timing = new LinkedHashMap<>();
        // DXVK delays its frame-latency signal at the selected native rate.
        // Keep UE3's independent adaptive limiter for the GLES path.
        timing.put("bSmoothFrameRate", nativePacer ? "FALSE" : "TRUE");
        timing.put("MinSmoothedFrameRate", "22");
        timing.put("MaxSmoothedFrameRate", Integer.toString(fps));
        return section(section(current, "SystemSettings", video), "Engine.GameEngine", timing);
    }

    private static void validateVideo(int width, int height, int fps) {
        if (height < 8 || width < height || width > 8192 || height > 8192 || fps < 1 || fps > 1000)
            throw new IllegalArgumentException("Unsupported graphics mode");
    }

    static final int MIN_SCALE = 25;

    private static void validateScale(int scale) {
        if (scale < MIN_SCALE || scale > 100) throw new IllegalArgumentException("Unsupported render scale");
    }

    static final String[] LANGUAGES = {"INT", "RUS", "DEU", "FRA", "ESN", "ITA", "POL", "CZE", "HUN"};
    static final String[] LANGUAGE_NAMES = {"English", "Русский", "Deutsch", "Français", "Español", "Italiano", "Polski", "Čeština", "Magyar"};

    static void validateLanguage(File game, String language) throws IOException {
        boolean supported = false;
        for (String candidate : LANGUAGES) if (candidate.equals(language)) supported = true;
        if (!supported) throw new IllegalArgumentException("Unsupported game language");
        boolean startup = false;
        for (String name : GameFiles.files(game, "UTGame/CookedPC"))
            if (name.equalsIgnoreCase("Startup_" + language + ".upk")) startup = true;
        if (!GameFiles.isDirectory(game, "UTGame/Localization/" + language) || !startup)
            throw new IOException("Game language files are missing: " + language);
    }

    static void prepareNativeUi(File engine, File game, File userdata, byte[] script, String options) throws IOException {
        if (script.length < 32 || (script[0] & 255) != 0xC1 || (script[1] & 255) != 0x83 ||
            (script[2] & 255) != 0x2A || (script[3] & 255) != 0x9E)
            throw new IOException("Invalid AndroidPort UnrealScript package");
        File published = new File(userdata, "Documents/My Games/Unreal Tournament 3/UTGame/Published/CookedPC/Script");
        if (!published.isDirectory() && !published.mkdirs()) throw new IOException("Cannot create mod directory");
        File target = new File(published, "AndroidPort.u");
        if (!target.isFile() || !Arrays.equals(Files.readAllBytes(target.toPath()), script)) {
            File temporary = new File(published, "AndroidPort.u.tmp");
            Files.write(temporary.toPath(), script);
            if (!Arrays.equals(Files.readAllBytes(temporary.toPath()), script)) throw new IOException("Mod package verification failed");
            try {
                Files.move(temporary.toPath(), target.toPath(), StandardCopyOption.ATOMIC_MOVE, StandardCopyOption.REPLACE_EXISTING);
            } catch (AtomicMoveNotSupportedException unsupported) {
                Files.move(temporary.toPath(), target.toPath(), StandardCopyOption.REPLACE_EXISTING);
            }
        }
        String path = "C:\\Users\\Player\\Documents\\My Games\\Unreal Tournament 3\\UTGame\\Published\\CookedPC\\Script";
        String layer = "[Core.System]\n+Paths=" + path + "\n+SeekFreePCPaths=" + path +
            "\n[Engine.Engine]\nGameViewportClientClassName=AndroidPort.AndroidPortViewportClient\n" +
            "[Engine.DataStoreClient]\n+GlobalDataStoreClasses=AndroidPort.AndroidPortDataStore\n" +
            "+GlobalDataStoreClasses=AndroidPort.AndroidPortValueStore\n";
        write(engine, combine(read(engine), layer));
        File runtimeGame = new File(engine.getParentFile(), "UTGame.ini");
        String current = runtimeGame.isFile() ? withoutPortOptions(read(runtimeGame)) :
            combine(read(game, "Engine/Config/BaseGame.ini"), read(game, "UTGame/Config/DefaultGame.ini"));
        if (!options.contains("OptionSet=AndroidPort") || !options.contains("<AndroidPort:"))
            throw new IOException("AndroidPort option definitions are missing");
        write(runtimeGame, combine(current, options.replaceAll("(?m)^[\\t ]*\\r?\\n", "") + FORCE_FEEDBACK));
    }

    /*
     * The PC configuration leaves PlayerController.ForceFeedbackManagerClassName
     * empty, so the original never rumbles there; the engine's own XInput
     * manager (WinDrv.XnaForceFeedbackManager) plays the game's waveforms on a
     * gamepad in use, which Android passes to the pad's motors.
     */
    static final String FORCE_FEEDBACK =
        "\n[UTGame.UTPlayerController]\nForceFeedbackManagerClassName=WinDrv.XnaForceFeedbackManager\n";

    /*
     * The option definitions each start writes replace the previous build's in
     * full: merging alone would keep the options a newer build no longer has.
     */
    static String withoutPortOptions(String text) {
        StringBuilder out = new StringBuilder();
        boolean port = false;
        for (String line : text.split("\\r?\\n")) {
            String trimmed = line.trim();
            if (trimmed.startsWith("[") && trimmed.endsWith("]")) {
                String name = trimmed.substring(1, trimmed.length() - 1);
                port = name.regionMatches(true, 0, "AndroidPort_", 0, 12) || name.equalsIgnoreCase("AndroidPort.AndroidPortDataStore");
            }
            if (!port) out.append(line).append('\n');
        }
        return out.toString();
    }

    private static Map<String, String> minimumVideo(String compatibility, int width, int height, int scale) {
        LinkedHashMap<String, String> video = new LinkedHashMap<>();
        boolean active = false;
        for (String line : compatibility.split("\\r?\\n")) {
            String value = line.trim();
            if (value.startsWith("[") && value.endsWith("]")) active = value.equalsIgnoreCase("[AppCompatBucket1]");
            else if (active && !value.startsWith(";") && !value.startsWith("#")) {
                int equals = value.indexOf('=');
                if (equals > 0) video.put(value.substring(0, equals).trim(), value.substring(equals + 1).trim());
            }
        }
        if (!video.containsKey("DetailMode") || !video.containsKey("ResX"))
            throw new IllegalArgumentException("Original minimum graphics profile is missing");
        video.put("ResX", Integer.toString(width));
        video.put("ResY", Integer.toString(height));
        video.put("UseVsync", "False");
        video.put("ScreenPercentage", Integer.toString(scale));
        return video;
    }

    static String section(String current, String name, Map<String, String> values) {
        StringBuilder out = new StringBuilder();
        boolean active = false, written = false;
        for (String line : current.split("\\r?\\n")) {
            String trimmed = line.trim();
            if (trimmed.startsWith("[") && trimmed.endsWith("]")) {
                if (active && !written) { entries(out, values); written = true; }
                active = trimmed.equalsIgnoreCase("[" + name + "]");
            }
            int equals = trimmed.indexOf('=');
            String key = equals > 0 ? trimmed.substring(0, equals).trim() : "";
            if (active && values.keySet().stream().anyMatch(k -> k.equalsIgnoreCase(key))) continue;
            out.append(line).append("\r\n");
        }
        if (!written) {
            if (!active) out.append('[').append(name).append("]\r\n");
            entries(out, values);
        }
        return out.toString();
    }

    private static void entries(StringBuilder out, Map<String, String> values) {
        for (Map.Entry<String, String> item : values.entrySet())
            out.append(item.getKey()).append('=').append(item.getValue()).append("\r\n");
    }

    static String read(File file) throws IOException {
        return decode(Files.readAllBytes(file.toPath()));
    }

    /** A file of the game part, from its folder or the archive it is played from. */
    static String read(File game, String path) throws IOException {
        return decode(GameFiles.read(game, path));
    }

    static String decode(byte[] bytes) throws IOException {
        Charset charset = StandardCharsets.UTF_8;
        int offset = 0;
        if (bytes.length >= 2 && bytes[0] == (byte)0xFF && bytes[1] == (byte)0xFE) { charset = StandardCharsets.UTF_16LE; offset = 2; }
        else if (bytes.length >= 2 && bytes[0] == (byte)0xFE && bytes[1] == (byte)0xFF) { charset = StandardCharsets.UTF_16BE; offset = 2; }
        else if (bytes.length >= 3 && bytes[0] == (byte)0xEF && bytes[1] == (byte)0xBB && bytes[2] == (byte)0xBF) offset = 3;
        return charset.newDecoder().decode(ByteBuffer.wrap(bytes, offset, bytes.length - offset)).toString();
    }

    static void write(File file, String text) throws IOException {
        File parent = file.getParentFile();
        if (!parent.isDirectory() && !parent.mkdirs()) throw new IOException("Cannot create game config directory");
        String stem = file.getName().replaceFirst("(?i)\\.ini$", "");
        // UE3 scans every .ini for per-object providers, including backup names.
        File backup = new File(parent, stem + ".before-widescreen-120.ini.bak");
        if (file.isFile() && !backup.exists()) Files.copy(file.toPath(), backup.toPath());
        byte[] body = text.getBytes(StandardCharsets.UTF_16LE), bytes = new byte[body.length + 2];
        bytes[0] = (byte)0xFF; bytes[1] = (byte)0xFE;
        System.arraycopy(body, 0, bytes, 2, body.length);
        File temporary = new File(parent, file.getName() + ".ut3.tmp");
        Files.write(temporary.toPath(), bytes);
        try {
            Files.move(temporary.toPath(), file.toPath(), StandardCopyOption.ATOMIC_MOVE, StandardCopyOption.REPLACE_EXISTING);
        } catch (AtomicMoveNotSupportedException unsupported) {
            Files.move(temporary.toPath(), file.toPath(), StandardCopyOption.REPLACE_EXISTING);
        }
    }
}
