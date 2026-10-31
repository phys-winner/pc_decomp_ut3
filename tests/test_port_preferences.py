"""Exercise the production settings store, including invalid values and restart."""
import os
from pathlib import Path
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]


class PortPreferencesTest(unittest.TestCase):
    def test_roundtrip_validation_and_atomic_write(self):
        program = r'''
package org.ut3.port;
import java.io.*;
public class PreferencesCheck {
    public static void main(String[] args) throws Exception {
        File root=new File(args[0]);
        PortPreferences p=new PortPreferences(root);
        assert p.getInt("missing",60)==60;
        assert p.getFloat("missing",.22f)==.22f;
        p.put("fpsLimit",45); p.put("language","RUS");
        p.put("touchLeftHanded",true); p.put("touchSensitivity",.35f);
        p.put("invalidInt","junk"); p.put("invalidFloat","NaN");
        p.put("infinity","Infinity"); p.put("invalidBoolean","yes");
        p.save();
        PortPreferences loaded=new PortPreferences(root);
        assert loaded.getInt("fpsLimit",60)==45;
        assert loaded.getString("language","").equals("RUS");
        assert loaded.getBoolean("touchLeftHanded",false);
        assert loaded.getFloat("touchSensitivity",0)==.35f;
        assert loaded.getInt("invalidInt",60)==60;
        assert loaded.getFloat("invalidFloat",.22f)==.22f;
        assert loaded.getFloat("infinity",1)==1;
        assert !loaded.getBoolean("invalidBoolean",false);
        assert loaded.getBoolean("invalidBoolean",true);
        loaded.put("language","FRA"); loaded.save();
        assert new PortPreferences(root).getString("language","").equals("FRA");
        File ini=new File(root,"userdata/Documents/My Games/Unreal Tournament 3/UTGame/Config/UTAndroidPort.ini");
        assert GameSettings.read(ini).contains("[AndroidPort.AndroidPortSettings]");
        assert !new File(ini.getPath()+".tmp").exists();
        loaded.put("touchX0",.4f); loaded.put("lastLayoutRevision",3); loaded.save();
        byte[] nativeBefore=java.nio.file.Files.readAllBytes(ini.toPath());
        loaded.put("touchX0",.5f); loaded.save();
        assert java.util.Arrays.equals(nativeBefore,java.nio.file.Files.readAllBytes(ini.toPath())):"Layout must not overwrite Unreal's cached config";
        PortPreferences withLayout=new PortPreferences(root);
        assert withLayout.getFloat("touchX0",0)==.5f;
        assert withLayout.getInt("lastLayoutRevision",0)==3;
        String changed=GameSettings.read(ini).replace("language=FRA","Language=RUS").replace("touchlefthanded=true","TouchLeftHanded=True");
        GameSettings.write(ini,changed+"\r\n[Other]\r\nPreserve=1\r\n");
        assert withLayout.reloadIfChanged();
        assert withLayout.getString("language","").equals("RUS");
        assert withLayout.getBoolean("touchLeftHanded",false);
        assert withLayout.getFloat("touchX0",0)==.5f;
        withLayout.put("fpsLimit",60);withLayout.save();
        assert GameSettings.read(ini).contains("Preserve=1");
        try {new PortPreferences(null);throw new AssertionError();}catch(IOException expected){}
        System.out.println("PASS: settings defaults, malformed/nonfinite values, save verification, atomic replacement and restart");
    }
}
'''
        java = Path(os.environ["JAVA_HOME"]) / "bin"
        with tempfile.TemporaryDirectory(prefix="ut3-prefs-") as temporary:
            folder = Path(temporary)
            source = folder / "PreferencesCheck.java"
            source.write_text(program, encoding="utf-8")
            subprocess.run([java / "javac.exe", "--release", "8", "-d", folder,
                            ROOT / "android/app/src/org/ut3/port/PortPreferences.java",
                            ROOT / "android/app/src/org/ut3/port/GameSettings.java",
                            ROOT / "android/app/src/org/ut3/port/GameFiles.java", source], check=True)
            subprocess.run([java / "java.exe", "-ea", "-cp", folder,
                            "org.ut3.port.PreferencesCheck", folder], check=True)


if __name__ == "__main__":
    unittest.main()
