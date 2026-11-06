package org.ut3.port;

import java.io.File;
import java.io.IOException;
import java.io.RandomAccessFile;
import java.nio.charset.StandardCharsets;
import java.nio.file.AtomicMoveNotSupportedException;
import java.nio.file.Files;
import java.nio.file.StandardCopyOption;

final class CrashReport {
    static String tail(File file, int limit) throws IOException {
        if (!file.isFile()) return "";
        try (RandomAccessFile input = new RandomAccessFile(file, "r")) {
            long size = input.length();
            if (size == 0) return "";
            int first = input.read(), second = input.read();
            boolean utf16 = first == 255 && second == 254;
            long start = Math.max(utf16 ? 2 : 0, size - limit);
            if (utf16 && (start & 1) != 0) ++start;
            input.seek(start);
            byte[] data = new byte[(int)Math.min(limit, size - start)];
            input.readFully(data);
            return new String(data, utf16 ? StandardCharsets.UTF_16LE : StandardCharsets.UTF_8);
        }
    }

    static void write(File file, String text) throws IOException {
        File parent = file.getParentFile();
        if (!parent.isDirectory() && !parent.mkdirs()) throw new IOException("Cannot create crash log directory");
        File temporary = new File(parent, file.getName() + ".tmp");
        try (java.io.FileOutputStream out = new java.io.FileOutputStream(temporary)) {
            out.write(text.getBytes(StandardCharsets.UTF_8));
            out.getFD().sync();
        }
        try {
            Files.move(temporary.toPath(), file.toPath(), StandardCopyOption.ATOMIC_MOVE, StandardCopyOption.REPLACE_EXISTING);
        } catch (AtomicMoveNotSupportedException unsupported) {
            Files.move(temporary.toPath(), file.toPath(), StandardCopyOption.REPLACE_EXISTING);
        }
    }

    static boolean clean(String reason) { return reason.trim().equals("Clean exit"); }

    static String exitReason(int reason, int status, String description, boolean russian) {
        if (description != null && !description.trim().isEmpty()) return description;
        switch (reason) {
        case 2:
            return (russian ? "Процесс игры завершён сигналом " : "Game process terminated by signal ")
                + (status == 9 ? "SIGKILL" : Integer.toString(status));
        case 3:
            return russian ? "Android завершил игру из-за нехватки памяти" : "Android stopped the game because memory was low";
        case 4:
            return russian ? "Необработанная ошибка Java" : "Unhandled Java exception";
        case 5:
            return russian ? "Сбой в нативном коде игры" : "Crash in native game code";
        case 6:
            return russian ? "Игра перестала отвечать (ANR)" : "The game stopped responding (ANR)";
        default:
            return russian ? "Процесс игры неожиданно завершился" : "Game process stopped unexpectedly";
        }
    }
}
