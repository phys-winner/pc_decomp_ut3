"""Run the production grouping and DXT badge logic with a small JVM view model."""
import os
from pathlib import Path
import re
import subprocess
import sys
import tempfile


def method(source, declaration):
    match = re.search(r"^    " + re.escape(declaration) + r"[^\n]*\{.*?^    }", source, re.M | re.S)
    assert match, declaration
    return match.group()


def check(root, folder):
    source = (root / "android/src/port/launcher/LauncherActivity.java").read_text(encoding="utf-8")
    ui = (root / "android/src/port/launcher/Ui.java").read_text(encoding="utf-8")
    methods = "\n".join(method(source, name) for name in (
        "private TextView message(", "private static void attach(", "private LinearLayout section(",
        "private LinearLayout notice(", "private String textureState(", "private TextView recoveryButton(",
        "private void rescanTextures(", "private LinearLayout group(", "private void refresh(int... sections)",
        "private Boolean dxtSupport(", "private void note(int section,", "private void note(String text,", "private boolean needsGpu(",
        "private String effectiveRenderer(", "private boolean launchWhenReady(", "@Override public void onUserInteraction("))
    program = r'''
import java.util.*;
import java.io.File;
import java.util.concurrent.Callable;
class View {
    static final int VISIBLE=0,GONE=8,ACCESSIBILITY_LIVE_REGION_POLITE=1;
    int visibility; ViewGroup parent; Object tag; boolean focused;
    void setVisibility(int v){visibility=v;} int getVisibility(){return visibility;}
    Object getParent(){return parent;}
    Object getTag(){return tag;}void setTag(Object v){tag=v;}boolean requestFocus(){focused=true;return true;}
}
class ViewGroup extends View {
    final List<View> children=new ArrayList<>();
    void addView(View v){assert v.parent==null;children.add(v);v.parent=this;}
    void removeView(View v){assert children.remove(v);v.parent=null;}
    void removeAllViews(){for(View v:children)v.parent=null;children.clear();}
    View getChildAt(int n){return children.get(n);}int getChildCount(){return children.size();}
    void removeViewAt(int n){removeView(children.get(n));}
    View findFocus(){for(View v:children){if(v.focused)return v;if(v instanceof ViewGroup){View f=((ViewGroup)v).findFocus();if(f!=null)return f;}}return null;}
    View findViewWithTag(Object t){for(View v:children){if(t.equals(v.tag))return v;if(v instanceof ViewGroup){View f=((ViewGroup)v).findViewWithTag(t);if(f!=null)return f;}}return null;}
}
class LinearLayout extends ViewGroup {}
class TextView extends View {
    String text=""; int colour; Runnable click;
    void setText(String v){text=v==null?"":v;} void setTextColor(int c){colour=c;}
    CharSequence getText(){return text;}int getCurrentTextColor(){return colour;}
    void setAccessibilityLiveRegion(int v){}
}
class Ui {
    static final int WARNING=3,SECONDARY=1; final int good=2,muted=4,accent=5,error=6;
    TextView button(String title,int kind,Runnable run){TextView t=label(title,15,text);t.click=run;t.tag=title;return t;}
    LinearLayout buttons(TextView... list){LinearLayout c=new LinearLayout();for(TextView v:list)c.addView(v);return c;}
    final int text=7;
    interface BoolSink{void set(boolean v);}
    static class Heading extends View {String summary;int colour;BoolSink click;}
    TextView label(String t,int sp,int c){TextView v=new TextView();v.setText(t);v.setTextColor(c);return v;}
    TextView heading(String t){return label(t,12,accent);}
    LinearLayout card(LinearLayout parent){LinearLayout c=new LinearLayout();parent.addView(c);return c;}
    View foldHeading(String title,String summary,int colour,boolean expanded,BoolSink changed){
        Heading h=new Heading();h.summary=summary;h.colour=colour;h.click=changed;return h;
    }
}
class Texts {
    static final int GAME_FILES=5,PLAY=0,RENDERER=59,SPACE=74,ABOUT=80,TEXTURES=98,
        TEX_DXT_YES=162,TEX_DXT_NO=163,TEX_DXT_UNKNOWN=164,PERM_FILES=40,UPDATE_AVAILABLE=90,OPTIONS=72,
        TEX_ORIGINALS=165,TEX_STATUS=166,TEX_CHECK_FAILED=167,COPY_DIAGNOSTICS=168,DIAGNOSTICS_COPIED=169,
        E_CLIPBOARD=170,RETRY_CHECK=171,OTHER_ARCHIVE=172,FOLDER=6,CHECKING=88;
    static String t(Object context,int id){return Integer.toString(id);}
    static String fmt(Object c,int id,Object... args){return t(c,id)+":"+Arrays.toString(args);}
}
class Gpu {boolean probed=true,s3tc;}
class TextureBackend {static final int ASTC=2;static class Scan {int converted;String failure;}}
class State {static final String CHOICES="choices";static int writes;static final Properties values=new Properties();interface Change{void apply(Properties p);}static boolean update(Object c,String f,Change change){++writes;change.apply(values);return true;}}
class Pickers {static final int FOLDER=1,ZIP=2;}
class Spec {
    static class Mode {String kind="gles";boolean vulkanKind(){return !kind.equals("gles");}}
    static class Distribution {List<String> renderers=new ArrayList<>(Arrays.asList("selected"));}
    final String defaultMode="selected";boolean updates=true;File storageRoot(){return new File(".");}
    final Mode selected=new Mode();Mode mode(String id){return id==null?null:selected;}
}
class Renderers {
    static class Status {} static int evaluations;
    static List<Status> evaluate(Object c,Spec s,Spec.Distribution d,Gpu gpu,boolean converted){++evaluations;return new ArrayList<>();}
    static String explicitChoice(Object c,Spec s,Spec.Distribution d,Object hooks){return null;}
    static String effective(String fallback,List<Status> statuses,String chosen,boolean explicit){return chosen;}
}
class Activity {public void onUserInteraction(){}}
class Probe {static Map<String,String> result;static Map<String,String> report(Object c){return result;}}
class Work {
    static class Job {final int section;boolean finished;Job(int s){section=s;}}
    static Job current;static boolean busy(){return current!=null&&!current.finished;}
}
public class GroupUiCheck extends Activity {
    final Ui ui=new Ui(); final Spec spec=new Spec();Gpu gpu=new Gpu();
    final Spec.Distribution selected=new Spec.Distribution();Object hooks;
    final Map<Integer,TextView> notes=new HashMap<>();
    final Map<Integer,LinearLayout> notices=new HashMap<>();
    TextureBackend.Scan textureScan;boolean textureScanning;String lastError="",textureSource="current";
    String textureSourceKey(){return "current";}
    static final int ASK_FOLDER=1,ASK_ZIP=2;int picked,cleanupScans,updateChecks,copies;
    final LinearLayout work=new LinearLayout(),files=new LinearLayout(),textures=new LinearLayout(),space=new LinearLayout();
    final LinearLayout permissions=new LinearLayout(),distributions=new LinearLayout(),renderer=new LinearLayout(),options=new LinearLayout(),
        texturesBox=textures,update=new LinearLayout(),about=new LinearLayout();
    final List<Integer> rendered=new ArrayList<>();
    String busy;int busySection;boolean filesExpanded,texturesExpanded;int refreshes;
    boolean initializing,autoLaunchPending,playRequestedPending,finishing,autoResult;int autoCalls,requestedCalls;
    boolean autoLaunch(){++autoCalls;return autoResult;}void playRequested(){++requestedCalls;}
    boolean isFinishing(){return finishing;}boolean convertedTextures(){return false;}
    boolean idle(){return !initializing&&busy==null&&!Work.busy();}
    interface Done<T>{void accept(T v);}
    boolean deferBackground;final List<Runnable> pendingWrites=new ArrayList<>();
    <T>void background(Callable<T> work,Done<T> done,Done<Throwable> failed){
        Runnable task=()->{try{done.accept(work.call());}catch(Exception e){throw new AssertionError(e);}};
        if(deferBackground)pendingWrites.add(task);else task.run();
    }
    void pick(int kind,int request,File root){picked=kind;}
    void scanCleanup(){++cleanupScans;}void checkUpdates(boolean manual){++updateChecks;}void copyDiagnostics(){++copies;}
    String installationConflict(){return null;}
    void record(int id){++refreshes;rendered.add(id);}
    void refreshPermissions(){record(Texts.PERM_FILES);}void refreshDistributions(boolean idle){record(Texts.PLAY);}
    void refreshFiles(boolean idle){record(Texts.GAME_FILES);redraw(files,Texts.GAME_FILES);}
    void refreshRenderer(){record(Texts.RENDERER);}
    void refreshOptions(){record(Texts.OPTIONS);options.removeAllViews();TextView v=ui.label("Option",15,0);v.tag="option";options.addView(v);}
    void refreshTextures(boolean idle){record(Texts.TEXTURES);redraw(textures,Texts.TEXTURES);}
    void refreshSpace(boolean idle){record(Texts.SPACE);}void refreshUpdate(){record(Texts.UPDATE_AVAILABLE);}void refreshAbout(){record(Texts.ABOUT);}
    LinearLayout column(){return new LinearLayout();}
''' + methods + r'''
    LinearLayout redraw(LinearLayout holder,int id){holder.removeAllViews();return section(holder,id);}
    static Ui.Heading header(LinearLayout holder){return (Ui.Heading)((LinearLayout)holder.getChildAt(0)).getChildAt(0);}
    void badge(String kind,boolean gles,String bc,int expected,int colour){
        spec.selected.kind=kind;gpu.s3tc=gles;Probe.result=new HashMap<>();
        if(bc!=null)Probe.result.put((kind.equals("turnip")?"turnip":"stock")+".bc",bc);
        redraw(textures,Texts.TEXTURES);Ui.Heading h=header(textures);
        assert h.summary.equals(Integer.toString(expected));assert h.colour==colour;
    }
    static TextView uiOption(GroupUiCheck c){TextView v=c.ui.label("Option",15,0);v.tag="option";v.focused=true;c.options.addView(v);return v;}
    public static void main(String[] args){
        GroupUiCheck c=new GroupUiCheck();
        assert c.redraw(c.files,Texts.GAME_FILES).visibility==View.GONE;
        assert c.redraw(c.textures,Texts.TEXTURES).visibility==View.GONE;
        header(c.files).click.set(true);assert c.filesExpanded&&c.refreshes==1;
        assert c.redraw(c.files,Texts.GAME_FILES).visibility==View.VISIBLE;
        c.note(Texts.TEXTURES,"conversion failed",true);c.note(Texts.ABOUT,"up to date",false);
        c.redraw(c.textures,Texts.TEXTURES);
        assert c.message(Texts.TEXTURES).parent==c.notice(Texts.TEXTURES)&&c.notice(Texts.TEXTURES).parent==c.textures.getChildAt(0);
        assert c.message(Texts.TEXTURES).visibility==View.VISIBLE;
        assert c.message(Texts.TEXTURES).colour==c.ui.error;
        assert c.message(Texts.ABOUT).text.equals("up to date");
        assert c.notice(Texts.TEXTURES).getChildCount()==2;
        int writes=State.writes;c.note(Texts.TEXTURES,"conversion failed",true);
        assert State.writes==writes : "unchanged failures must not rewrite storage or rebuild actions";
        TextView retry=(TextView)((LinearLayout)c.notice(Texts.TEXTURES).getChildAt(1)).getChildAt(0);
        retry.click.run();assert c.rendered.contains(Texts.TEXTURES)&&c.message(Texts.TEXTURES).visibility==View.GONE;
        c.note(Texts.GAME_FILES,"bad archive",true);
        LinearLayout choices=(LinearLayout)c.notice(Texts.GAME_FILES).getChildAt(1);
        ((TextView)choices.getChildAt(0)).click.run();assert c.picked==Pickers.FOLDER;
        ((TextView)choices.getChildAt(1)).click.run();assert c.picked==Pickers.ZIP;
        c.textureScan=new TextureBackend.Scan();assert c.textureState().equals("165");
        c.textureScan.converted=2;assert c.textureState().contains("ASTC");
        c.textureScan.converted=1;assert c.textureState().contains("ETC2");
        c.textureScan.failure="bad";assert c.textureState().equals("167");
        c.textureScanning=true;assert c.textureState().equals("88");c.textureScanning=false;
        c.rendered.clear();View aboutCard=new View();c.about.addView(aboutCard);
        TextView oldOption=uiOption(c);c.refresh(Texts.OPTIONS);
        assert c.rendered.equals(Arrays.asList(Texts.OPTIONS));assert c.about.getChildAt(0)==aboutCard;
        assert c.options.getChildAt(0)!=oldOption&&c.options.getChildAt(0).focused;
        c.texturesExpanded=true;Work.current=new Work.Job(Texts.TEXTURES);
        assert c.redraw(c.textures,Texts.TEXTURES).visibility==View.GONE;
        assert c.work.parent==c.textures.getChildAt(0);
        Object old=c.work.parent;c.redraw(c.textures,Texts.TEXTURES);
        assert c.work.parent==c.textures.getChildAt(0)&&c.work.parent!=old;
        assert c.redraw(c.files,Texts.GAME_FILES).visibility==View.VISIBLE;
        GroupUiCheck reopened=new GroupUiCheck();reopened.redraw(reopened.textures,Texts.TEXTURES);
        assert reopened.work.parent==reopened.textures.getChildAt(0);
        Work.current=new Work.Job(Texts.SPACE);c.redraw(c.space,Texts.SPACE);
        assert c.work.parent==c.space.getChildAt(0);
        Work.current.finished=true;
        assert c.redraw(c.space,Texts.SPACE).visibility==View.VISIBLE;
        Work.current=null;c.busy="searching";c.busySection=Texts.GAME_FILES;
        assert c.redraw(c.files,Texts.GAME_FILES).visibility==View.GONE;
        c.busy=null;assert c.redraw(c.files,Texts.GAME_FILES).visibility==View.VISIBLE;
        c.badge("gles",true,null,Texts.TEX_DXT_YES,c.ui.good);
        c.badge("gles",false,null,Texts.TEX_DXT_NO,Ui.WARNING);
        c.gpu.probed=false;c.badge("gles",true,null,Texts.TEX_DXT_UNKNOWN,c.ui.muted);c.gpu.probed=true;
        c.badge("vulkan",false,"111",Texts.TEX_DXT_YES,c.ui.good);
        c.badge("vulkan",true,"101",Texts.TEX_DXT_NO,Ui.WARNING);
        c.badge("turnip",false,"111",Texts.TEX_DXT_YES,c.ui.good);
        c.badge("turnip",true,null,Texts.TEX_DXT_UNKNOWN,c.ui.muted);
        GroupUiCheck errors=new GroupUiCheck();errors.deferBackground=true;
        errors.note(Texts.GAME_FILES,"first error",true);errors.note(Texts.TEXTURES,"latest error",true);
        errors.pendingWrites.get(1).run();errors.pendingWrites.get(0).run();
        assert State.values.getProperty("lastError").equals("latest error") : "late background writes must keep the latest error";
        GroupUiCheck startup=new GroupUiCheck();startup.gpu=null;startup.initializing=true;
        startup.autoLaunchPending=startup.playRequestedPending=true;
        assert !startup.launchWhenReady()&&startup.autoCalls==0&&startup.requestedCalls==0;
        startup.initializing=false;
        assert startup.effectiveRenderer(startup.selected).equals("selected");
        int evaluated=Renderers.evaluations;
        assert !startup.launchWhenReady()&&startup.autoCalls==1&&startup.requestedCalls==1;
        assert Renderers.evaluations==evaluated : "GLES-only starts must not wait for the GPU probe";
        startup.spec.selected.kind="vulkan";startup.autoLaunchPending=startup.playRequestedPending=true;
        assert !startup.launchWhenReady()&&startup.autoLaunchPending;
        startup.onUserInteraction();startup.gpu=new Gpu();startup.launchWhenReady();
        assert startup.autoCalls==1&&startup.requestedCalls==1 : "a late probe must not launch after user input";
        startup.autoLaunchPending=true;startup.autoResult=true;
        assert startup.launchWhenReady()&&startup.autoCalls==2;
        startup.launchWhenReady();assert startup.autoCalls==2 : "startup launch is consumed once";
        System.out.println("PASS group UI and startup: folding, message/progress placement, DXT badges, no synchronous GPU wait, input cancels delayed autostart");
    }
}
'''
    path = folder / "GroupUiCheck.java"
    path.write_text(program, encoding="utf-8")
    java = Path(os.environ["JAVA_HOME"]) / "bin"
    for command in ([java / "javac.exe", "--release", "8", "-encoding", "UTF-8", "-d", folder, path],
                    [java / "java.exe", "-ea", "-cp", folder, "GroupUiCheck"]):
        subprocess.run(list(map(str, command)), check=True)
    return True


if __name__ == "__main__":
    root = Path(sys.argv[1]).resolve() if len(sys.argv) > 1 else Path(__file__).resolve().parents[1]
    with tempfile.TemporaryDirectory(prefix="launcher-group-ui-") as temporary:
        check(root, Path(temporary))
