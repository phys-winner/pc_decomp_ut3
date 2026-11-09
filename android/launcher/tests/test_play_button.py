"""Execute the real button drawing code with a recording Canvas on the JVM."""
import importlib.util
import json
import os
from pathlib import Path
import re
import subprocess
import sys
import tempfile


def extract(source, signature):
    match = re.search(r"^    " + re.escape(signature) + r"[^\n]*\{.*?^    }", source, re.M | re.S)
    assert match, signature
    return match.group()


STUBS = r'''
import java.util.*;
class Context {Object getContentResolver(){return this;}}
class View {
    static final int VISIBLE=0;boolean enabled=true,focus=true,shown=true;int visibility,width=240,height=60,frames,invalidations;
    Drawable background;Context context;
    View(Context c){context=c;}Context getContext(){return context;}
    int getWidth(){return width;}int getHeight(){return height;}boolean isEnabled(){return enabled;}
    boolean hasWindowFocus(){return focus;}int getWindowVisibility(){return visibility;}boolean isShown(){return shown;}
    void invalidate(){invalidations++;}void postInvalidateDelayed(long ms){assert ms==33;frames++;}
    protected void onSizeChanged(int w,int h,int ow,int oh){}
    void resize(int w,int h){width=w;height=h;onSizeChanged(w,h,0,0);}
    public void onWindowFocusChanged(boolean f){focus=f;}
    protected void onWindowVisibilityChanged(int v){visibility=v;}
    void setBackground(Drawable d){background=d;}void setFocusable(boolean b){}
    interface Click {void click(View v);}void setOnClickListener(Click c){}
}
class TextView extends View {
    int textColour;TextView(Context c){super(c);}void setText(String v){}void setTag(Object v){}
    void setGravity(int v){}void setTextSize(int v){}void setTypeface(Object v){}void setTextColor(int v){textColour=v;}
    void setMinHeight(int v){}void setPadding(int a,int b,int c,int d){}
    protected void onDraw(Canvas c){c.textLast=true;assert !c.clipped;}
}
class Button extends TextView {Button(Context c){super(c);}}
class Color {
    static final int TRANSPARENT=0,WHITE=0xffffffff;
    static int parseColor(String s){if(!s.matches("#[0-9a-fA-F]{6}([0-9a-fA-F]{2})?"))throw new IllegalArgumentException();
        return (int)Long.parseLong(s.substring(1),16)|(s.length()==7?0xff000000:0);}
}
class JSONObject extends HashMap<String,Object> {
    JSONObject putColour(String k,String v){put(k,v);return this;}
    String optString(String k,String fallback){return containsKey(k)?String.valueOf(get(k)):fallback;}
}
class Gravity {static final int CENTER=0;}
class Drawable {}
class GradientDrawable extends Drawable {
    int fill,stroke;void setColor(int c){fill=c;}void setStroke(int w,int c){stroke=c;}void setCornerRadius(int r){}
}
class ColorStateList {static ColorStateList valueOf(int c){return new ColorStateList();}}
class RippleDrawable extends Drawable {
    final GradientDrawable content;RippleDrawable(ColorStateList c,GradientDrawable d,GradientDrawable mask){content=d;}
}
class Shader {enum TileMode {CLAMP}}
class Matrix {void setTranslate(float x,float y){}}
class LinearGradient extends Shader {
    int[] colours;LinearGradient(float a,float b,float c,float d,int[] e,Object f,TileMode g){colours=e;}
    void setLocalMatrix(Matrix m){}
}
class Path {
    enum Direction {CW}int width,height;void reset(){}
    void addRoundRect(float l,float t,float r,float b,int x,int y,Direction d){width=(int)r;height=(int)b;}
}
class Paint {
    static final int ANTI_ALIAS_FLAG=1;enum Style {STROKE,FILL}int colour;Shader shader;
    Paint(int flags){}void setColor(int c){colour=c;}void setAlpha(int a){assert a>=0&&a<=255;colour=(colour&0xffffff)|(a<<24);}
    void setStyle(Style s){}void setStrokeWidth(int w){}void setShader(Shader s){shader=s;}
}
class Canvas {
    boolean clipped,textLast;int glow,particle,circles,lines,sheens;Path path;String positions="";
    int save(){return 1;}void clipPath(Path p){path=p;assert p.width>0&&p.height>0;clipped=true;}
    void restoreToCount(int n){assert clipped;clipped=false;}
    void drawRoundRect(float a,float b,float c,float d,int e,int f,Paint p){assert clipped;glow=p.colour&0xffffff;}
    void drawRect(float a,float b,float c,float d,Paint p){assert clipped&&p.shader!=null;sheens++;
        LinearGradient gradient=(LinearGradient)p.shader;assert (gradient.colours[1]>>>24)==40;}
    void drawCircle(float x,float y,float r,Paint p){assert clipped&&p.shader==null&&r>=0;
        assert x>=0&&x<=path.width&&y>=0&&y<=path.height;circles++;particle=p.colour&0xffffff;positions+=x+","+y+","+r+";";}
    void drawLine(float a,float b,float c,float d,Paint p){assert clipped;lines++;}
}
class Build {static class VERSION {static int SDK_INT=26;}}
class ValueAnimator {static boolean enabled=true;static boolean areAnimatorsEnabled(){return enabled;}}
class Settings {static class Global {static final String ANIMATOR_DURATION_SCALE="scale";static float scale=1;
    static float getFloat(Object r,String key,float fallback){return scale;}}}
class SystemClock {static long now=900;static long uptimeMillis(){return now;}}
'''

