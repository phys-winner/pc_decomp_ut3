package port.launcher;

import java.io.File;
import java.io.FileInputStream;
import java.io.FileOutputStream;
import java.io.IOException;
import java.io.RandomAccessFile;
import java.lang.reflect.Field;
import java.nio.file.Files;
import java.util.ArrayList;
import java.util.Arrays;
import java.util.Collections;
import java.util.List;
import java.util.zip.CRC32;

/** Plain-Java checks of the launcher base, run on the PC by tests/run_tests.py with fixtures it
 *  writes: archives, import plans and jobs, the stored-archive index, discovery, cleanup, texts. */
public final class LauncherSelfcheck {
    private static int failures, checks;

    private static void check(boolean condition, String what) {
        ++checks;
        if (!condition) {
            ++failures;
            System.out.println("FAIL " + what);
        }
    }

    public static void main(String[] args) throws Exception {
        File fixtures = new File(args[0]);
        java.util.Locale.setDefault(java.util.Locale.ENGLISH);
        Texts.reset();
        globs();
        paths();
        texts();
        sizes();
        versions();
        launches(new File(fixtures, "state"));
        probe();
        List<Spec.Distribution> dists = Collections.singletonList(ut3());
        archive(new File(fixtures, "game.zip"), dists, new File(fixtures, "work"));
        zip64(new File(fixtures, "zip64.zip"));
        unsafe(new File(fixtures, "unsafe.zip"), dists);
        stored(new File(fixtures, "stored.zip"), new File(fixtures, "stored.zmi"));
        editions(new File(fixtures, "editions.zip"), new File(fixtures, "editions"));
        adopt(new File(fixtures, "adopt"));
        discovery(dists, new File(fixtures, "tree"));
        cleanup(new File(fixtures, "clean"));
        turnip(new File(fixtures, "turnip"));
        System.out.println(checks + " checks, " + failures + " failed");
        System.exit(failures == 0 ? 0 : 1);
    }

    static Spec.Distribution ut3() {
        Spec.Distribution d = new Spec.Distribution();
        d.id = "ut3";
        d.activity = "org.example.GameActivity";
        Spec.Part game = new Spec.Part();
        game.id = "game";
        game.marker = "UTGame/CookedPC/";
        game.required.addAll(Arrays.asList("UTGame/CookedPC/", "UTGame/Config/", "Engine/Config/"));
        game.needed.add(new Glob("UTGame/"));
        game.needed.add(new Glob("Engine/"));
        Spec.Part images = new Spec.Part();
        images.id = "images";
        images.marker = "UT3.exe";
        images.required.addAll(Arrays.asList("UT3.exe", "PhysXCore.dll"));
        d.parts.add(game);
        d.parts.add(images);
        return d;
    }

    /** The player's own Turnip driver: packages with and without meta.json, a bare library, refusals. */
    static void turnip(File dir) throws IOException {
        File drivers = new File(dir, "drivers");
        drivers.mkdirs();
        byte[] library = elf(2, 3, 183), arm32 = elf(1, 3, 40), program = elf(2, 2, 183);
        File packaged = zip(new File(dir, "Turnip_T30.zip"), "meta.json",
            "{\"schemaVersion\": 1, \"name\": \"Turnip \\\"T30\\\" \\u00e9\", \"libraryName\": \"vulkan.purple.so\"}".getBytes("UTF-8"),
            "vulkan.purple.so", library);
        String[] driver = TurnipDriver.unpack(packaged, packaged.getName(), drivers);
        check("vulkan.purple.so".equals(driver[0]) && "Turnip \"T30\" \u00e9".equals(driver[1]), "a package's meta.json names library and title: "
            + Arrays.toString(driver));
        check(Arrays.equals(library, Files.readAllBytes(new File(drivers, driver[0]).toPath())), "the library is copied whole");
        File single = zip(new File(dir, "single.zip"), "drivers/libvulkan_freedreno.so", library, "readme.txt", new byte[] {'x'});
        check("libvulkan_freedreno.so".equals(TurnipDriver.unpack(single, "single.zip", drivers)[0]), "a ZIP without meta.json gives its only .so");
        File bare = new File(dir, "bare");
        Files.write(bare.toPath(), library);
        check("my_turnip.so".equals(TurnipDriver.unpack(bare, "my turnip", drivers)[0]), "a bare library keeps a safe name with .so");
        Files.write(bare.toPath(), arm32);
        check(refused(bare, drivers, "64-bit"), "a 32-bit library is refused");
        Files.write(bare.toPath(), program);
        check(refused(bare, drivers, "AArch64 shared"), "an executable is refused");
        Files.write(bare.toPath(), "these words are not a driver library at all".getBytes("UTF-8"));
        check(refused(bare, drivers, "64-bit"), "a file that is no ELF is refused");
        File missing = zip(new File(dir, "missing.zip"), "meta.json", "{\"libraryName\": \"vulkan.ad07xx.so\"}".getBytes("UTF-8"),
            "other.so", library);
        check(refused(missing, drivers, "lacks"), "a package without its library is refused");
        check("evil.so".equals(TurnipDriver.safe("../../evil.so")) && "libvulkan_turnip.so".equals(TurnipDriver.safe("..")),
            "names lose their folders and leading dots");
        File[] left = drivers.listFiles((d, name) -> name.endsWith(".part"));
        check(left != null && left.length == 0, "no partial copies stay behind");
    }

