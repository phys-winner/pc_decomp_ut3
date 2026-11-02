"""Exercise the launcher's actual scan/card callbacks on the PC, without a device."""
import os
from pathlib import Path
import subprocess
import unittest
from tests import test_touch_controls as touch_checks

ROOT = Path(__file__).resolve().parents[1]


class LauncherTexturesTest(unittest.TestCase):
    def test_pending_failure_retry_and_source_change(self):
        source = (ROOT / 'android/launcher/android/src/port/launcher/LauncherActivity.java').read_text(encoding='utf-8')
        methods = '\n'.join(touch_checks.TouchControlsTest._method(source, name) for name in (
            'private File textureRoot()', 'private String textureSourceKey()',
            'private void textureScanFinished(', 'private boolean convertedTextures()',
            'private void refreshTextures('))
        program = r'''
import java.io.File;
import java.util.*;
import java.util.concurrent.Callable;
class TextView { Runnable click; TextView(){} TextView(Runnable r){click=r;} }
class LinearLayout extends TextView {
    List<Object> children=new ArrayList<>();void addView(Object x){children.add(x);}void removeAllViews(){children.clear();}
}
class Ui {
    static final int SECONDARY=0;int error,muted,text;
    LinearLayout card(LinearLayout box){LinearLayout c=new LinearLayout();box.addView(c);return c;}
    TextView heading(String x){return label(x,0,0);}TextView label(String x,int s,int c){return new Label(x);}
    TextView button(String x,int style,Runnable r){return new TextView(r);}
    LinearLayout buttons(TextView... b){LinearLayout l=new LinearLayout();for(TextView v:b)l.addView(v);return l;}
    static void enable(TextView v,boolean b){}
    Dialog dialog(){return new Dialog();}void show(Dialog d){}
}
class Label extends TextView {String text;Label(String t){text=t;}}
class Dialog {
    interface Click {void accept(Object d,int w);}
    Dialog setTitle(String s){return this;}Dialog setMessage(String s){return this;}
    Dialog setPositiveButton(String s,Click c){return this;}Dialog setNegativeButton(String s,Click c){return this;}
}
class Spec {String textureBackend="Backend";static class Part {String id="game";}static class Distribution {
    String id="ut3";List<Part> parts=Arrays.asList(new Part());}}
class Install {
    static String mount="archive.zip",folder="sidecar";
    static File folder(Object c,Spec s,Spec.Distribution d,Spec.Part p){return new File(folder);}
    static String mount(Object c,Spec.Distribution d,Spec.Part p){return mount;}
    static boolean ready(Object c,Spec s,Spec.Distribution d){return true;}
}
class Gpu {boolean astc=true;}
class Work {static boolean busy(){return false;}}
class Texts {
    static final int TEXTURES=1,TEX_SCANNING=2,TEX_RESCAN=3,TEX_DONE=4,TEX_GONE=5,TEX_REMOVE=6,
        TEX_REMOVE_ASK=7,DELETE=8,CANCEL=9,TEX_UNDO=10,TEX_WHY=11,TEX_FOUND=12,TEX_CONVERT=13;
    static String t(Object c,int id){return "text:"+id;}static String fmt(Object c,int id,Object...v){return t(c,id);}
    static String size(Object c,long b){return "bytes";}
}
class Log {static void w(String tag,String s){}}
interface TextureBackend {
    int ASTC=2,ETC2=1;class Scan {long textures,bytes,convertedBytes,removable;int converted;boolean originalsRemoved;String failure;}
    Scan scan(File root,File index,int fd);
}
class Backend implements TextureBackend {
    static int scans;public Scan scan(File r,File i,int fd){assert fd==17&&i.getName().equals("index.zmi");++scans;Scan s=new Scan();s.textures=34833;return s;}
}
public class ScanCheck {
    interface Done<T>{void accept(T result);}interface Operation<T>{T run(File index,int fd);}
    Spec spec=new Spec();Spec.Distribution selected=new Spec.Distribution();TextureBackend textures;
    TextureBackend.Scan textureScan;String textureSource,busy;boolean textureScanning;Gpu gpu=new Gpu();Ui ui=new Ui();
    LinearLayout texturesBox=new LinearLayout();static final int TEX_REMOVE_ORIGINALS=3;
    Runnable pending;int jobs,refreshes;boolean failOpen;
    <T>T withTextureArchive(Spec.Distribution d,Operation<T> op){
        if(failOpen)throw new RuntimeException("Cannot open game archive");
        return op.run(new File("index.zmi"),17);
    }
    <T>void background(Callable<T> work,Done<T> done,Done<Throwable> failed){
        assert pending==null;pending=()->{pending=null;try{done.accept(work.call());}catch(Throwable t){failed.accept(t);}};
    }
    void refresh(){++refreshes;refreshTextures(true);}void startTextures(int format){++jobs;}
''' + methods.replace('android.util.Log.w', 'Log.w') + r'''
    Label label(){return (Label)((LinearLayout)texturesBox.children.get(0)).children.get(1);}
    TextView button(){return (TextView)((LinearLayout)((LinearLayout)texturesBox.children.get(0)).children.get(2)).children.get(0);}
    public static void main(String[] args){
        ScanCheck c=new ScanCheck();c.textures=new Backend();c.refresh();
        assert c.textureScanning&&c.textureScan==null&&c.label().text.equals("text:2");
        Runnable first=c.pending;c.refresh();assert c.pending==first&&Backend.scans==0;
        Install.mount="different.zip";c.refresh();String newer=c.textureSource;
        first.run();assert c.textureScan==null&&c.textureScanning&&c.pending!=null&&c.textureSource.equals(newer);
        c.pending.run();assert c.textureScan.textures==34833&&!c.textureScanning;
        c.textureScan.converted=2;assert c.convertedTextures();Install.folder="other";assert !c.convertedTextures();
        c.failOpen=true;c.refresh();c.pending.run();
        assert !c.textureScanning&&c.label().text.equals("Cannot open game archive");
        c.failOpen=false;
        c.button().click.run();assert c.textureScanning&&c.textureScan==null&&c.pending!=null;
        c.pending.run();assert c.textureScan.failure==null&&c.textureScan.textures>0;
        System.out.println("PASS texture card: visible pending, serialized scans, stale source rejected, failure/retry, ZIP FD forwarding, renderer cache identity");
    }
}
'''
        out = ROOT / 'build/launcher-texture-test'
        out.mkdir(parents=True, exist_ok=True)
        path = out / 'ScanCheck.java'
        path.write_text(program, encoding='utf-8')
        java = Path(os.environ['JAVA_HOME']) / 'bin'
        for command in ([java / 'javac.exe', '-d', out, path], [java / 'java.exe', '-ea', '-cp', out, 'ScanCheck']):
            result = subprocess.run(list(map(str, command)), capture_output=True, text=True)
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        print(result.stdout.strip())
