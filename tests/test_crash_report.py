"""Exercise the real crash journal's bounded reads and atomic replacement with Java."""
import os
from pathlib import Path
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]


class CrashReportTest(unittest.TestCase):
    def test_real_files(self):
        source = r'''
package org.ut3.port;
import java.io.*;
import java.nio.file.*;
import java.nio.charset.*;
public class CrashReportCheck {
    public static void main(String[] args) throws Exception {
        File file = new File(args[0], "crashes/report.txt");
        assert CrashReport.tail(file, 64).isEmpty();
        CrashReport.write(file, "Native crash: SIGSEGV\nGuest function: 0x12345678\n");
        assert CrashReport.tail(file, 64).startsWith("Native crash: SIGSEGV");
        assert CrashReport.tail(file, 10).equals("x12345678\n");
        assert !new File(file.getPath()+".tmp").exists();
        CrashReport.write(file, "Clean exit\n");
        assert CrashReport.clean(CrashReport.tail(file, 64));
        assert !CrashReport.clean("Game exited with code 70\n");
        assert CrashReport.exitReason(2,9,null,false).contains("SIGKILL");
        assert CrashReport.exitReason(3,0,null,true).contains("нехватки памяти");
        assert CrashReport.exitReason(5,11,"native fault",false).equals("native fault");
        assert !CrashReport.exitReason(0,0,"",false).isEmpty();
        assert Files.size(file.toPath()) == 11 : "Atomic replacement retained the old tail";
        byte[] text="Журнал игры: ошибка\n".getBytes(StandardCharsets.UTF_16LE);
        byte[] bom=new byte[text.length+2];bom[0]=(byte)255;bom[1]=(byte)254;System.arraycopy(text,0,bom,2,text.length);
        Files.write(file.toPath(),bom);
        assert CrashReport.tail(file,64).equals("Журнал игры: ошибка\n");
        assert CrashReport.tail(file,15).equals("ошибка\n");
        CrashReport.write(file, "");
        assert CrashReport.tail(file,64).isEmpty();
        System.out.println("PASS real crash report files: bounded UTF-8/UTF-16 tails, atomic replace, clean/error distinction, empty/missing journals");
    }
}
'''
        java = Path(os.environ["JAVA_HOME"]) / "bin"
        with tempfile.TemporaryDirectory(prefix="ut3-crash-") as temporary:
            work = Path(temporary)
            fixture = work / "CrashReportCheck.java"
            fixture.write_text(source, encoding="utf-8")
            subprocess.run([java / "javac.exe", "--release", "8", "-encoding", "UTF-8", "-d", work,
                            ROOT / "android/app/src/org/ut3/port/CrashReport.java", fixture], check=True)
            subprocess.run([java / "java.exe", "-ea", "-cp", work, "org.ut3.port.CrashReportCheck", work], check=True)


if __name__ == "__main__":
    unittest.main()