    private static boolean refused(File picked, File drivers, String reason) {
        try {
            TurnipDriver.unpack(picked, picked.getName(), drivers);
            return false;
        } catch (IOException e) {
            return String.valueOf(e.getMessage()).contains(reason);
        }
    }

    /** An ELF header: class (1 = 32, 2 = 64 bit), little-endian, type and machine; padded to 64 bytes. */
    private static byte[] elf(int elfClass, int type, int machine) {
        byte[] data = new byte[64];
        data[0] = 0x7F; data[1] = 'E'; data[2] = 'L'; data[3] = 'F';
        data[4] = (byte) elfClass; data[5] = 1; data[6] = 1;
        data[16] = (byte) type; data[18] = (byte) machine; data[19] = (byte) (machine >> 8);
        return data;
    }

    private static File zip(File file, Object... entries) throws IOException {
        try (java.util.zip.ZipOutputStream out = new java.util.zip.ZipOutputStream(new FileOutputStream(file))) {
            for (int i = 0; i < entries.length; i += 2) {
                out.putNextEntry(new java.util.zip.ZipEntry((String) entries[i]));
                out.write((byte[]) entries[i + 1]);
                out.closeEntry();
            }
        }
        return file;
    }

    static void globs() {
        check(new Glob("directx9c/").matches("DirectX9c/DXSETUP.exe"), "folder glob matches inside, any case");
        check(new Glob("directx9c/").matches("directx9c/a/b/c.cab"), "folder glob matches deep");
        check(!new Glob("directx9c/").matches("UTGame/directx9c/x"), "folder glob is anchored at the root");
        check(new Glob("*.pdf").matches("Manual.PDF"), "star matches at the top");
        check(!new Glob("*.pdf").matches("docs/Manual.pdf"), "star stays in one folder");
        check(new Glob("**/*.pdf").matches("docs/en/Manual.pdf") && new Glob("**/*.pdf").matches("Manual.pdf"), "double star spans folders");
        check(new Glob("Binaries/PS3/**").matches("binaries/ps3/x.elf"), "trailing double star");
        check(new Glob("a/**/b.txt").matches("a/b.txt") && new Glob("a/**/b.txt").matches("a/x/y/b.txt"), "inner double star");
        check(new Glob("unins???.exe").matches("unins000.exe") && !new Glob("unins???.exe").matches("unins0000.exe"), "question marks");
        check(new Glob("directx9c/").mayMatchBelow("DirectX9c") && new Glob("directx9c/").mayMatchBelow("directx9c/sub"), "may match below a folder");
        check(!new Glob("*.pdf").mayMatchBelow("docs") && new Glob("**/*.pdf").mayMatchBelow("docs/en"), "prunes unreachable folders");
    }

    static void paths() {
        check("a/b".equals(Install.relative("a\\./b")), "relative normalises separators and dots");
        check(Install.relative("../evil") == null && Install.relative("a/../../x") == null, "relative rejects parent folders");
        check(Install.relative("/etc/passwd") == null && Install.relative("C:/Windows/x") == null, "relative rejects absolute paths");
        check("".equals(Install.relative(".")) && "a".equals(Install.relative("a/./")), "relative of a dot is the root");
        check("UTGame/x.ini".equals(Install.strip("UT3 Copy/UTGame/x.ini", 1)), "strip removes leading folders");
    }

