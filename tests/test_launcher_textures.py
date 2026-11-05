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
            'private void refreshTextures(', 'private void placeWork('))
        program = r'''
import java.io.File;
import java.util.*;
import java.util.concurrent.Callable;
class TextView { Runnable click; LinearLayout parent;TextView(){} TextView(Runnable r){click=r;}LinearLayout getParent(){return parent;} }
class LinearLayout extends TextView {
    List<Object> children=new ArrayList<>();
    void addView(Object x){assert ((TextView)x).parent==null;children.add(x);((TextView)x).parent=this;}
    void removeView(Object x){assert children.remove(x);((TextView)x).parent=null;}
    void removeAllViews(){for(Object x:children)((TextView)x).parent=null;children.clear();}
    int getChildCount(){return children.size();}Object getChildAt(int i){return children.get(i);}
}
class Ui {
    static final int SECONDARY=0;int error,muted,text,good;
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
class Work {static class Job {String title;Job(String t){title=t;}}static Job current;static boolean busy(){return false;}}
class Texts {
    static final int TEXTURES=1,TEX_SCANNING=2,TEX_RESCAN=3,TEX_DONE=4,TEX_GONE=5,TEX_REMOVE=6,
        TEX_REMOVE_ASK=7,DELETE=8,CANCEL=9,TEX_UNDO=10,TEX_WHY=11,TEX_FOUND=12,TEX_CONVERT=13,TEX_ESTIMATE=14,JOB_TEXTURES=15;
    static String t(Object c,int id){return "text:"+id;}static String fmt(Object c,int id,Object...v){return t(c,id);}
    static String size(Object c,long b){return "bytes";}
}
class Log {static void w(String tag,String s){}}
interface TextureBackend {
    int ASTC=2,ETC2=1;class Scan {long textures,bytes,convertedBytes,removable;int converted;boolean originalsRemoved,estimated;String failure;}
    Scan scan(File root);
}
class Backend implements TextureBackend {
    static int scans;static boolean fail;
    public Scan scan(File root){++scans;if(fail)throw new RuntimeException("Cache probe failed");Scan s=new Scan();s.textures=34833;s.estimated=true;return s;}
}
public class ScanCheck {
    interface Done<T>{void accept(T result);}interface Operation<T>{T run(File index,int fd);}
    Spec spec=new Spec();Spec.Distribution selected=new Spec.Distribution();TextureBackend textures;
    TextureBackend.Scan textureScan;String textureSource,busy,textureMessage;boolean textureScanning,textureError;Gpu gpu=new Gpu();Ui ui=new Ui();
    LinearLayout texturesBox=new LinearLayout();static final int TEX_REMOVE_ORIGINALS=3;
    LinearLayout workHome=new LinearLayout(),work=ui.card(workHome);
    Runnable pending;int jobs,refreshes;boolean failOpen;
    <T>T withTextureArchive(Spec.Distribution d,Operation<T> op){
        if(failOpen)throw new RuntimeException("Cannot open game archive");
        return op.run(new File("index.zmi"),17);
    }
    <T>void background(Callable<T> work,Done<T> done,Done<Throwable> failed){
        assert pending==null;pending=()->{pending=null;try{done.accept(work.call());}catch(Throwable t){failed.accept(t);}};
    }
    void refresh(){++refreshes;refreshTextures(true);placeWork();}void startTextures(int format){++jobs;}
''' + methods.replace('android.util.Log.w', 'Log.w').replace('android.view.ViewGroup', 'LinearLayout') + r'''
    Label label(){return (Label)((LinearLayout)texturesBox.children.get(0)).children.get(1);}
    TextView button(){return (TextView)((LinearLayout)((LinearLayout)texturesBox.children.get(0)).children.get(2)).children.get(0);}
    public static void main(String[] args){
        ScanCheck c=new ScanCheck();c.failOpen=true;c.textures=new Backend();c.refresh();
        assert c.textureScanning&&c.textureScan==null&&c.label().text.equals("text:2");
        Runnable first=c.pending;c.refresh();assert c.pending==first&&Backend.scans==0;
        Install.mount="different.zip";c.refresh();String newer=c.textureSource;
        first.run();assert c.textureScan==null&&c.textureScanning&&c.pending!=null&&c.textureSource.equals(newer);
        c.pending.run();assert c.textureScan.textures==34833&&!c.textureScanning;
        c.textureScan.converted=2;assert c.convertedTextures();Install.folder="other";assert !c.convertedTextures();
        Backend.fail=true;c.refresh();c.pending.run();
        assert !c.textureScanning&&c.label().text.equals("Cache probe failed");
        Backend.fail=false;
        c.button().click.run();assert c.textureScanning&&c.textureScan==null&&c.pending!=null;
        c.pending.run();assert c.textureScan.failure==null&&c.textureScan.textures>0;
        Work.current=new Work.Job(Texts.t(c,Texts.JOB_TEXTURES));c.placeWork();
        Object firstCard=c.texturesBox.getChildAt(0);assert c.work.getParent()==firstCard;
        c.refresh();assert c.work.getParent()==c.texturesBox.getChildAt(0)&&c.work.getParent()!=firstCard;
        Work.current=null;c.placeWork();assert c.work.getParent()==c.workHome;
        Work.current=new Work.Job("Other operation");c.placeWork();assert c.work.getParent()==c.workHome;
        c.texturesBox.removeAllViews();Work.current=new Work.Job(Texts.t(c,Texts.JOB_TEXTURES));c.placeWork();
        assert c.work.getParent()==c.texturesBox.getChildAt(0);
        System.out.println("PASS texture card: pending/stale/retry, quick probe without archive, progress stays in rebuilt texture card, other jobs retain their home");
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