DRAW_CHECKS = r'''
    static Canvas draw(TextView button){button.frames=0;Canvas c=new Canvas();button.onDraw(c);assert c.textLast;return c;}
    static void motion(TextView b,int glow,int particles){
        b.resize(240,60);Build.VERSION.SDK_INT=26;ValueAnimator.enabled=true;
        for(long time:new long[]{0,900,1800,2700,3599}){
            SystemClock.now=time;Canvas c=draw(b);assert c.circles==6&&c.lines==6&&c.sheens==1;
            assert c.glow==(glow&0xffffff)&&c.particle==(particles&0xffffff);assert b.frames==1;
        }
        ValueAnimator.enabled=false;String frozen=draw(b).positions;assert b.frames==0;
        SystemClock.now=1777;assert draw(b).positions.equals(frozen)&&b.frames==0;
        Build.VERSION.SDK_INT=25;ValueAnimator.enabled=true;Settings.Global.scale=0;
        assert draw(b).positions.equals(frozen)&&b.frames==0;
        Settings.Global.scale=1;draw(b);assert b.frames==1;
        b.enabled=false;assert draw(b).circles==0&&b.frames==0;b.enabled=true;
        b.onWindowFocusChanged(false);draw(b);assert b.frames==0;
        b.onWindowFocusChanged(true);draw(b);assert b.frames==1;
        b.onWindowVisibilityChanged(8);draw(b);assert b.frames==0;
        b.onWindowVisibilityChanged(0);b.shown=false;draw(b);assert b.frames==0;
        b.shown=true;b.resize(48,32);assert draw(b).circles==6&&b.frames==1;
    }
'''


def run(folder, body):
    folder.mkdir(parents=True, exist_ok=True)
    source = folder / "ButtonEffectCheck.java"
    source.write_text(STUBS + body, encoding="utf-8")
    java = Path(os.environ["JAVA_HOME"]) / "bin"
    subprocess.run([str(java / "javac.exe"), "--release", "8", "-Xlint:-options", "-encoding", "UTF-8", "-d", str(folder), str(source)], check=True)
    subprocess.run([str(java / "java.exe"), "-ea", "-cp", str(folder), "ButtonEffectCheck"], check=True)