    static void texts() throws ReflectiveOperationException {
        Field f = Texts.class.getDeclaredField("TEXTS");
        f.setAccessible(true);
        String[][] table = (String[][]) f.get(null);
        check(table.length == Texts.LAST + 1, "one row per text id (" + table.length + " rows, last id " + Texts.LAST + ")");
        check(Texts.t(null, Texts.PICKER_DISABLED_BUILTIN).endsWith("The built-in browser is used instead."),
            "the disabled-picker note is a whole sentence");
        check(!Texts.t(null, Texts.FIXED_HINT).contains("%"), "the fixed-part hint does not repeat the folder");
        for (int i = 0; i < table.length; ++i) {
            check(table[i].length == Texts.LANGUAGES.length, "row " + i + " has every language");
            for (String s : table[i]) check(s != null && !s.isEmpty(), "row " + i + " has no empty text");
            int formats = table[i][0].split("%", -1).length;
            for (String s : table[i]) check(s.split("%", -1).length == formats, "row " + i + " keeps its format arguments: " + s);
        }
    }

    static void sizes() {
        check("0 KB".equals(Texts.size(null, 0)), "zero bytes: " + Texts.size(null, 0));
        check("0.5 KB".equals(Texts.size(null, 512)), "half a kilobyte: " + Texts.size(null, 512));
        check("12 KB".equals(Texts.size(null, 12_000)), "kilobytes below 0.1 MB: " + Texts.size(null, 12_000));
        check("0.1 MB".equals(Texts.size(null, 100_000)), "0.1 MB: " + Texts.size(null, 100_000));
        check("37 MB".equals(Texts.size(null, 37_000_000)), "megabytes: " + Texts.size(null, 37_000_000));
        check("4.2 GB".equals(Texts.size(null, 4_200_000_000L)), "gigabytes: " + Texts.size(null, 4_200_000_000L));
    }

    /** Launch records: a refused start names its reason and is not blamed on the renderer. */
    static void launches(File dir) {
        dir.mkdirs();
        Launch.recordStart(dir, "ut3", "10:1", "gles");
        java.util.Properties record = State.read(dir, State.launch("ut3"));
        check("gles".equals(Renderers.failed(record, "10:1")), "a start without its first frame names its renderer");
        check(Renderers.failed(record, "11:2") == null, "another build's record says nothing");
        check(Launch.refusal(record, "10:1") == null, "a start the game did not refuse has no reason");
        Launch.recordFailure(dir, "ut3", "Game language files are missing: INT\n");
        record = State.read(dir, State.launch("ut3"));
        check("Game language files are missing: INT".equals(Launch.refusal(record, "10:1")), "Launch.failed records the reason: " + record);
        check(Renderers.failed(record, "10:1") == null, "a refused start does not blame the renderer");
        check("1".equals(record.getProperty("started")) && "gles".equals(record.getProperty("renderer")), "the start record stays");
        Launch.recordStart(dir, "ut3", "10:1", "dxvk");
        record = State.read(dir, State.launch("ut3"));
        check(Launch.refusal(record, "10:1") == null && "dxvk".equals(Renderers.failed(record, "10:1")), "a new start clears the reason");
        State.update(dir, State.launch("ut3"), values -> values.setProperty("reached", "1"));
        check(Renderers.failed(State.read(dir, State.launch("ut3")), "10:1") == null, "a start that reached its first frame did not fail");
    }

    static Spec.Mode mode(String id, String kind) {
        Spec.Mode m = new Spec.Mode();
        m.id = id;
        m.kind = kind;
        m.title = id;
        return m;
    }

