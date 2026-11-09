"""Exercise the production report and clipboard failure handling without Android."""
import os
from pathlib import Path
import subprocess
import tempfile


def check(root, folder):
    with tempfile.TemporaryDirectory(prefix="diagnostics-", dir=folder) as temporary:
        work = Path(temporary)
        stubs = {
            "android/content/Context.java": '''package android.content;
public class Context {
    public ClipboardManager clipboard;
    public android.content.pm.PackageManager manager = new android.content.pm.PackageManager();
    public android.content.pm.PackageManager getPackageManager(){return manager;}
    public String getPackageName(){return "org.example.game";}
    public <T>T getSystemService(Class<T> type){return type.cast(clipboard);}
}''',
            "android/content/ClipData.java": '''package android.content;
public class ClipData {
    public String text;
    public static ClipData newPlainText(String label,String text){ClipData c=new ClipData();c.text=text;return c;}
}''',
            "android/content/ClipboardManager.java": '''package android.content;
public class ClipboardManager {
    public ClipData clip;public boolean denied;
    public void setPrimaryClip(ClipData value){if(denied)throw new SecurityException();clip=value;}
}''',
            "android/content/pm/PackageInfo.java": '''package android.content.pm;
public class PackageInfo {
    public String versionName="1.2";public int versionCode=7;
    public long getLongVersionCode(){return 4294967303L;}
}''',
            "android/content/pm/PackageManager.java": '''package android.content.pm;
public class PackageManager {
    public boolean missing;
    public static class NameNotFoundException extends Exception{}
    public PackageInfo getPackageInfo(String name,int flags)throws NameNotFoundException{
        if(missing)throw new NameNotFoundException();return new PackageInfo();
    }
}''',
            "android/os/Build.java": '''package android.os;
public class Build {
    public static String[] SUPPORTED_ABIS={"arm64-v8a"};
    public static class VERSION {public static int SDK_INT=37;public static String RELEASE="17";}
}''',
            "port/launcher/DiagnosticsCheck.java": '''package port.launcher;
public class DiagnosticsCheck {
    public static void main(String[] args){
        android.content.Context c=new android.content.Context();
        String report=Diagnostics.report(c,"Edition","GPU","Vulkan","ASTC","disk full");
        for(String part:new String[]{"org.example.game","1.2 (4294967303)","Edition","API 37","arm64-v8a","GPU","Vulkan","ASTC","disk full"})
            assert report.contains(part):part;
        android.os.Build.VERSION.SDK_INT=26;
        assert Diagnostics.report(c,"","","","",null).contains("1.2 (7)");
        c.manager.missing=true;
        report=Diagnostics.report(c,"","","","","");
        assert report.contains("Version: ?")&&report.contains("Last error: none");
        assert !Diagnostics.copy(c,report);
        c.clipboard=new android.content.ClipboardManager();
        assert Diagnostics.copy(c,report)&&c.clipboard.clip.text.equals(report);
        c.clipboard.denied=true;assert !Diagnostics.copy(c,report);
        System.out.println("PASS diagnostics: fields, API versions, missing metadata, clipboard success/unavailable/denied");
    }
}''',
        }
        sources = [root / "android/src/port/launcher/Diagnostics.java"]
        for name, text in stubs.items():
            path = work / name
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_text(text, encoding="utf-8")
            sources.append(path)
        java = Path(os.environ["JAVA_HOME"]) / "bin"
        subprocess.run(list(map(str, [java / "javac.exe", "--release", "8", "-d", work, *sources])), check=True)
        subprocess.run(list(map(str, [java / "java.exe", "-ea", "-cp", work, "port.launcher.DiagnosticsCheck"])), check=True)
    return True


if __name__ == "__main__":
    with tempfile.TemporaryDirectory(prefix="launcher-diagnostics-") as temporary:
        check(Path(__file__).resolve().parents[1], Path(temporary))
