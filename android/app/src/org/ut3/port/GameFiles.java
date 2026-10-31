package org.ut3.port;

import java.io.File;
import java.io.FileNotFoundException;
import java.io.IOException;
import java.nio.ByteBuffer;
import java.nio.ByteOrder;
import java.nio.channels.FileChannel;
import java.nio.charset.StandardCharsets;
import java.nio.file.Files;
import java.util.ArrayList;
import java.util.List;
import java.util.Locale;
import java.util.Map;
import java.util.TreeMap;

/** The game part as Java reads it: its folder, then, when the launcher plays the part straight from a ZIP
 *  whose files are stored without compression, that archive. The folder is then the archive's sidecar (its
 *  few compressed files, unpacked); the archive's index is the launcher's "ZMI1" (StoredArchive.java), the
 *  same one the native file layer (zipmount.c) reads. Plain Java: the tests compile it on the PC. */
final class GameFiles {
    private static final class Entry {
        final String path;
        final long offset, size;
        final boolean sidecar;

        Entry(String path, long offset, long size, boolean sidecar) {
            this.path = path;
            this.offset = offset;
            this.size = size;
            this.sidecar = sidecar;
        }
    }

    private static final class Archive {
        final FileChannel channel;
        final TreeMap<String, Entry> entries = new TreeMap<>();

        Archive(FileChannel channel) { this.channel = channel; }
    }

    private static volatile Archive archive;

    private GameFiles() { }

    /** The archive the launcher set up (index file, the archive's open channel); its size must match the index. */
    static void mount(File index, FileChannel channel) throws IOException {
        ByteBuffer data = ByteBuffer.wrap(Files.readAllBytes(index.toPath())).order(ByteOrder.LITTLE_ENDIAN);
        if (data.remaining() < 48 || data.get() != 'Z' || data.get() != 'M' || data.get() != 'I' || data.get() != '1')
            throw new IOException("Not a ZMI1 index: " + index);
        long count = data.getInt() & 0xFFFFFFFFL, size = data.getLong();
        if (size != channel.size()) throw new IOException("The archive changed since its index was written");
        data.position(48);
        Archive mounted = new Archive(channel);
        for (long i = 0; i < count; ++i) {
            if (data.remaining() < 34) throw new IOException("Truncated ZMI1 index");
            long offset = data.getLong(), length = data.getLong();
            data.getInt();
            boolean sidecar = (data.getInt() & 1) != 0;
            data.getLong();
            int name = data.getShort() & 0xFFFF;
            if (data.remaining() < name || length < 0 || (!sidecar && (offset < 0 || offset > size - length)))
                throw new IOException("Damaged ZMI1 index");
            byte[] bytes = new byte[name];
            data.get(bytes);
            String path = new String(bytes, StandardCharsets.UTF_8);
            mounted.entries.put(key(path), new Entry(path, offset, length, sidecar));
        }
        archive = mounted;
    }

    static boolean mounted() { return archive != null; }

    private static String key(String path) {
        String key = path.replace('\\', '/').toLowerCase(Locale.ROOT);
        while (key.startsWith("/")) key = key.substring(1);
        while (key.endsWith("/")) key = key.substring(0, key.length() - 1);
        return key;
    }

    private static Entry stored(String path) {
        Archive a = archive;
        Entry e = a != null ? a.entries.get(key(path)) : null;
        return e != null && !e.sidecar ? e : null;
    }

    private static boolean archiveDirectory(String path) {
        Archive a = archive;
        if (a == null) return false;
        String prefix = key(path);
        if (!prefix.isEmpty()) prefix += "/";
        String next = a.entries.ceilingKey(prefix);
        return next != null && next.startsWith(prefix);
    }

    static boolean isFile(File game, String path) {
        return new File(game, path).isFile() || stored(path) != null;
    }

    static boolean isDirectory(File game, String path) {
        return new File(game, path).isDirectory() || archiveDirectory(path);
    }

    /** The names of the files directly in folder: the folder's, then the archive's it lacks. */
    static List<String> files(File game, String folder) {
        List<String> names = new ArrayList<>();
        File[] own = new File(game, folder).listFiles();
        if (own != null) for (File file : own) if (file.isFile()) names.add(file.getName());
        Archive a = archive;
        if (a == null) return names;
        String prefix = key(folder);
        if (!prefix.isEmpty()) prefix += "/";
        for (Map.Entry<String, Entry> item : a.entries.tailMap(prefix, true).entrySet()) {
            if (!item.getKey().startsWith(prefix)) break;
            String rest = item.getValue().path.replace('\\', '/');
            rest = rest.substring(rest.length() - (item.getKey().length() - prefix.length()));
            if (rest.indexOf('/') >= 0 || item.getValue().sidecar) continue;
            boolean known = false;
            for (String name : names) known |= name.equalsIgnoreCase(rest);
            if (!known) names.add(rest);
        }
        return names;
    }

    /** A file's bytes: the folder's copy, else the archive's stored one. */
    static byte[] read(File game, String path) throws IOException {
        File file = new File(game, path);
        if (file.isFile()) return Files.readAllBytes(file.toPath());
        Entry e = stored(path);
        Archive a = archive;
        if (e == null || a == null) throw new FileNotFoundException(file.getPath());
        if (e.size > Integer.MAX_VALUE - 8) throw new IOException("Too large: " + path);
        ByteBuffer bytes = ByteBuffer.allocate((int) e.size);
        while (bytes.hasRemaining()) {
            if (a.channel.read(bytes, e.offset + bytes.position()) < 0) throw new IOException("Archive ends inside " + path);
        }
        return bytes.array();
    }
}