    /** The driver check's report decides the DXVK modes; unchecked ones are never picked by themselves. */
    static void probe() {
        Spec.Mode dxvk = mode("dxvk", "vulkan"), turnip = mode("turnip", "turnip"), gles = mode("gles", "gles");
        java.util.Map<String, String> mali = Probe.parse("stock.status=missing\nstock.device=Mali-G715\nstock.vendor=13b5\n"
            + "stock.api=1.4.305\nstock.missing=bcTextures\nturnip.status=skipped\n");
        Renderers.Status s = Renderers.judge(null, dxvk, mali);
        check(!s.available && s.reason.contains("BC (DXT)"), "no BC1-3: DXVK is unavailable and says why: " + s.reason);
        dxvk.needsBC = false;
        s = Renderers.judge(null, dxvk, mali);
        check(s.available && s.verified, "a mode that does not need BC passes the same driver");
        dxvk.needsBC = true;
        s = Renderers.judge(null, turnip, mali);
        check(!s.available && s.reason.contains("Mali-G715"), "Turnip is not offered on a Mali GPU: " + s.reason);
        java.util.Map<String, String> adreno = Probe.parse("stock.status=missing\nstock.vendor=5143\nstock.api=1.1.128\n"
            + "stock.missing=vulkan13,robustness2\nturnip.status=ok\nturnip.vendor=5143\nturnip.api=1.3.289\n");
        s = Renderers.judge(null, dxvk, adreno);
        check(!s.available && s.reason.contains("1.1.128") && s.reason.contains("robustness2"), "old system driver: " + s.reason);
        s = Renderers.judge(null, turnip, adreno);
        check(s.available && s.verified, "a passing Turnip check makes Turnip verified");
        check(!Renderers.judge(null, dxvk, Probe.parse("stock.status=crash\n")).available, "a crashed check makes the mode unavailable");
        java.util.Map<String, String> fine = Probe.parse("stock.status=ok\nstock.vendor=13b5\n");
        check(Renderers.judge(null, dxvk, fine).verified, "a passing check makes DXVK verified");

        java.util.List<Renderers.Status> pending = java.util.Arrays.asList(new Renderers.Status(gles, true, true, null),
            new Renderers.Status(dxvk, true, false, "not checked"));
        check("gles".equals(Renderers.effective("gles", pending, "dxvk", false)), "an unchecked mode is never chosen automatically");
        check("dxvk".equals(Renderers.effective("gles", pending, "dxvk", true)), "the player may still choose an unchecked mode");
        check("gles".equals(Renderers.effective("dxvk", pending, "dxvk", false)), "not even as the port's default");
        java.util.List<Renderers.Status> refused = java.util.Arrays.asList(new Renderers.Status(gles, true, true, null),
            new Renderers.Status(dxvk, false, false, "no BC"));
        check("gles".equals(Renderers.effective("gles", refused, "dxvk", true)), "an unavailable choice falls back to the default");
    }

    static void versions() {
        check(Renderers.version(30) == 11 && Renderers.version(32) == 12 && Renderers.version(33) == 13 && Renderers.version(37) == 17,
            "Android versions of API levels");
        check(Spec.vulkanVersion("1.3") == ((1 << 22) | (3 << 12)), "Vulkan 1.3 as PackageManager writes it");
    }

