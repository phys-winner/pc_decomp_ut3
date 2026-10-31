"""Run the production Java INI adapter and its real file/backup operations."""
import os
import json
from pathlib import Path
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]


class GameSettingsTest(unittest.TestCase):
    def test_renderer_owned_frame_pacing_and_unchanged_other_settings(self):
        source = r'''
package org.ut3.port;
import java.io.*;
import java.nio.file.*;
import java.nio.charset.*;
import java.util.*;
public class PacingCheck {
    static List<String> entries(String text,String section,String key) {
        List<String> values=new ArrayList<>();boolean active=false;
        for(String line:text.split("\\r?\\n")) {
            String t=line.trim();int equals=t.indexOf('=');
            if(t.startsWith("[")&&t.endsWith("]")) active=t.equalsIgnoreCase("["+section+"]");
            else if(active&&equals>0&&t.substring(0,equals).trim().equalsIgnoreCase(key)) values.add(t.substring(equals+1).trim());
        }
        return values;
    }
    static void value(String text,String section,String key,String expected) {
        assert entries(text,section,key).equals(Arrays.asList(expected)):section+":"+key;
    }
    public static void main(String[] args)throws Exception {
        Path root=Paths.get(args[0]);File game=new File(args[1]);
        String profile=GameSettings.read(new File(game,"UTGame/Config/DefaultCompat.ini"));
        String current="[Engine.GameEngine]\r\nbSmoothFrameRate=TRUE\r\nMaxSmoothedFrameRate=62\r\nMinSmoothedFrameRate=22\r\nOtherTiming=preserve\r\n[engine.gameengine]\r\nbSmoothFrameRate=TRUE\r\n[SystemSettings]\r\nScreenPercentage=100\r\nUnknownVideo=stay\r\n[Custom]\r\n; user comment\r\nEntry=a\r\nEntry=a\r\nName=Игрок\r\n";
        int cases=0;
        for(String renderer:new String[]{"gles","dxvk","dxvk-turnip"}) {
            boolean nativePacer=!renderer.equals("gles");
            assert GameSettings.nativeFramePacer(renderer)==nativePacer;
            for(int fps:new int[]{30,45,60,90,120,144,240,1000}) {
                String baseline=GameSettings.minimum(current,profile,2400,1080,fps);
                String candidate=GameSettings.minimum(current,profile,2400,1080,fps,nativePacer);
                assert candidate.equals(nativePacer?baseline.replace("bSmoothFrameRate=TRUE","bSmoothFrameRate=FALSE"):baseline):"Additional pacing/config delta";
                value(candidate,"Engine.GameEngine","bSmoothFrameRate",nativePacer?"FALSE":"TRUE");
                value(candidate,"Engine.GameEngine","MaxSmoothedFrameRate",Integer.toString(fps));
                value(candidate,"Engine.GameEngine","MinSmoothedFrameRate","22");
                value(candidate,"Engine.GameEngine","OtherTiming","preserve");
                value(candidate,"SystemSettings","ScreenPercentage","100");
                value(candidate,"SystemSettings","UseVsync","False");
                value(candidate,"SystemSettings","ResX","2400");value(candidate,"SystemSettings","ResY","1080");
                value(candidate,"SystemSettings","UnknownVideo","stay");
                assert entries(candidate,"Custom","Entry").equals(Arrays.asList("a","a"));
                assert candidate.contains("; user comment")&&candidate.contains("Name=Игрок");
                Path folder=root.resolve(renderer+"-"+fps);Files.createDirectories(folder);
                File engine=folder.resolve("UTEngine.ini").toFile();byte[] original=current.getBytes(StandardCharsets.UTF_8);Files.write(engine.toPath(),original);
                GameSettings.prepare(engine,game,2400,1080,fps,"INT",renderer);
                String prepared=GameSettings.read(engine);value(prepared,"Engine.GameEngine","bSmoothFrameRate",nativePacer?"FALSE":"TRUE");
                value(prepared,"Engine.GameEngine","MaxSmoothedFrameRate",Integer.toString(fps));
                value(prepared,"SystemSettings","ScreenPercentage","100");
                assert prepared.contains("Name=Игрок")&&entries(prepared,"Custom","Entry").equals(Arrays.asList("a","a"));
                assert Arrays.equals(Files.readAllBytes(folder.resolve("UTEngine.before-widescreen-120.ini.bak")),original);
                byte[] saved=Files.readAllBytes(engine.toPath()),compat=Files.readAllBytes(folder.resolve("UTCompat.ini"));
                GameSettings.prepare(engine,game,2400,1080,fps,"INT",renderer);
                assert Arrays.equals(Files.readAllBytes(engine.toPath()),saved)&&Arrays.equals(Files.readAllBytes(folder.resolve("UTCompat.ini")),compat);
                // Switching to GLES restores the original policy on the next launch.
                GameSettings.prepare(engine,game,2400,1080,fps,"INT","gles");
                value(GameSettings.read(engine),"Engine.GameEngine","bSmoothFrameRate","TRUE");++cases;
            }
        }
        Path invalid=root.resolve("invalid");Files.createDirectories(invalid);File engine=invalid.resolve("UTEngine.ini").toFile();
        byte[] original=current.getBytes(StandardCharsets.UTF_8),compat="unmodified compatibility".getBytes(StandardCharsets.UTF_8),backup="legacy backup".getBytes(StandardCharsets.UTF_8);
        Files.write(engine.toPath(),original);Files.write(invalid.resolve("UTCompat.ini"),compat);Files.write(invalid.resolve("UTGame.before-widescreen-120.ini"),backup);
        for(String renderer:new String[]{null,"","DXVK","dxvk-other","unknown"}) {
            try {GameSettings.prepare(engine,game,2400,1080,60,"INT",renderer);throw new AssertionError("Invalid renderer accepted");}catch(IllegalArgumentException expected){}
        }
        for(int fps:new int[]{0,1001}) {
            try {GameSettings.prepare(engine,game,2400,1080,fps,"INT","dxvk");throw new AssertionError("Invalid rate accepted");}catch(IllegalArgumentException expected){}
        }
        try {GameSettings.prepare(engine,game,1080,2400,60,"INT","dxvk");throw new AssertionError("Portrait accepted");}catch(IllegalArgumentException expected){}
        try {GameSettings.prepare(engine,game,2400,1080,60,"../INT","dxvk");throw new AssertionError("Invalid language accepted");}catch(IllegalArgumentException expected){}
        assert Arrays.equals(Files.readAllBytes(engine.toPath()),original)&&Arrays.equals(Files.readAllBytes(invalid.resolve("UTCompat.ini")),compat)&&Arrays.equals(Files.readAllBytes(invalid.resolve("UTGame.before-widescreen-120.ini")),backup);
        assert Files.list(invalid).count()==3:"Validation wrote files";
        File legacy=root.resolve("legacy/UTEngine.ini").toFile();
        GameSettings.prepare(legacy,game,2400,1080,60,"INT");
        value(GameSettings.read(legacy),"Engine.GameEngine","bSmoothFrameRate","TRUE");
        System.out.println("PASS: "+cases+" backend/rate cases, DXVK native-only pacing/GLES original pacing, unchanged rates/render100/other settings, real atomic writes/backups/idempotence/backend return, fail-before-write validation and legacy overload");
    }
}
'''
        java = Path(os.environ["JAVA_HOME"]) / "bin"
        with tempfile.TemporaryDirectory(prefix="ut3-pacing-") as temporary:
            folder = Path(temporary)
            test = folder / "PacingCheck.java"
            test.write_text(source, encoding="utf-8")
            subprocess.run([java / "javac.exe", "--release", "8", "-encoding", "UTF-8", "-d", folder,
                            ROOT / "android/app/src/org/ut3/port/GameSettings.java",
                            ROOT / "android/app/src/org/ut3/port/GameFiles.java", test], check=True)
            result = subprocess.run([java / "java.exe", "-ea", "-cp", folder, "org.ut3.port.PacingCheck", folder,
                                     Path(json.loads((ROOT / "config/inputs.json").read_text())["source_root"])], check=True, capture_output=True)
            out = ROOT / "build/dxvk-frame-pacer"
            out.mkdir(parents=True, exist_ok=True)
            (out / "java-settings.log").write_bytes(result.stdout + result.stderr)
            print(result.stdout.decode().strip())

    def test_widescreen_minimum_60_language_and_preserved_user_data(self):
        source = r'''
package org.ut3.port;
import java.io.*;
import java.nio.file.*;
import java.nio.charset.*;
import java.util.*;
public class SettingsCheck {
    static List<String> entries(String text, String section, String key) {
        List<String> values=new ArrayList<>();boolean active=false;
        for(String line:text.split("\r?\n")) {
            String t=line.trim();
            if(t.startsWith("[") && t.endsWith("]")) active=t.equalsIgnoreCase("["+section+"]");
            else if(active && t.indexOf('=')>0 && t.substring(0,t.indexOf('=')).trim().equalsIgnoreCase(key))
                values.add(t.substring(t.indexOf('=')+1).trim());
        }
        return values;
    }
    static void inherited(String text) {
        assert entries(text,"TextureStreaming","MinRequestedMipsToConsider").equals(Arrays.asList("11"));
        assert entries(text,"TextureStreaming","FudgeFactorIncreaseRateOfChange").equals(Arrays.asList("0.5"));
        assert entries(text,"Engine.Engine","GameEngine").equals(Arrays.asList("Engine.GameEngine"));
        assert entries(text,"Core.System","Paths").equals(Arrays.asList("..\\Engine\\Content","..\\UTGame\\Content","..\\UTGame\\DLC","..\\UTGame\\__Trashcan"));
        assert entries(text,"Core.System","CutdownPaths").equals(Arrays.asList("..\\UTGame\\CutdownPackages","..\\UTGame\\Script"));
        assert entries(text,"Engine.Engine","LightComplexityColors").size()==6;
        assert !GameSettings.legacySeed(text);
    }
    static void minimumBuckets(String text, String profile, int width) {
        String minimum=GameSettings.minimum("",profile,width);
        boolean active=false; int keys=0;
        for(String line:minimum.split("\r?\n")) {
            String t=line.trim();
            if(t.startsWith("[") && t.endsWith("]")) active=t.equals("[SystemSettings]");
            else if(active && t.indexOf('=')>0) {
                String key=t.substring(0,t.indexOf('=')).trim();
                for(int bucket=1;bucket<=5;bucket++)
                    assert entries(text,"AppCompatBucket"+bucket,key).equals(Arrays.asList(t.substring(t.indexOf('=')+1).trim())):key+" bucket"+bucket;
                keys++;
            }
        }
        assert keys==57:keys;
    }
    public static void main(String[] args) throws Exception {
        assert GameSettings.renderWidth(2780,1264)==1056;
        assert GameSettings.renderWidth(640,480)==640;
        assert GameSettings.renderWidth(1920,1080)==856;
        try {GameSettings.renderWidth(0,480);throw new AssertionError();}catch(IllegalArgumentException expected){}
        try {GameSettings.renderWidth(1264,2780);throw new AssertionError();}catch(IllegalArgumentException expected){}
        String profile="[AppCompatBucket1]\nResX=640\nResY=480\nDetailMode=0\nUseVsync=False\nScreenPercentage=100\nTEXTUREGROUP_World=(MinLODSize=256,MaxLODSize=256,LODBias=0)\n[AppCompatBucket2]\nDetailMode=2\n";
        String current="[Configuration]\r\nBasedOn=original.ini\r\n[SystemSettings]\r\n; preserved comment\r\nResX=800\r\nUnknownOption=stay\r\n[Audio]\r\nVolume=0.75\r\nName=Игрок\r\n[systemsettings]\r\nrESx=1920\r\n[Engine.GameEngine]\r\nMaxSmoothedFrameRate=62\r\n";
        String result=GameSettings.minimum(current,profile,1056);
        assert result.contains("ResX=1056\r\n") && result.contains("ResY=480\r\n");
        assert result.contains("ScreenPercentage=100\r\n") && result.contains("UseVsync=False\r\n");
        assert result.contains("DetailMode=0\r\n") && !result.contains("DetailMode=2");
        assert result.contains("MaxSmoothedFrameRate=60\r\n") && result.contains("bSmoothFrameRate=TRUE\r\n");
        assert result.contains("BasedOn=original.ini") && result.contains("Volume=0.75") && result.contains("Name=Игрок");
        assert result.contains("UnknownOption=stay") && result.contains("; preserved comment");
        assert !result.contains("rESx=1920") && !result.contains("ResX=800");
        assert GameSettings.minimum(result,profile,1056).equals(result);
        assert GameSettings.minimum("",profile,1056).contains("[SystemSettings]");
        try{GameSettings.minimum(current,"[Other]\nResX=640",1056);throw new AssertionError();}catch(IllegalArgumentException expected){}
        Path folder=Paths.get(args[0]);File ini=folder.resolve("UTEngine.ini").toFile();
        Files.write(ini.toPath(),current.getBytes(StandardCharsets.UTF_8));
        GameSettings.write(ini,result);
        byte[] saved=Files.readAllBytes(ini.toPath());assert saved[0]==(byte)0xff && saved[1]==(byte)0xfe;
        assert GameSettings.read(ini).equals(result);
        Path backup=folder.resolve("UTEngine.before-widescreen-120.ini.bak");
        assert Arrays.equals(Files.readAllBytes(backup),current.getBytes(StandardCharsets.UTF_8));
        GameSettings.write(ini,result.replace("1056","856"));
        assert GameSettings.read(ini).contains("ResX=856");
        assert Arrays.equals(Files.readAllBytes(backup),current.getBytes(StandardCharsets.UTF_8));
        assert !folder.resolve("UTEngine.ini.ut3.tmp").toFile().exists();
        Files.write(ini.toPath(),new byte[]{(byte)0xff,(byte)0xfe,65});
        try{GameSettings.read(ini);throw new AssertionError();}catch(IOException expected){}
        Path project=Paths.get(args[1]), game=Paths.get(args[2]);
        String base=GameSettings.read(game.resolve("Engine/Config/BaseEngine.ini").toFile());
        String defaults=GameSettings.read(game.resolve("UTGame/Config/DefaultEngine.ini").toFile());
        String compatibility=GameSettings.read(game.resolve("UTGame/Config/DefaultCompat.ini").toFile());
        String fresh=GameSettings.bootstrap(null,base,defaults);
        inherited(fresh);
        assert entries(fresh,"TextureStreaming","PoolSize").equals(Arrays.asList("158"));
        List<String> ordered=new ArrayList<>();
        boolean inMap=false;
        for(String line:defaults.split("\r?\n")) {
            if(line.startsWith("["))inMap=line.equals("[Engine.PackagesToForceCookPerMap]");
            else if(inMap && line.startsWith("."))ordered.add(line.substring(1));
        }
        List<String> actual=new ArrayList<>();inMap=false;
        for(String line:fresh.split("\r?\n")) {
            if(line.startsWith("["))inMap=line.equalsIgnoreCase("[Engine.PackagesToForceCookPerMap]");
            else if(inMap && (line.startsWith("Map=") || line.startsWith("Package=")))actual.add(line);
        }
        assert ordered.equals(actual):"Map/package interleaving changed";
        String broken=GameSettings.minimum(defaults,compatibility,1080).replace("PoolSize=158","PoolSize=123")+"\r\n[Custom]\r\n; user comment\r\nName=Игрок\r\n";
        assert GameSettings.legacySeed(broken);
        String repaired=GameSettings.bootstrap(broken,base,defaults);
        inherited(repaired);
        assert entries(repaired,"TextureStreaming","PoolSize").equals(Arrays.asList("123"));
        assert repaired.contains("; user comment") && repaired.contains("Name=Игрок");
        assert GameSettings.bootstrap(repaired,base,defaults).equals(repaired);
        String reported=GameSettings.read(project.resolve("research/pixel10-crash/reported-config/UTEngine.ini").toFile());
        assert GameSettings.legacySeed(reported);
        assert entries(reported,"TextureStreaming","MinRequestedMipsToConsider").isEmpty();
        String reportedRepair=GameSettings.bootstrap(reported,base,defaults);
        inherited(reportedRepair);
        assert entries(reportedRepair,"SystemSettings","ResX").equals(entries(reported,"SystemSettings","ResX"));
        assert entries(reportedRepair,"SystemSettings","ScreenPercentage").equals(entries(reported,"SystemSettings","ScreenPercentage"));
        String live=GameSettings.read(project.resolve("research/live-UTEngine.ini").toFile());
        assert live.contains("BasedOn=") && !GameSettings.legacySeed(live);
        assert GameSettings.bootstrap(live,base,defaults).equals(live):"Valid generated config changed";
        String custom="[Configuration]\nBasedOn=..\\Engine\\Config\\BaseEngine.ini\n[Core.System]\nPaths=custom\n[Custom]\nEntry=first\nEntry=first\nEntry=third\n";
        assert GameSettings.bootstrap(custom,base,defaults).equals(custom):"Valid user arrays/order changed";
        String operators="[Configuration]\nBasedOn=..\\Engine\\Config\\BaseEngine.ini\n[Core.System]\n+Paths=..\\Engine\\Content\n+Paths=..\\Engine\\Content\n.Paths=..\\Engine\\Content\n[Custom]\n!Entry=clear\n+Entry=a\n+Entry=a\n.Entry=a\n+Entry=b\n-Entry=b\n";
        String resolved=GameSettings.bootstrap(operators,base+"\n[Custom]\nEntry=old\n",defaults);
        assert entries(resolved,"Core.System","Paths").size()==2;
        assert entries(resolved,"Custom","Entry").equals(Arrays.asList("a","a"));
        try {GameSettings.bootstrap(null,base,"[Configuration]\nBasedOn=unexpected.ini");throw new AssertionError();}catch(IllegalArgumentException expected){}
        try {GameSettings.bootstrap(null,"",defaults);throw new AssertionError();}catch(IllegalArgumentException expected){}
        File newIni=folder.resolve("first-run/UTEngine.ini").toFile();
        GameSettings.prepare(newIni,game.toFile(),1080);
        inherited(GameSettings.read(newIni));
        assert entries(GameSettings.read(newIni),"SystemSettings","ResX").equals(Arrays.asList("1080"));
        assert !new File(newIni.getParentFile(),"UTEngine.before-bootstrap-repair.ini.bak").exists();
        File freshCompat=new File(newIni.getParentFile(),"UTCompat.ini");
        String freshCompatText=GameSettings.read(freshCompat);
        minimumBuckets(freshCompatText,compatibility,1080);
        String baseCompat=GameSettings.read(game.resolve("Engine/Config/BaseCompat.ini").toFile());
        assert entries(freshCompatText,"AppCompat","CPUScore1").equals(entries(baseCompat,"AppCompat","CPUScore1"));
        assert entries(baseCompat,"AppCompatGPU-0x10DE","0x0162").size()==2;
        assert entries(freshCompatText,"AppCompatGPU-0x10DE","0x0162").equals(entries(baseCompat,"AppCompatGPU-0x10DE","0x0162"));
        assert entries(freshCompatText,"AppCompatGPU-0x10DE","VendorName").equals(Arrays.asList("NVIDIA"));
        assert freshCompatText.contains("[AppCompat]") && freshCompatText.contains("[AppCompatBucket1]");
        assert !new File(newIni.getParentFile(),"UTCompat.before-widescreen-120.ini.bak").exists();
        byte[] freshCompatBytes=Files.readAllBytes(freshCompat.toPath());
        GameSettings.prepare(newIni,game.toFile(),1080);
        assert Arrays.equals(Files.readAllBytes(freshCompat.toPath()),freshCompatBytes):"Compatibility preparation not idempotent";
        assert Arrays.equals(Files.readAllBytes(new File(newIni.getParentFile(),"UTCompat.before-widescreen-120.ini.bak").toPath()),freshCompatBytes);
        assert !new File(newIni.getParentFile(),"UTCompat.ini.ut3.tmp").exists();
        File repairIni=folder.resolve("legacy/UTEngine.ini").toFile();repairIni.getParentFile().mkdirs();
        byte[] originalBroken=broken.getBytes(StandardCharsets.UTF_8);
        Files.write(repairIni.toPath(),originalBroken);
        GameSettings.prepare(repairIni,game.toFile(),1080);
        inherited(GameSettings.read(repairIni));
        Path repairBackup=repairIni.toPath().resolveSibling("UTEngine.before-bootstrap-repair.ini.bak");
        assert Arrays.equals(Files.readAllBytes(repairBackup),originalBroken);
        byte[] after=Files.readAllBytes(repairIni.toPath());
        GameSettings.prepare(repairIni,game.toFile(),1080);
        assert Arrays.equals(Files.readAllBytes(repairIni.toPath()),after);
        assert Arrays.equals(Files.readAllBytes(repairBackup),originalBroken);
        File validIni=folder.resolve("valid/UTEngine.ini").toFile();validIni.getParentFile().mkdirs();
        String valid=GameSettings.minimum(live,compatibility,1080);
        Files.write(validIni.toPath(),valid.getBytes(StandardCharsets.UTF_16LE));
        // prepare normally receives a BOM as generated by the game/our writer.
        GameSettings.write(validIni,valid);
        GameSettings.prepare(validIni,game.toFile(),1080);
        String prepared=GameSettings.read(validIni);
        assert entries(prepared,"Engine.Engine","Language").equals(Arrays.asList("INT"));
        assert entries(prepared,"WinDrv.WindowsClient","AllowJoystickInput").equals(Arrays.asList("1"));
        GameSettings.prepare(validIni,game.toFile(),1080);
        assert GameSettings.read(validIni).equals(prepared);
        assert !new File(validIni.getParentFile(),"UTEngine.before-bootstrap-repair.ini.bak").exists();
        File existingCompat=new File(validIni.getParentFile(),"UTCompat.ini");
        String reportedCompat=GameSettings.read(project.resolve("research/pixel10-crash/reported-config/UTCompat.ini").toFile());
        String editedCompat=reportedCompat.replace("CPUScore1=1000","CPUScore1=1234")+"\r\n[Custom]\r\n; preserved compat comment\r\nName=Игрок\r\nEntry=a\r\nEntry=a\r\n";
        byte[] compatOriginal=editedCompat.getBytes(StandardCharsets.UTF_8);
        Files.write(existingCompat.toPath(),compatOriginal);
        // Remove the backup created by the preceding prepare, then prove this
        // existing generated config is backed up separately from UTEngine.
        Files.deleteIfExists(new File(validIni.getParentFile(),"UTCompat.before-widescreen-120.ini.bak").toPath());
        GameSettings.prepare(validIni,game.toFile(),1056);
        String updatedCompat=GameSettings.read(existingCompat);
        minimumBuckets(updatedCompat,compatibility,1056);
        assert entries(updatedCompat,"AppCompat","CPUScore1").equals(Arrays.asList("1234"));
        assert entries(updatedCompat,"AppCompat","CompatLevelComposite").equals(entries(editedCompat,"AppCompat","CompatLevelComposite"));
        assert entries(updatedCompat,"Custom","Entry").equals(Arrays.asList("a","a"));
        assert updatedCompat.contains("; preserved compat comment") && updatedCompat.contains("Name=Игрок");
        Path compatBackup=existingCompat.toPath().resolveSibling("UTCompat.before-widescreen-120.ini.bak");
        assert Arrays.equals(Files.readAllBytes(compatBackup),compatOriginal);
        assert !Arrays.equals(Files.readAllBytes(validIni.toPath().resolveSibling("UTEngine.before-widescreen-120.ini.bak")),compatOriginal);
        GameSettings.prepare(validIni,game.toFile(),1056);
        assert GameSettings.read(existingCompat).equals(updatedCompat);
        assert Arrays.equals(Files.readAllBytes(compatBackup),compatOriginal);
        System.out.println("PASS: actual BaseEngine/DefaultEngine first-run inheritance; legacy seed repair with byte-exact immutable backup; user scalar overrides; +unique/.duplicates/map-package order; valid generated config unchanged");
        assert GameSettings.renderWidth(2400,1080,1080)==2400;
        String nativeVideo=GameSettings.minimum(current,profile,2400,1080,45);
        assert entries(nativeVideo,"SystemSettings","ResY").equals(Arrays.asList("1080"));
        assert entries(nativeVideo,"Engine.GameEngine","MaxSmoothedFrameRate").equals(Arrays.asList("45"));
        for(String language:GameSettings.LANGUAGES) GameSettings.validateLanguage(game.toFile(),language);
        try{GameSettings.validateLanguage(game.toFile(),"../INT");throw new AssertionError();}catch(IllegalArgumentException expected){}
        GameSettings.prepare(validIni,game.toFile(),2400,1080,60,"RUS");
        assert entries(GameSettings.read(validIni),"Engine.Engine","Language").equals(Arrays.asList("RUS"));
        assert entries(GameSettings.read(validIni),"SystemSettings","ScreenPercentage").equals(Arrays.asList("100"));
        assert entries(GameSettings.read(existingCompat),"AppCompatBucket5","ResY").equals(Arrays.asList("1080"));
        byte[] script=Files.readAllBytes(project.resolve("build/uscript/AndroidPort.u"));
        String options=new String(Files.readAllBytes(project.resolve("android/uscript/AndroidPort/AndroidPortOptions.ini")),StandardCharsets.UTF_8);
        GameSettings.prepareNativeUi(validIni,game.toFile(),folder.toFile(),script,options);
        String uiEngine=GameSettings.read(validIni);
        assert entries(uiEngine,"Engine.Engine","GameViewportClientClassName").equals(Arrays.asList("AndroidPort.AndroidPortViewportClient"));
        assert entries(uiEngine,"Engine.DataStoreClient","GlobalDataStoreClasses").contains("AndroidPort.AndroidPortDataStore");
        assert entries(uiEngine,"Engine.DataStoreClient","GlobalDataStoreClasses").contains("AndroidPort.AndroidPortValueStore");
        assert entries(uiEngine,"Core.System","Paths").size()==entries(prepared,"Core.System","Paths").size()+1;
        String uiGame=GameSettings.read(new File(validIni.getParentFile(),"UTGame.ini"));
        assert uiGame.contains("OptionSet=AndroidPort") && uiGame.contains("<AndroidPort:language>");
        assert entries(uiGame,"UTGame.UTPlayerController","ForceFeedbackManagerClassName").equals(Arrays.asList("WinDrv.XnaForceFeedbackManager"));
        Path installed=folder.resolve("Documents/My Games/Unreal Tournament 3/UTGame/Published/CookedPC/Script/AndroidPort.u");
        assert Arrays.equals(script,Files.readAllBytes(installed));
        GameSettings.prepareNativeUi(validIni,game.toFile(),folder.toFile(),script,options);
        assert GameSettings.read(validIni).equals(uiEngine):"Native UI engine config must be idempotent";
        assert GameSettings.read(new File(validIni.getParentFile(),"UTGame.ini")).equals(uiGame):"Native options config must be idempotent";
        assert entries(uiGame,"AndroidPort.AndroidPortDataStore","Choices").size()==5;
        assert entries(uiGame,"AndroidPort.AndroidPortDataStore","StringData").isEmpty();
        File runtimeOptions=new File(validIni.getParentFile(),"UTGame.ini");
        GameSettings.write(runtimeOptions,uiGame+"[AndroidPort_editLayout UTUIDataProvider_MenuOption]\nOptionSet=AndroidPort\n"+
            "DataStoreMarkup=<AndroidPortValues:editLayout>\n[AndroidPort.AndroidPortDataStore]\nStringData=(Tag=\"renderer\",DefaultValueIndex=0)\n");
        GameSettings.prepareNativeUi(validIni,game.toFile(),folder.toFile(),script,options);
        assert GameSettings.read(runtimeOptions).equals(uiGame):"Options an older build defined must not survive an update";
        String russianOptions=new String(Files.readAllBytes(project.resolve("android/uscript/AndroidPort/AndroidPortOptions-RUS.ini")),StandardCharsets.UTF_8);
        GameSettings.prepareNativeUi(validIni,game.toFile(),folder.toFile(),script,russianOptions);
        String russianGame=GameSettings.read(new File(validIni.getParentFile(),"UTGame.ini"));
        assert entries(russianGame,"AndroidPort.AndroidPortDataStore","Choices").size()==5;
        assert russianGame.contains("Частота дисплея");
        GameSettings.prepareNativeUi(validIni,game.toFile(),folder.toFile(),script,options);
        String englishGame=GameSettings.read(new File(validIni.getParentFile(),"UTGame.ini"));
        assert !englishGame.contains("Частота дисплея") && entries(englishGame,"AndroidPort.AndroidPortDataStore","Choices").size()==5;
        Path legacyProvider=validIni.toPath().resolveSibling("UTGame.before-widescreen-120.ini");
        Path retiredProvider=validIni.toPath().resolveSibling("UTGame.before-widescreen-120.ini.bak");
        Files.deleteIfExists(retiredProvider);
        byte[] oldProvider="[OriginalMutator UTUIDataProvider_Mutator]\nClassName=UTGame.UTMutator_Instagib\n".getBytes(StandardCharsets.UTF_8);
        Files.write(legacyProvider,oldProvider);
        GameSettings.prepare(validIni,game.toFile(),2400,1080,60,"RUS");
        assert !Files.exists(legacyProvider) && Arrays.equals(Files.readAllBytes(retiredProvider),oldProvider):"Backups must not register duplicate providers";
        Files.write(legacyProvider,"different backup".getBytes(StandardCharsets.UTF_8));
        try{GameSettings.prepare(validIni,game.toFile(),2400,1080,60,"RUS");throw new AssertionError();}catch(IOException expected){}
        assert Files.exists(legacyProvider) && Arrays.equals(Files.readAllBytes(retiredProvider),oldProvider):"Conflicting backups must never overwrite data";
        try{GameSettings.prepareNativeUi(validIni,game.toFile(),folder.toFile(),new byte[32],options);throw new AssertionError();}catch(IOException expected){}
        System.out.println("PASS: full-width/native resolution, minimum profile/100% scale, default cap60 and45 override, all9 installed language inputs, Russian selector, analog input, preservation and idempotence");
        System.out.println("PASS: BaseCompat inheritance, all5 buckets/57 minimum settings, measured thresholds/history/custom values preserved, immutable backups and atomic writes");
    }
}
'''
        java = Path(os.environ["JAVA_HOME"]) / "bin"
        with tempfile.TemporaryDirectory(prefix="ut3-settings-") as temporary:
            folder = Path(temporary)
            test = folder / "SettingsCheck.java"
            test.write_text(source, encoding="utf-8")
            subprocess.run([java / "javac.exe", "--release", "8", "-encoding", "UTF-8", "-d", folder,
                            ROOT / "android/app/src/org/ut3/port/GameSettings.java",
                            ROOT / "android/app/src/org/ut3/port/GameFiles.java", test], check=True)
            subprocess.run([java / "java.exe", "-ea", "-cp", folder, "org.ut3.port.SettingsCheck", folder, ROOT,
                            Path(json.loads((ROOT / "config/inputs.json").read_text())["source_root"])], check=True)


if __name__ == "__main__":
    unittest.main()