def check(root, folder):
    source = (root / "android/src/port/launcher/Ui.java").read_text(encoding="utf-8")
    signatures = ["private static int colour(", "static int parse(",
                  "TextView button(String value, int kind, Runnable action)",
                  "TextView button(String value, int kind, JSONObject theme, Runnable action)",
                  "void attention(", "private final class ActionButton"]
    # The one-line helper has no separate closing line.
    methods = next(line for line in source.splitlines() if line.startswith("    private static int colour(")) + "\n"
    methods += "\n".join(extract(source, s) for s in signatures[1:])
    methods = methods.replace("private final class ActionButton", "final class ActionButton")
    body = r'''
class Ui {
    final Context activity=new Context();final Object bold=new Object();
    static final int PRIMARY=0,SECONDARY=1,QUIET=2;
    final int accent=0xffe8742a,accentDark=0xff9a4a16,primaryFill=0xff3a2414,text=0xfff2f4f7,muted=0xffa7afbc,cell=0xff262a33;
    final int playGlow=0xffe0a9d4,playParticles=0xfffadff6;
    int dp(float v){return Math.round(v);}
''' + methods + r'''
}
public class ButtonEffectCheck {
''' + DRAW_CHECKS + r'''
    public static void main(String[] args){
        Ui ui=new Ui();TextView inherited=ui.button("Play",Ui.PRIMARY,()->{});
        ui.attention(inherited,true);assert inherited.textColour==ui.text;motion(inherited,ui.playGlow,ui.playParticles);
        JSONObject winter=new JSONObject().putColour("primaryFill","#163044").putColour("accentDark","#6AADD0")
            .putColour("playGlow","#A7E2F4").putColour("playParticles","#F1FCFF");
        TextView hof=ui.button("HoF",Ui.PRIMARY,winter,()->{});ui.attention(hof,true);
        GradientDrawable background=((RippleDrawable)hof.background).content;
        assert background.fill==0xff163044&&background.stroke==0xff6aadd0;
        motion(hof,0xffa7e2f4,0xfff1fcff);motion(inherited,ui.playGlow,ui.playParticles);
        TextView partial=ui.button("Partial",Ui.PRIMARY,new JSONObject().putColour("playGlow","#123456"),()->{});
        ui.attention(partial,true);motion(partial,0xff123456,ui.playParticles);
        assert ((RippleDrawable)partial.background).content.fill==ui.primaryFill;
        assert Ui.parse("invalid",37)==37&&Ui.parse("",41)==41;
        ui.attention(partial,false);assert draw(partial).circles==0&&partial.frames==1;
        TextView quiet=ui.button("Other",Ui.SECONDARY,()->{});assert draw(quiet).circles==0&&quiet.frames==0;
        System.out.println("PASS Play button: per-edition colours, inheritance, clipping, particles, reduced motion and visibility");
    }
}
'''
    run(folder / "play-button", body)
    module = importlib.util.spec_from_file_location("play_button_launcher", root / "tools/launcher.py")
    launcher = importlib.util.module_from_spec(module)
    module.loader.exec_module(launcher)
    spec = json.loads((root / "examples/homm5.port.json").read_text(encoding="utf-8"))
    spec["theme"] = {"playGlow": "#A7E2F4", "playParticles": "#F1FCFF"}
    spec["distributions"][0]["theme"] = {"primaryFill": "#163044", "accentDark": "#6aADD0"}
    before = json.dumps(spec, sort_keys=True)
    launcher.validate(spec)
    assert json.dumps(spec, sort_keys=True) == before
    for scope, key in ((spec, "playGlow"), (spec, "playParticles"), (spec["distributions"][0], "primaryFill"),
                       (spec["distributions"][0], "accentDark"), (spec["distributions"][0], "playGlow"),
                       (spec["distributions"][0], "playParticles")):
        saved = scope["theme"]
        for bad in (None, [], "#123456", {key: "#123"}, {key: "#80123456"}, {key: 123456}, {key: "red"}):
            scope["theme"] = bad
            try:
                launcher.validate(spec)
            except launcher.SpecError:
                pass
            else:
                raise AssertionError(f"invalid theme accepted: {bad!r}")
        scope["theme"] = saved
    return True


def check_legacy(project, folder):
    """The same motion checks for the two independent launcher implementations."""
    stronghold = project.name == "stronghold_crusader"
    relative = "org/stronghold/port/Theme.java" if stronghold else "org/thevoid/port/Launcher.java"
    source = (project / "android/app/src" / relative).read_text(encoding="utf-8")
    colours = extract(source, "static int[] playColours(")
    button = extract(source, "private static final class PlayButton" if stronghold else "private final class PlayButton")
    button = button.replace("private static final class", "static final class").replace("private final class", "final class")
    button = button.replace("android.provider.Settings", "Settings")
    owner = "class Theme {static int dp(Context c,float v){return Math.round(v);}" if stronghold else "class Launcher extends Context {int dp(float v){return Math.round(v);}"
    tests = r'''
        for(String edition:new String[]{"classic","extreme","stronghold"}){
            int[] c=Theme.playColours(edition);Theme.PlayButton b=new Theme.PlayButton(new Context(),edition);
            assert b.fill==c[0]&&b.stroke==c[1];motion(b,c[2],c[3]);
        }
        assert Theme.playColours("classic")[0]!=Theme.playColours("extreme")[0];
        assert Theme.playColours("classic")[0]!=Theme.playColours("stronghold")[0];
''' if stronghold else r'''
        Launcher owner=new Launcher();Launcher.PlayButton b=owner.new PlayButton();
        for(String library:new String[]{"void","void_tension","void"}){
            b.style(library);int[] c=Launcher.playColours(library);
            GradientDrawable bg=(GradientDrawable)b.background;assert bg.fill==c[0]&&bg.stroke==c[1];
            int before=b.invalidations;b.style(library);assert before==b.invalidations;
            motion(b,c[2],c[3]);
        }
        assert Launcher.playColours("void")[0]!=Launcher.playColours("void_tension")[0];
'''
    body = owner + colours + button + "}\npublic class ButtonEffectCheck {\n" + DRAW_CHECKS
    body += "public static void main(String[] args){" + tests + '\nSystem.out.println("PASS edition palettes and motion");}}'
    run(folder / project.name, body)
    return True


if __name__ == "__main__":
    root = Path(sys.argv[1]).resolve() if len(sys.argv) > 1 else Path(__file__).resolve().parents[1]
    with tempfile.TemporaryDirectory(prefix="play-button-") as temporary:
        if root.name in ("stronghold_crusader", "the_void"):
            check_legacy(root, Path(temporary))
        else:
            check(root, Path(temporary))