    static void archive(File file, List<Spec.Distribution> dists, File work) throws Exception {
        Zip zip = new Zip(new FileInputStream(file).getChannel());
        List<Install.ArchivePart> plan = Install.plan(zip, dists);
        check(plan.size() == 2, "the archive holds both parts");
        Install.ArchivePart game = plan.get(0), images = plan.get(1);
        check("ut3 copy/".equals(game.root), "the game's root is the folder above UTGame: " + game.root);
        check(game.paths.contains("UTGame/CookedPC/Maps/DM-Deck.ut3") && game.paths.contains("Engine/Config/BaseEngine.ini"),
            "the game's files keep their case under the root: " + game.paths);
        check(!game.paths.contains("directx9c/dxsetup.exe") && game.paths.size() == 3, "only the folders the part needs");
        check("images/".equals(images.root) && images.paths.size() == 2, "the images part: " + images.root + " " + images.paths);
        check(game.compressed > 0 && images.compressed == 0, "compressed bytes are counted");

        File gameTarget = new File(work, "game"), imageTarget = new File(work, "images");
        gameTarget.mkdirs();
        Files.write(new File(gameTarget, "old.txt").toPath(), "previous".getBytes("UTF-8"));
        Install.UnpackJob job = new Install.UnpackJob(null, zip, plan, Arrays.asList(gameTarget, imageTarget));
        job.run();
        check(job.failure == null, "unpacking works: " + job.failure);
        check(new File(gameTarget, "UTGame/CookedPC/Maps/DM-Deck.ut3").length() == 100000, "unpacked file size");
        check(!new File(gameTarget, "old.txt").exists() && !new File(work, "game.old").exists() && !new File(work, "game.part").exists(),
            "the previous folder is replaced and nothing is left aside");
        check(new File(imageTarget, "UT3.exe").isFile(), "the second part is unpacked too");
        check(job.done == job.total && job.total > 0, "progress reaches the total");

        Zip again = new Zip(new FileInputStream(file).getChannel());
        List<Install.ArchivePart> plan2 = Install.plan(again, dists);
        Install.UnpackJob stopped = new Install.UnpackJob(null, again, plan2.subList(0, 1), Collections.singletonList(gameTarget));
        stopped.cancelled = true;
        stopped.run();
        check(stopped.failure != null && stopped.failure.code == Failure.CANCELLED, "a stopped job fails as cancelled");
        check(new File(gameTarget, "UTGame/CookedPC/Maps/DM-Deck.ut3").isFile() && !new File(work, "game.part").exists(),
            "a stopped job leaves the previous files and no staging folder");

        File corrupt = new File(work, "corrupt.zip");
        Files.copy(file.toPath(), corrupt.toPath());
        try (RandomAccessFile raf = new RandomAccessFile(corrupt, "rw")) {
            Zip probe = new Zip(new FileInputStream(corrupt).getChannel());
            for (Zip.Entry e : probe.entries) if (e.name.endsWith("DM-Deck.ut3")) { raf.seek(probe.dataOffset(e) + 500); raf.write(0x5A ^ raf.read()); }
            probe.close();
        }
        Zip bad = new Zip(new FileInputStream(corrupt).getChannel());
        List<Install.ArchivePart> plan3 = Install.plan(bad, dists);
        File fresh = new File(work, "fresh");
        Install.UnpackJob broken = new Install.UnpackJob(null, bad, plan3.subList(0, 1), Collections.singletonList(fresh));
        broken.run();
        check(broken.failure != null && broken.failure.code == Failure.CORRUPT && !fresh.exists() && !new File(work, "fresh.part").exists(),
            "a CRC mismatch fails and leaves nothing: " + broken.failure);

        File recovered = new File(work, "recover");
        new File(recovered.getPath() + Install.PART, "x").mkdirs();
        new File(recovered.getPath() + Install.OLD, "kept").mkdirs();
        check(Install.recover(recovered) && recovered.isDirectory() && new File(recovered, "kept").isDirectory()
            && !new File(recovered.getPath() + Install.PART).exists(), "an interrupted swap is rolled back");
    }

    static void zip64(File file) throws IOException {
        Zip zip = new Zip(new FileInputStream(file).getChannel());
        check(zip.entries.size() == 2 && zip.entries.get(0).size == 70000, "ZIP64 records are read");
        CRC32 crc = new CRC32();
        byte[] buffer = new byte[8192];
        try (java.io.InputStream in = zip.open(zip.entries.get(0))) { for (int n; (n = in.read(buffer)) > 0;) crc.update(buffer, 0, n); }
        check(crc.getValue() == zip.entries.get(0).crc, "a ZIP64 entry reads whole");
        zip.close();
    }

    static void unsafe(File file, List<Spec.Distribution> dists) throws IOException {
        Zip zip = new Zip(new FileInputStream(file).getChannel());
        try {
            Install.plan(zip, dists);
            check(false, "a path out of the archive is refused");
        } catch (Failure f) {
            check(f.code == Failure.UNSAFE, "a path out of the archive is refused as unsafe");
        } finally {
            zip.close();
        }
    }

    static void stored(File file, File index) throws IOException {
        Zip zip = new Zip(new FileInputStream(file).getChannel());
        StoredArchive.Plan plan = StoredArchive.plan(zip, "", new ArrayList<>());
        check(plan.items.size() == 4 && plan.sidecarFiles == 1, "stored and compressed items: " + plan.items.size() + "/" + plan.sidecarFiles);
        try (RandomAccessFile raw = new RandomAccessFile(file, "r")) {
            for (StoredArchive.Item item : plan.items) {
                if (!item.stored()) continue;
                byte[] data = new byte[(int) item.entry.size];
                raw.seek(item.offset);
                raw.readFully(data);
                CRC32 crc = new CRC32();
                crc.update(data);
                check(crc.getValue() == item.entry.crc, "a stored item's bytes are at its offset: " + item.path);
            }
        }
        StoredArchive.writeIndex(plan, zip.length(), StoredArchive.fingerprint(zip), index);
        check(index.isFile() && index.length() > 48, "the index is written");
        byte[] head = new byte[4];
        try (FileInputStream in = new FileInputStream(index)) { check(in.read(head) == 4 && "ZMI1".equals(new String(head, "US-ASCII")), "index magic"); }
        zip.close();
    }

    static Spec.Distribution edition(String id, String marker, List<String> required, List<String> needed) {
        Spec.Distribution d = new Spec.Distribution();
        d.id = id;
        d.activity = "org.example." + id;
        Spec.Part game = new Spec.Part();
        game.id = "game";
        game.marker = marker;
        game.required.addAll(required);
        for (String n : needed) game.needed.add(new Glob(n));
        d.parts.add(game);
        return d;
    }

    /** An archive of several distributions: parts in one folder are unpacked once for all of them;
     *  targets of one import never coincide. */
    static void editions(File file, File work) throws Exception {
        Spec.Distribution tribes = edition("tribes", "bin/game.exe", Arrays.asList("bin/game.exe", "data/base.pak", "data/tribes.pak"),
            Arrays.asList("bin/", "data/"));
        Spec.Distribution addon = edition("addon", "bin2/game.exe", Arrays.asList("bin2/game.exe", "data/base.pak", "data2/addon.pak"),
            Arrays.asList("bin2/", "data/", "data2/"));
        Spec.Distribution orig = edition("orig", "bin/game.exe", Arrays.asList("bin/game.exe", "data/base.pak", "data/patch.pak"),
            Arrays.asList("bin/", "data/"));
        Zip zip = new Zip(new FileInputStream(file).getChannel());
        List<Install.ArchivePart> plan = Install.plan(zip, Arrays.asList(tribes, addon, orig));
        check(plan.size() == 2, "the add-on and the original in one folder are one part: " + plan.size());
        Install.ArchivePart standalone = plan.get(0), gold = plan.get(1);
        check(standalone.distribution == tribes && "games/tribes/".equals(standalone.root) && standalone.also.isEmpty(),
            "the standalone distribution keeps its own part: " + standalone.root);
        check(gold.distribution == addon && gold.also.size() == 1 && gold.also.get(0).distribution == orig
            && gold.served().size() == 2, "the original is unpacked with the add-on");
        check(gold.paths.contains("bin/game.exe") && gold.paths.contains("bin2/game.exe") && gold.paths.contains("data/patch.pak")
            && !gold.paths.contains("Manual.pdf"), "the merged part carries what each needs: " + gold.paths);
        check(new java.util.HashSet<>(gold.paths).size() == gold.paths.size(), "no file twice: " + gold.paths);
        long size = 0;
        for (Zip.Entry e : gold.entries) size += e.size;
        check(size == gold.size, "the merged size is its files' size");
        for (int i = 1; i < gold.entries.size(); ++i)
            check(gold.entries.get(i - 1).header < gold.entries.get(i).header, "the merged part is read in archive order");

        File shared = new File(work, "game");
        List<File> targets = Install.distinct(plan, Arrays.asList(shared, shared));
        check(targets.get(0).equals(shared) && targets.get(1).equals(new File(work, "game-2")),
            "two parts of one import never share a folder: " + targets);
        new File(work, "game-2").mkdirs();
        targets = Install.distinct(plan, Arrays.asList(shared, shared));
        check(targets.get(1).equals(new File(work, "game-3")), "a free name is not an existing folder: " + targets);
        Install.UnpackJob job = new Install.UnpackJob(null, zip, plan, Arrays.asList(new File(work, "tribes"), new File(work, "gold")));
        job.run();
        check(job.failure == null, "unpacking several distributions works: " + job.failure);
        check(Install.present(tribes.parts.get(0), new File(work, "tribes")), "the standalone distribution is complete");
        check(Install.present(addon.parts.get(0), new File(work, "gold")) && Install.present(orig.parts.get(0), new File(work, "gold")),
            "one folder holds the add-on and the original");
        check(!new File(work, "gold/Manual.pdf").exists(), "what neither needs stays in the archive");
    }

    /** An earlier launcher's settings: taken only where the player has chosen nothing yet. */
    static void adopt(File dir) {
        dir.mkdirs();
        State.update(dir, State.CHOICES, values -> values.setProperty("part.toe.game", "/sdcard/Chosen"));
        java.util.Map<String, String> old = new java.util.LinkedHashMap<>();
        old.put("part.toe.game", "/sdcard/Old");
        old.put("part.hof.game", "/sdcard/Gold");
        old.put("option.new_creature_framework", "1");
        old.put("intro.toe", "0");
        old.put("last", "hof");
        old.put("language", "ru");
        old.put("../escape", "x");
        List<String> taken = Launch.adopt(dir, old);
        java.util.Properties choices = State.read(dir, State.CHOICES);
        check("/sdcard/Chosen".equals(choices.getProperty("part.toe.game")), "a choice made here stays");
        check("/sdcard/Gold".equals(choices.getProperty("part.hof.game")) && "1".equals(choices.getProperty("option.new_creature_framework"))
            && "0".equals(choices.getProperty("intro.toe")) && "hof".equals(choices.getProperty("last")), "the old choices are taken: " + choices);
        check(!choices.containsKey("language") && !choices.containsKey("../escape") && taken.size() == 4,
            "only the base's own kinds of choices are taken: " + taken);
        check(Launch.adopt(dir, old).isEmpty(), "taking them again changes nothing");
    }

    static void discovery(List<Spec.Distribution> dists, File tree) throws IOException {
        File base = new File(tree, "Download/games/UT3 Steam");
        for (String folder : new String[] {"UTGame/CookedPC", "UTGame/Config", "Engine/Config"}) new File(base, folder).mkdirs();
        List<Install.Found> found = Install.discover(dists, new File(tree, "Download"));
        check(found.size() == 1 && found.get(0).part.id.equals("game") && found.get(0).folder.equals(base),
            "discovery finds the game folder below the chosen one");
        found = Install.discover(dists, new File(base, "UTGame/CookedPC"));
        check(found.size() == 1 && found.get(0).folder.equals(base), "discovery finds the game folder above the chosen one");
        check(Install.missing(dists.get(0).parts.get(1), base).size() == 2, "missing files of a part are named");
    }

    static void cleanup(File root) throws Exception {
        write(new File(root, "directx9c/DXSETUP.exe"), 1000);
        write(new File(root, "directx9c/cab/x.cab"), 500);
        write(new File(root, "Manual.pdf"), 300);
        write(new File(root, "UTGame/CookedPC/map.ut3"), 2000);
        write(new File(root, "UTGame/Manual.pdf"), 100);
        Spec.Category c = new Spec.Category();
        c.id = "installers";
        c.globs.add(new Glob("directx9c/"));
        c.globs.add(new Glob("*.pdf"));
        Cleanup.Plan plan = Cleanup.scan(root, Collections.singletonList(c));
        check(plan.files.size() == 3 && plan.bytes == 1800, "cleanup previews only the matching files: " + plan.files.size() + " " + plan.bytes);
        write(new File(root, "Manual.pdf"), 301);
        Cleanup.Job changed = new Cleanup.Job(null, plan);
        changed.run();
        check(changed.failure != null && changed.failure.code == Failure.CHANGED && new File(root, "directx9c/DXSETUP.exe").isFile(),
            "a file changed since the preview stops the deletion before anything goes");
        plan = Cleanup.scan(root, Collections.singletonList(c));
        Cleanup.Job job = new Cleanup.Job(null, plan);
        job.run();
        check(job.failure == null && !new File(root, "directx9c").exists() && !new File(root, "Manual.pdf").exists()
            && new File(root, "UTGame/Manual.pdf").isFile() && new File(root, "UTGame/CookedPC/map.ut3").isFile(),
            "cleanup deletes the previewed files and their emptied folders only");
    }

    private static void write(File file, int size) throws IOException {
        file.getParentFile().mkdirs();
        try (FileOutputStream out = new FileOutputStream(file)) { out.write(new byte[size]); }
    }
}
