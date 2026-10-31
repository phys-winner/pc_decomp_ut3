"""Exercise the production pointer state machine without Android or JNI."""
import os
from pathlib import Path
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]


class TouchControlsTest(unittest.TestCase):
    @staticmethod
    def _method(source, signature):
        begin = source.index(signature)
        end = source.index("{", begin) + 1
        depth = 1
        while depth:
            depth += (source[end] == "{") - (source[end] == "}")
            end += 1
        return source[begin:end]

    def test_pie_captions_fit_each_sector_without_squashing(self):
        java = Path(os.environ.get("JAVA_HOME", "C:/Portable/jdk-24.0.1")) / "bin"
        source = (ROOT / "android/app/src/org/ut3/port/GameControls.java").read_text(encoding="utf-8")
        methods = "\n".join(self._method(source, name) for name in ("private void drawText", "private String pieLabel",
            "private int[] pieItems(int kind)", "private float pieRadius", "private float pieCaptionWidth",
            "private float pieTitleWidth", "private float pieRing"))
        program = r'''
package org.ut3.port;
class Paint {
    enum Align {CENTER} enum Style {FILL}
    static class FontMetrics {float ascent,descent;}
    float size;void setStyle(Style style){}void setColor(int color){}
    void setTextAlign(Align align){assert align==Align.CENTER;}
    void setTextSize(float value){assert Float.isFinite(value)&&value>0;size=value;}
    float measureText(String label){
        // Wider than Roboto's averages, so the production fit is checked conservatively.
        float width=0;for(int i=0;i<label.length();++i)width+=label.charAt(i)>127?.66f:.62f;
        return size*width;
    }
    FontMetrics getFontMetrics(){FontMetrics f=new FontMetrics();f.ascent=-.8f*size;f.descent=.25f*size;return f;}
}
class Canvas {
    int draws;float x,y,width,top,bottom,size;
    void drawText(String label,float a,float b,Paint p){
        ++draws;x=a;y=b;width=p.measureText(label);Paint.FontMetrics m=p.getFontMetrics();
        top=b+m.ascent;bottom=b+m.descent;size=p.size;
    }
}
public class PieFitCheck {
    static final int SETTINGS=-3,PIE_ACTIONS=0,PIE_SEATS=1,SEAT_COUNT=5;
    final Paint paint=new Paint();float dp=1,layoutScale=1,w=800,h=360,pieR;int profile;boolean russian;
    float width(){return w;}float height(){return h;}String text(String en,String ru){return russian?ru:en;}
''' + methods + r'''
    public static void main(String[] args){
        PieFitCheck view=new PieFitCheck();Canvas c=new Canvas();int cases=0;
        for(float density:new float[]{1,2.75f,3,3.5f})for(float[] screen:new float[][]{{2400,1080},{1920,1080},{1280,720},{2560,1600},{3200,1440}})
        for(float scale:new float[]{.65f,1,1.6f})for(boolean russian:new boolean[]{false,true})
        for(int kind:new int[]{PIE_ACTIONS,PIE_SEATS})for(int profile=0;profile<5;++profile){
            if(screen[1]/density<320)continue; // No Android device is shorter than 320dp in landscape.
            view.dp=density;view.w=screen[0];view.h=screen[1];view.layoutScale=scale;view.russian=russian;view.profile=profile;
            int[] items=view.pieItems(kind);int n=items.length;
            view.pieR=view.pieRadius(n);
            assert n>=4&&n<=7;
            float r=view.pieR,inner=r*.36f,ring=view.pieRing(),caption=view.pieCaptionWidth(n);
            assert r<=Math.min(view.w,view.h)*.45f+.001f&&caption>0;
            for(int i=0;i<n;++i){
                String label=view.pieLabel(items[i],false);assert !label.isEmpty();
                double a=Math.PI*2*i/n;
                float ix=(float)Math.sin(a)*ring,iy=-(float)Math.cos(a)*ring;
                view.drawText(c,label,ix,iy+14*density,11*density,caption,0);
                assert c.size<=11*density+.001f&&c.size>=9*density-.001f&&c.width<=caption+.01f:label+" "+c.size/density;
                assert Math.abs((c.top+c.bottom)*.5f-(iy+14*density))<.001f;
                // Every corner of the caption box stays inside its own sector of the ring.
                for(float x:new float[]{ix-c.width*.5f,ix+c.width*.5f})for(float y:new float[]{c.top,c.bottom}){
                    double angle=Math.atan2(x,-y),offset=Math.abs(Math.IEEEremainder(angle-a,Math.PI*2));
                    assert offset<Math.PI/n:label+" leaves its sector";
                    assert Math.hypot(x,y)<r&&Math.hypot(x,y)>inner:label;
                }
                // The full name stays readable in its pill beside the pie.
                String full=view.pieLabel(items[i],true);
                view.drawText(c,full,0,0,14*density,view.pieTitleWidth(),0);
                assert c.size>=11.5f*density&&c.width<=view.pieTitleWidth()+.01f:full;
                ++cases;
            }
        }
        int draws=c.draws;view.drawText(c,"",0,0,10,10,0);view.drawText(c,"x",0,0,10,0,0);assert c.draws==draws;
        System.out.println("PASS "+cases+" production pie caption cases: EN/RU, foot/vehicle/board/turret actions and seats, 65..160 percent, phone/tablet densities; captions fit their sector and ring");
    }
}
'''
        with tempfile.TemporaryDirectory() as temporary:
            path = Path(temporary) / "PieFitCheck.java"
            path.write_text(program, encoding="utf-8")
            subprocess.run([java / "javac.exe", "--release", "8", "-encoding", "UTF-8", "-d", temporary,
                            ROOT / "android/app/src/org/ut3/port/TouchInput.java",
                            ROOT / "android/app/src/org/ut3/port/TouchLayout.java", path], check=True)
            subprocess.run([java / "java.exe", "-ea", "-cp", temporary, "org.ut3.port.PieFitCheck"], check=True)

    def test_closed_ime_retires_served_connection_before_hiding(self):
        java = Path(os.environ.get("JAVA_HOME", "C:/Portable/jdk-24.0.1")) / "bin"
        source = (ROOT / "android/app/src/org/ut3/port/GameControls.java").read_text(encoding="utf-8")
        def method(signature):
            begin = source.index(signature)
            end = source.index("{", begin) + 1
            depth = 1
            while depth:
                depth += (source[end] == "{") - (source[end] == "}")
                end += 1
            return source[begin:end]
        program = r'''
class Context {
    static final String INPUT_METHOD_SERVICE="input_method";
    InputMethodManager manager;
    Object getSystemService(String name){assert name.equals(INPUT_METHOD_SERVICE);return manager;}
}
class InputMethodManager {
    int restarts,hides;ImeRetireCheck served;
    void restartInput(ImeRetireCheck view){
        assert !view.imeRequested&&!view.imeWasVisible&&!view.onCheckIsTextEditor();
        ++restarts;served=view;view.hideKeyboard(); // Re-entry must be neutral after editor retirement.
    }
    void hideSoftInputFromWindow(Object token,int flags){assert restarts==hides+1;++hides;}
}
public class ImeRetireCheck {
    boolean imeRequested,imeWasVisible,authoritative=true;long cursorContext=7,imeSession=10;
    final Context context=new Context();Context getContext(){return context;}Object getWindowToken(){return this;}
''' + method("void hideKeyboard") + method("@Override public boolean onCheckIsTextEditor").replace("@Override ", "") + r'''
    public static void main(String[] args){
        ImeRetireCheck view=new ImeRetireCheck();InputMethodManager imm=new InputMethodManager();view.context.manager=imm;
        view.imeRequested=true;assert view.onCheckIsTextEditor();view.hideKeyboard();
        assert view.imeSession==11&&imm.restarts==1&&imm.hides==1&&!view.onCheckIsTextEditor();
        view.hideKeyboard();assert view.imeSession==11&&imm.restarts==1&&imm.hides==1;
        view.imeWasVisible=true;view.hideKeyboard();assert view.imeSession==12&&imm.restarts==2&&imm.hides==2;
        view.imeRequested=true;view.context.manager=null;view.hideKeyboard();assert view.imeSession==13&&!view.onCheckIsTextEditor();
        System.out.println("PASS production hideKeyboard: editor false/session retired before IMM restart then hide; recursive and repeated close neutral; null IMM safe");
    }
}
'''
        with tempfile.TemporaryDirectory() as temporary:
            path = Path(temporary) / "ImeRetireCheck.java"
            path.write_text(program, encoding="utf-8")
            subprocess.run([java / "javac.exe", "--release", "8", "-d", temporary, path], check=True)
            subprocess.run([java / "java.exe", "-ea", "-cp", temporary, "ImeRetireCheck"], check=True)

    def test_ui_visibility_and_ime_sessions(self):
        java = Path(os.environ.get("JAVA_HOME", "C:/Portable/jdk-24.0.1")) / "bin"
        source = (ROOT / "android/app/src/org/ut3/port/GameControls.java").read_text(encoding="utf-8")
        methods = "\n".join(self._method(source, name) for name in ("private int action", "private boolean visible",
            "private boolean textUi", "private boolean ready", "@Override public boolean performEditorAction",
            "private void applyPendingSettings"))
        program = r'''
package org.ut3.port;
class GameActivity { static long current=7;static long nativeTouchContext(){return current;} }
class EditorBase { public boolean performEditorAction(int action){return false;} }
public class ImeGuardCheck extends EditorBase {
    static final int MORE=-1,MODE=-2,SETTINGS=-3,KEYBOARD=-4,WEAPON=-5,SEATS=-6,VEHICLE_SEAT_JUMP=4;
    boolean editing,authoritative=true,playing,imeRequested=true;int profile,vehicleFlags;
    long cursorContext=7,context=7,imeSession=2,session=1;
    int hidden,commits;
    boolean settingsPending;int menuId=-1,editId=-1,reloads;
    final TouchInput input=new TouchInput(new TouchInput.Sink(){
        public void submit(float a,float b,float c,float d,int held){}
        public boolean stick(float x,float y){return true;}
        public void reset(){}
    });
    void reloadSettings(){++reloads;settingsPending=false;}
    final TouchText text=new TouchText(new TouchText.Sink(){
        public boolean commit(String value){++commits;return true;}public boolean delete(int count){return true;}
    });
    void hideKeyboard(){++hidden;imeRequested=false;++imeSession;}
''' + methods + r'''
    boolean only(int... controls){
        for(int c=0;c<TouchLayout.COUNT;++c){
            boolean expected=false;for(int x:controls)expected|=x==c;
            if(visible(c)!=expected)return false;
        }
        return true;
    }
    public static void main(String[] args) {
        ImeGuardCheck view=new ImeGuardCheck();
        // Original UI: only the keyboard shortcut; the Android settings are a tab of the original settings.
        assert view.only(TouchLayout.KEYBOARD);
        view.cursorContext=5;assert view.only();
        view.cursorContext=6;view.playing=true;
        int[] foot={TouchLayout.FIRE,TouchLayout.ALT,TouchLayout.JUMP,TouchLayout.DUCK,TouchLayout.USE,
            TouchLayout.WEAPON,TouchLayout.BOARD,TouchLayout.MORE,TouchLayout.PAUSE,TouchLayout.SCORES,TouchLayout.FIRE2};
        assert view.only(foot)&&view.action(TouchLayout.WEAPON)==WEAPON&&view.action(TouchLayout.USE)==TouchInput.USE;
        view.profile=1;
        assert view.only(TouchLayout.FIRE,TouchLayout.ALT,TouchLayout.JUMP,TouchLayout.DUCK,TouchLayout.USE,
            TouchLayout.WEAPON,TouchLayout.MORE,TouchLayout.PAUSE,TouchLayout.SCORES,TouchLayout.FIRE2)&&view.action(TouchLayout.WEAPON)==SEATS;
        assert view.action(TouchLayout.FIRE2)==TouchInput.FIRE;
        view.profile=2;assert view.action(TouchLayout.JUMP)==TouchInput.JUMP&&view.action(TouchLayout.DUCK)==TouchInput.DUCK;
        view.profile=3;
        assert view.only(TouchLayout.FIRE,TouchLayout.ALT,TouchLayout.JUMP,TouchLayout.DUCK,TouchLayout.USE,
            TouchLayout.BOARD,TouchLayout.MORE,TouchLayout.PAUSE,TouchLayout.SCORES,TouchLayout.FIRE2);
        view.profile=4;
        assert view.only(TouchLayout.FIRE,TouchLayout.ALT,TouchLayout.USE,TouchLayout.WEAPON,TouchLayout.MORE,
            TouchLayout.PAUSE,TouchLayout.SCORES,TouchLayout.FIRE2);
        // A gunner seat whose vehicle accepts turret Jump (the deployed Hellfire's cannon) keeps Jump: it undeploys.
        view.vehicleFlags=VEHICLE_SEAT_JUMP;
        assert view.only(TouchLayout.FIRE,TouchLayout.ALT,TouchLayout.JUMP,TouchLayout.USE,TouchLayout.WEAPON,TouchLayout.MORE,
            TouchLayout.PAUSE,TouchLayout.SCORES,TouchLayout.FIRE2)&&view.action(TouchLayout.JUMP)==TouchInput.JUMP;
        view.vehicleFlags=0;
        // The layout editor shows every on-foot control whatever the seat.
        view.editing=true;assert view.only(foot);view.editing=false;view.profile=0;
        // Without a script publisher the manual PLAY/MENU fallback remains.
        view.authoritative=false;view.cursorContext=0;view.playing=false;
        assert view.only(TouchLayout.MODE);
        view.playing=true;
        assert view.visible(TouchLayout.MODE)&&!view.visible(TouchLayout.SETTINGS)&&view.visible(TouchLayout.FIRE)&&!view.visible(TouchLayout.KEYBOARD);
        view.cursorContext=5;assert view.only();
        view.authoritative=true;view.cursorContext=7;view.playing=false;
        assert !view.ready()&&view.imeRequested&&view.hidden==0;
        assert !view.performEditorAction(6)&&view.imeRequested&&view.hidden==0&&view.commits==0;
        view.session=2;assert view.ready()&&view.text.compose("A");
        assert view.performEditorAction(6)&&!view.imeRequested&&view.hidden==1&&view.commits==1;
        view.imeRequested=true;view.imeSession=10;view.session=9;
        assert !view.performEditorAction(6)&&view.imeRequested&&view.hidden==1;
        view.session=10;GameActivity.current=11;
        assert !view.ready()&&!view.imeRequested&&view.hidden==2;
        view.settingsPending=true;view.applyPendingSettings();
        assert view.reloads==0&&view.settingsPending;
        view.cursorContext=6;view.menuId=1;view.applyPendingSettings();assert view.reloads==0;
        view.menuId=-1;view.editId=2;view.applyPendingSettings();assert view.reloads==0;
        view.editId=-1;view.input.down(3,0,0,0,TouchInput.LOOK,-1);view.applyPendingSettings();assert view.reloads==0;
        view.input.cancel();view.imeRequested=true;view.applyPendingSettings();assert view.reloads==0;
        view.imeRequested=false;view.applyPendingSettings();assert view.reloads==1&&!view.settingsPending;
        System.out.println("PASS production visibility: original UI shows the keyboard only, no settings button, loading none, foot/ground/air/board/turret sets, editor on-foot set, legacy PLAY fallback; stale IME session/Done cannot commit or hide a new keyboard; deferred settings reload");
    }
}
'''
        with tempfile.TemporaryDirectory() as temporary:
            path = Path(temporary) / "ImeGuardCheck.java"
            path.write_text(program, encoding="utf-8")
            subprocess.run([java / "javac.exe", "--release", "8", "-d", temporary,
                            ROOT / "android/app/src/org/ut3/port/TouchText.java",
                            ROOT / "android/app/src/org/ut3/port/TouchInput.java",
                            ROOT / "android/app/src/org/ut3/port/TouchLayout.java", path], check=True)
            subprocess.run([java / "java.exe", "-ea", "-cp", temporary, "org.ut3.port.ImeGuardCheck"], check=True)

    def test_activity_back_edge_ownership(self):
        java = Path(os.environ.get("JAVA_HOME", "C:/Portable/jdk-24.0.1")) / "bin"
        def method(file, signature):
            text = (ROOT / file).read_text(encoding="utf-8")
            begin = text.index(signature)
            end = text.index("{", begin) + 1
            depth = 1
            while depth:
                depth += (text[end] == "{") - (text[end] == "}")
                end += 1
            return text[begin:end]
        dispatch = method("android/app/src/org/ut3/port/GameActivity.java", "@Override public boolean dispatchKeyEvent")
        preime = method("android/app/src/org/ut3/port/GameControls.java", "@Override public boolean onKeyPreIme")
        program = r'''
class KeyEvent {
    static final int ACTION_DOWN=0,ACTION_UP=1,KEYCODE_BACK=4,KEYCODE_ESCAPE=111,KEYCODE_A=29,KEYCODE_Z=54,
        KEYCODE_0=7,KEYCODE_9=16,KEYCODE_F1=131,KEYCODE_F12=142,KEYCODE_SPACE=62,KEYCODE_ENTER=66,
        KEYCODE_GRAVE=68,KEYCODE_MINUS=69,KEYCODE_PERIOD=56,KEYCODE_DEL=67,KEYCODE_DPAD_UP=19,KEYCODE_DPAD_DOWN=20,
        KEYCODE_DPAD_LEFT=21,KEYCODE_DPAD_RIGHT=22,KEYCODE_TAB=61,KEYCODE_BUTTON_A=96;
    final int action,key;final long downTime;
    KeyEvent(int action,int key,long downTime){this.action=action;this.key=key;this.downTime=downTime;}
    int getAction(){return action;}int getKeyCode(){return key;}long getDownTime(){return downTime;}
}
class ActivityBase { public boolean dispatchKeyEvent(KeyEvent event){return false;} }
class PreImeBase { public boolean onKeyPreIme(int code,KeyEvent event){return false;} }
class Controls extends PreImeBase {
    boolean overlay,imeRequested;int closes,backCalls,hidden;
    void hideKeyboard(){imeRequested=false;++hidden;}
    boolean handleBack(){++backCalls;if(imeRequested){hideKeyboard();return true;}if(overlay){overlay=false;++closes;return true;}return false;}
    int pads;boolean gamepadKey(KeyEvent event){if(event.getKeyCode()!=KeyEvent.KEYCODE_BUTTON_A)return false;++pads;return true;}
''' + preime + r'''
}
public class ActivityBackCheck extends ActivityBase {
    Controls controls=new Controls();long backDownTime=-1;boolean backConsumed,started=true;int launcher;
    void back(){++launcher;}
    final java.util.ArrayList<Integer> nativeEdges=new java.util.ArrayList<>();
    void nativeKey(int vk,boolean down){nativeEdges.add(down?vk:-vk);}
''' + dispatch + r'''
    boolean deliver(int action,int key,long time) {
        KeyEvent event=new KeyEvent(action,key,time);
        return controls.onKeyPreIme(key,event)||dispatchKeyEvent(event);
    }
    public static void main(String[] args) {
        ActivityBackCheck view=new ActivityBackCheck();view.controls.overlay=true;
        assert view.deliver(0,4,100)&&view.controls.closes==1&&view.nativeEdges.isEmpty();
        assert view.deliver(0,4,100)&&view.controls.closes==1&&view.controls.backCalls==1;
        assert view.deliver(1,4,100)&&view.nativeEdges.isEmpty();
        assert view.deliver(0,4,200)&&view.deliver(1,4,200);
        assert view.nativeEdges.equals(java.util.Arrays.asList(27,-27));
        view.nativeEdges.clear();view.controls.imeRequested=true;
        assert view.deliver(0,4,300)&&view.controls.imeRequested;
        assert view.deliver(1,4,300)&&!view.controls.imeRequested&&view.controls.hidden==1&&view.nativeEdges.isEmpty();
        view.controls.overlay=true;
        assert view.deliver(0,4,400)&&view.deliver(1,4,400)&&view.controls.closes==2&&view.nativeEdges.isEmpty();
        assert view.dispatchKeyEvent(new KeyEvent(0,111,500))&&view.dispatchKeyEvent(new KeyEvent(1,111,500));
        assert view.nativeEdges.equals(java.util.Arrays.asList(27,-27));
        view.nativeEdges.clear();assert view.dispatchKeyEvent(new KeyEvent(1,4,600))&&view.nativeEdges.isEmpty();
        // Keyboard arrows and Tab reach the original UI; a gamepad button goes to the touch layer.
        assert view.dispatchKeyEvent(new KeyEvent(0,19,700))&&view.dispatchKeyEvent(new KeyEvent(1,19,700))&&view.dispatchKeyEvent(new KeyEvent(0,61,701));
        assert view.nativeEdges.equals(java.util.Arrays.asList(0x26,-0x26,9));
        view.nativeEdges.clear();assert view.dispatchKeyEvent(new KeyEvent(0,96,702))&&view.controls.pads==1&&view.nativeEdges.isEmpty();
        // Before the game started, Back (UP) leads to the launcher once and reaches no game.
        view.started=false;assert view.dispatchKeyEvent(new KeyEvent(0,4,800))&&view.launcher==0;
        assert view.dispatchKeyEvent(new KeyEvent(1,4,800))&&view.launcher==1&&view.nativeEdges.isEmpty();
        assert !view.dispatchKeyEvent(new KeyEvent(0,96,801))&&view.controls.pads==1;
        System.out.println("PASS production Activity/View Back routing: overlay DOWN owns repeats/UP, closes once; original Escape pair preserved without overlay; IME pre-IME consumes both edges; orphan UP neutral; arrows/Tab; gamepad to the layer; back before the start opens the launcher");
    }
}
'''
        with tempfile.TemporaryDirectory() as temporary:
            source = Path(temporary) / "ActivityBackCheck.java"
            source.write_text(program, encoding="utf-8")
            subprocess.run([java / "javac.exe", "--release", "8", "-d", temporary, source], check=True)
            subprocess.run([java / "java.exe", "-ea", "-cp", temporary, "ActivityBackCheck"], check=True)

    def test_keyboard_back_consumes_both_edges(self):
        java = Path(os.environ.get("JAVA_HOME", "C:/Portable/jdk-24.0.1")) / "bin"
        source = (ROOT / "android/app/src/org/ut3/port/GameControls.java").read_text(encoding="utf-8")
        begin = source.index("@Override public boolean onKeyPreIme")
        end = source.index("{", begin) + 1
        depth = 1
        while depth:
            depth += (source[end] == "{") - (source[end] == "}")
            end += 1
        program = r'''
class KeyEvent {
    static final int KEYCODE_BACK=4,ACTION_DOWN=0,ACTION_UP=1;
    final int action;KeyEvent(int action){this.action=action;}int getAction(){return action;}
}
class PreImeBase { public boolean onKeyPreIme(int code,KeyEvent event){return false;} }
public class PreImeCheck extends PreImeBase {
    boolean imeRequested=true;int hidden;
    void hideKeyboard(){imeRequested=false;++hidden;}
''' + source[begin:end] + r'''
    public static void main(String[] args) {
        PreImeCheck view=new PreImeCheck();
        assert view.onKeyPreIme(4,new KeyEvent(0))&&view.imeRequested&&view.hidden==0;
        assert view.onKeyPreIme(4,new KeyEvent(1))&&!view.imeRequested&&view.hidden==1;
        assert !view.onKeyPreIme(4,new KeyEvent(0));
        view.imeRequested=true;assert !view.onKeyPreIme(99,new KeyEvent(0))&&view.imeRequested;
        System.out.println("PASS production IME Back consumes DOWN/UP, hides once, and cannot fall through to the original game menu");
    }
}
'''
        with tempfile.TemporaryDirectory() as temporary:
            source = Path(temporary) / "PreImeCheck.java"
            source.write_text(program, encoding="utf-8")
            subprocess.run([java / "javac.exe", "--release", "8", "-d", temporary, source], check=True)
            subprocess.run([java / "java.exe", "-ea", "-cp", temporary, "PreImeCheck"], check=True)

    def test_text_composition(self):
        java = Path(os.environ.get("JAVA_HOME", "C:/Portable/jdk-24.0.1")) / "bin"
        program = r'''
package org.ut3.port;
public class TextCheck {
    static class Sink implements TouchText.Sink {
        String committed="";int deleted,calls;boolean accept=true;
        public boolean commit(String text){++calls;if(!accept)return false;committed+=text;return true;}
        public boolean delete(int count){if(!accept)return false;deleted+=count;return true;}
    }
    public static void main(String[] args) {
        Sink sink=new Sink();TouchText text=new TouchText(sink);
        assert text.compose("\u0416\uD83D\uDE00")&&sink.calls==0;
        assert text.backspace()&&text.before(256).toString().equals("\u0416")&&sink.deleted==0;
        assert text.commit("\u0416")&&sink.calls==1&&text.finish()&&sink.calls==1;
        assert text.compose("ab")&&text.compose("abc")&&text.finish()&&sink.committed.equals("\u0416abc");
        assert text.compose("xy")&&text.delete(4,0)&&sink.deleted==4&&text.before(256).toString().equals("xy");
        assert text.compose("xy");sink.accept=false;assert !text.delete(3,0)&&text.before(256).toString().equals("xy");
        assert !text.commit("z")&&text.before(256).toString().equals("xy");sink.accept=true;
        assert text.finish();
        Sink other=new Sink();TouchText codepoints=new TouchText(other);
        assert codepoints.deleteCodePoints(1,0)&&other.deleted==1; // New IME session can delete an existing game field.
        assert !codepoints.deleteCodePoints(2,0)&&other.deleted==1;
        assert codepoints.commit("A\uD83D\uDE00B")&&!codepoints.deleteCodePoints(2,0)&&other.deleted==1;
        assert codepoints.deleteCodePoints(1,0)&&other.deleted==2;
        assert !codepoints.deleteCodePoints(2,0)&&other.deleted==2;
        assert codepoints.deleteCodePoints(1,0)&&other.deleted==3; // Delegate one original key, never infer surrogate units.
        assert !codepoints.deleteCodePoints(2,0);
        assert codepoints.backspace()&&other.deleted==4;
        assert codepoints.commit("XYZ")&&codepoints.compose("pq")&&codepoints.deleteCodePoints(2,0)&&other.deleted==6;
        assert codepoints.before(256).toString().equals("pq")&&codepoints.backspace()&&other.deleted==6;
        assert codepoints.delete(1,0)&&other.deleted==7&&codepoints.before(256).toString().equals("p");
        assert !codepoints.deleteCodePoints(2,0)&&!codepoints.deleteCodePoints(0,1);
        assert !text.delete(-1,0)&&!text.delete(257,0)&&!text.delete(1,1);
        for(int i=0;i<160;i++)if(i<32||i>=127)assert !TouchText.valid(Character.toString((char)i));
        assert !TouchText.valid("\uD800")&&!TouchText.valid("\uDC00")&&!TouchText.valid("\uD800x");
        assert !TouchText.valid("\u2028")&&!TouchText.valid("\u2029")&&!TouchText.valid(null);
        assert TouchText.valid("\u0416\uD83D\uDE00");
        char[] max=new char[257];java.util.Arrays.fill(max,'x');assert !TouchText.valid(new String(max));
        assert TouchText.valid(new String(max,0,256));
        System.out.println("PASS IME local composition/backspace, outside-composition deletion, one original Backspace for a new/unknown session, bounded BMP multi-delete, no surrogate-count guesses, rejected-sink atomicity, UTF16/control/size bounds");
    }
}
'''
        with tempfile.TemporaryDirectory() as temporary:
            source = Path(temporary) / "TextCheck.java"
            source.write_text(program, encoding="utf-8")
            subprocess.run([java / "javac.exe", "--release", "8", "-d", temporary,
                            ROOT / "android/app/src/org/ut3/port/TouchText.java", source], check=True)
            subprocess.run([java / "java.exe", "-ea", "-cp", temporary, "org.ut3.port.TextCheck"], check=True)

    def test_layout_geometry_and_sensor_bounds(self):
        java = Path(os.environ.get("JAVA_HOME", "C:/Portable/jdk-24.0.1")) / "bin"
        source = (ROOT / "android/app/src/org/ut3/port/GameControls.java").read_text(encoding="utf-8")
        methods = "\n".join(self._method(source, name) for name in ("private void buildLayout", "private int action",
            "private boolean visible", "private float width", "private float height", "static float gyroDelta",
            "private static float clamp"))
        program = r'''
package org.ut3.port;
import java.util.ArrayList;
public class GeometryCheck {
    static final int MORE=-1,MODE=-2,SETTINGS=-3,KEYBOARD=-4,WEAPON=-5,SEATS=-6,VEHICLE_SEAT_JUMP=4,HUD_MAP=1<<15;
    int vehicleFlags,weaponSlots=HUD_MAP; // The minimap shown: the strictest HUD.
    static class Insets {int left,top,right,bottom;} final Insets safe=new Insets();
    int w=2400,h=1080;int getWidth(){return w;}int getHeight(){return h;}
    final float[] controlX=new float[TouchLayout.COUNT],controlY=new float[TouchLayout.COUNT],controlR=new float[TouchLayout.COUNT],
        customX=new float[TouchLayout.COUNT],customY=new float[TouchLayout.COUNT];
    final boolean[] shown=new boolean[TouchLayout.COUNT];
    final ArrayList<float[]> occupied=new ArrayList<>(),reserved=new ArrayList<>();
    float dp=3,scale=1,layoutScale=1,uiScale=1;
    boolean playing=true,editing,leftHanded,authoritative=true,layoutDirty,wasd,dpadShown;int profile;long cursorContext=6;
    float dpadX,dpadY,dpadCell;
    void buildTools(){}
''' + methods + r'''
    int crowded,noCross;
    void check(String name){
        java.util.Arrays.fill(customX,Float.NaN);java.util.Arrays.fill(customY,Float.NaN);
        buildLayout();
        float left=safe.left,top=safe.top,right=w-safe.right,bottom=h-safe.bottom;
        for(int c=0;c<TouchLayout.COUNT;++c){
            assert shown[c]==visible(c):name+" control "+c+" not placed";
            if(!shown[c])continue;
            float x=controlX[c],y=controlY[c],r=controlR[c];
            assert x-r>=left-.01f&&x+r<=right+.01f&&y-r>=top-.01f&&y+r<=bottom+.01f:name+" control "+c+" outside";
            assert 2*r>=TouchLayout.MIN_DP*dp-.01f:name+" control "+c+" too small";
            for(int d=c+1;d<TouchLayout.COUNT;++d)if(shown[d])
                assert Math.hypot(x-controlX[d],y-controlY[d])>=r+controlR[d]+4*dp-.01f:name+" controls "+c+"/"+d+" touch";
            for(float[] hud:reserved)
                assert !TouchLayout.overlaps(new float[]{x-r,y-r,x+r,y+r},hud,4*dp-.01f):name+" control "+c+" covers the HUD";
            if(dpadShown) {
                float[] cross={dpadX-1.5f*dpadCell,dpadY-1.5f*dpadCell,dpadX+1.5f*dpadCell,dpadY+1.5f*dpadCell};
                assert !TouchLayout.overlaps(new float[]{x-r,y-r,x+r,y+r},cross,4*dp-.01f):name+" control "+c+" covers the W/A/S/D cross";
            }
        }
        assert !dpadShown||(wasd&&playing&&!editing):name+" W/A/S/D cross";
        if(wasd&&playing&&!editing&&!dpadShown)++noCross;
        if(dpadShown) {
            float[] cross={dpadX-1.5f*dpadCell,dpadY-1.5f*dpadCell,dpadX+1.5f*dpadCell,dpadY+1.5f*dpadCell};
            assert cross[0]>=left-.01f&&cross[2]<=right+.01f&&cross[1]>=top-.01f&&cross[3]<=bottom+.01f:name+" cross outside";
            assert dpadCell>=TouchLayout.MIN_DP*dp-.01f:name+" cross cells too small";
            assert leftHanded?dpadX>w*.5f:dpadX<w*.5f:name+" cross on the wrong side";
            for(float[] hud:reserved)assert !TouchLayout.overlaps(cross,hud,4*dp-.01f):name+" cross covers the HUD";
        }
        // The combat cluster moves as one rigid group by a bounded shift and stays on its thumb's side.
        float shiftX=Float.NaN,shiftY=Float.NaN;boolean rigid=true;
        for(int c:TouchLayout.CLUSTER)if(shown[c]){
            float[] at=TouchLayout.anchor(c,left,top,right,bottom,dp,layoutScale,leftHanded);
            float ox=controlX[c]-at[0],oy=controlY[c]-at[1];
            if(Float.isNaN(shiftX)){shiftX=ox;shiftY=oy;}
            else rigid&=Math.abs(ox-shiftX)<.01f&&Math.abs(oy-shiftY)<.01f;
            assert leftHanded?controlX[c]<w*.5f:controlX[c]>w*.5f:name+" control "+c+" on the wrong side";
        }
        if(Float.isNaN(shiftX)) {
            // No cluster outside gameplay.
        } else if(rigid) {
            assert Math.abs(shiftX)<=TouchLayout.MAX_SHIFT_X*(right-left)+.5f&&Math.abs(shiftY)<=TouchLayout.MAX_SHIFT_Y*(bottom-top)+.5f:name;
        } else {
            ++crowded;
            // Only an enlarged control set under an enlarged HUD may give up the shape.
            assert scale>1&&uiScale>1.25f:name+" cluster lost its shape";
        }
    }
    public static void main(String[] args) {
        GeometryCheck g=new GeometryCheck();int cases=0;
        float[][] screens={{2400,1080,3},{2400,1080,2.75f},{1920,1080,3},{1280,720,2},{2560,1600,2},{3200,1440,3.5f},
            {2340,1080,2.625f,80},{1600,720,2},{2778,1284,3}};
        for(float[] s:screens)for(int profile=0;profile<5;++profile)for(float scale:new float[]{.65f,1,1.3f,1.6f})
        for(float ui:new float[]{1,1.25f,1.5f})for(boolean left:new boolean[]{false,true})for(boolean wasd:new boolean[]{false,true}){
            g.w=(int)s[0];g.h=(int)s[1];g.dp=s[2];g.safe.left=s.length>3?(int)s[3]:0;
            g.profile=profile;g.scale=scale;g.uiScale=ui;g.leftHanded=left;g.playing=true;g.cursorContext=6;g.wasd=wasd;
            g.check(s[0]+"x"+s[1]+"@"+s[2]+" profile "+profile+" scale "+scale+" ui "+ui+(left?" left":"")+(wasd?" wasd":""));++cases;
        }
        // The reference phones show the cross at 100 and 150 percent HUD, either hand.
        for(boolean left:new boolean[]{false,true})for(float ui:new float[]{1,1.5f}){
            g.w=2400;g.h=1080;g.dp=3;g.safe.left=0;g.profile=0;g.scale=1;g.uiScale=ui;g.leftHanded=left;g.wasd=true;
            g.check("reference wasd");assert g.dpadShown;
            g.w=2340;g.dp=2.625f;g.safe.left=80;g.check("cutout wasd");assert g.dpadShown;
        }
        System.out.println("W/A/S/D cross left out for lack of room: "+g.noCross+" of the crowded combinations");
        g.wasd=false;g.leftHanded=false;g.safe.left=0;
        // The reference phone keeps Fire exactly at its designed place.
        g.w=2400;g.h=1080;g.dp=3;g.safe.left=0;g.profile=0;g.scale=1;g.uiScale=1;g.leftHanded=false;g.check("reference");
        assert Math.abs(g.controlX[TouchLayout.FIRE]-(2400-140*3))<.01f&&Math.abs(g.controlY[TouchLayout.FIRE]-(1080-142*3))<.01f;
        // The enlarged right cluster: Fire 92dp, its neighbours at least 62dp; the left Fire above the stick.
        assert Math.abs(g.controlR[TouchLayout.FIRE]-46*3)<.01f&&g.controlR[TouchLayout.DUCK]>=31*3-.01f;
        assert g.shown[TouchLayout.FIRE2]&&g.controlX[TouchLayout.FIRE2]<1200&&g.controlY[TouchLayout.FIRE2]<.7f*1080;
        // The original weapon bar (UTHUD defaults), five cards with one selected, at 100 and 150 percent HUD.
        float[] bar=TouchLayout.weaponBar(0,0,2400,1080,1,5);
        assert Math.abs(bar[4]-75f/1280*2400)<.01f&&Math.abs(bar[2]-bar[0]-5.5f*bar[4])<.01f;
        assert Math.abs(bar[3]-(1080-16f/1280*2400))<.01f&&Math.abs((bar[0]+bar[2])*.5f-(1200+70f/1280*2400))<.01f;
        float[] big=TouchLayout.weaponBar(0,0,2400,1080,1.5f,5);
        assert Math.abs(big[4]-1.5f*bar[4])<.01f&&Math.abs((big[0]+big[2])*.5f-(1200+1.5f*70f/1280*2400))<.01f;
        assert TouchLayout.weaponBar(0,0,2400,1080,1,1)==null&&TouchLayout.weaponBar(0,0,0,1080,1,5)==null;
        int[] slots=TouchLayout.weaponSlots(1|2|16|32|64|(5<<10)|(1<<14));
        assert slots.length==5&&slots[0]==0&&slots[2]==4&&slots[4]==6;
        // A swipe starts on the selected card and moves one card per step, clamped to the ends.
        assert TouchLayout.weaponIndex(3,0,100,5)==3&&TouchLayout.weaponIndex(3,149,100,5)==4&&TouchLayout.weaponIndex(3,-151,100,5)==1;
        assert TouchLayout.weaponIndex(3,900,100,5)==4&&TouchLayout.weaponIndex(3,-900,100,5)==0&&TouchLayout.weaponIndex(3,Float.NaN,100,5)==3;
        // At the test phone's 137 percent HUD the enlarged arc keeps its designed place with and
        // without the vehicle minimap; without a drawn map its corner is not reserved at all.
        g.weaponSlots=0;g.uiScale=1.37f;g.check("no minimap");assert g.reserved.size()==3;
        assert Math.abs(g.controlX[TouchLayout.FIRE]-(2400-140*3))<.01f&&Math.abs(g.controlY[TouchLayout.FIRE]-(1080-142*3))<.01f;
        g.weaponSlots=HUD_MAP;g.uiScale=1.37f;g.check("minimap 137");assert g.reserved.size()==4;
        assert Math.abs(g.controlX[TouchLayout.FIRE]-(2400-140*3))<.01f&&Math.abs(g.controlY[TouchLayout.FIRE]-(1080-142*3))<.01f;
        g.uiScale=1;
        // A player's placement is kept as dropped and defaults move away from it.
        java.util.Arrays.fill(g.customX,Float.NaN);java.util.Arrays.fill(g.customY,Float.NaN);
        g.customX[TouchLayout.FIRE]=.5f;g.customY[TouchLayout.FIRE]=.5f;g.buildLayout();
        assert Math.abs(g.controlX[TouchLayout.FIRE]-1200)<.01f&&Math.abs(g.controlY[TouchLayout.FIRE]-540)<.01f;
        g.leftHanded=true;g.buildLayout();assert Math.abs(g.controlX[TouchLayout.FIRE]-1200)<.01f;
        g.customX[TouchLayout.FIRE]=.8f;g.buildLayout();assert Math.abs(g.controlX[TouchLayout.FIRE]-.2f*2400)<.01f;
        // Original UI: the keyboard shortcut at the top right, no settings button, nothing reserved for a hidden HUD.
        g.leftHanded=false;g.playing=false;g.cursorContext=7;g.check("menu");
        assert g.reserved.isEmpty()&&!g.shown[TouchLayout.SETTINGS]&&g.shown[TouchLayout.KEYBOARD];
        // In the corner, above original column headers such as the scoreboard's.
        assert g.controlX[TouchLayout.KEYBOARD]>2000&&g.controlY[TouchLayout.KEYBOARD]<.16f*1080;
        // Pie directions and absolute hits: twelve o'clock first, clockwise.
        assert TouchLayout.sector(0,-50,8,24)==0&&TouchLayout.sector(50,0,8,24)==2&&TouchLayout.sector(0,50,8,24)==4;
        assert TouchLayout.sector(-50,0,8,24)==6&&TouchLayout.sector(-36,-36,8,24)==7&&TouchLayout.sector(10,-10,8,24)==-1;
        assert TouchLayout.sector(Float.NaN,0,8,0)==-1&&TouchLayout.sector(1,1,0,0)==-1;
        for(int n=4;n<=7;++n)for(int i=0;i<n;++i){
            double a=TouchLayout.sectorAngle(i,n);
            float x=(float)Math.sin(a)*80,y=-(float)Math.cos(a)*80;
            assert TouchLayout.sector(x,y,n,24)==i&&TouchLayout.pieHit(300+x,200+y,300,200,40,118,n)==i;
        }
        assert TouchLayout.pieHit(300,200,300,200,40,118,7)==TouchLayout.CENTER&&TouchLayout.pieHit(300,330,300,200,40,118,7)==-1;
        assert TouchLayout.fitScale(1.6f,360)<1.6f&&TouchLayout.fitScale(1.6f,800)==1.6f&&TouchLayout.fitScale(.1f,360)==.65f;
        assert gyroDelta(0,.01)==0;
        assert gyroDelta(Float.NaN,.01)==0;
        assert gyroDelta(Float.POSITIVE_INFINITY,.01)==0;
        assert gyroDelta(1,0)==0&&gyroDelta(1,-1)==0&&gyroDelta(1,.101)==0;
        float one=gyroDelta(1,.02),two=gyroDelta(1,.01)*2;
        assert Math.abs(one-two)<.00001;
        assert gyroDelta(1000,.1)==25&&gyroDelta(-1000,.1)==-25;
        assert gyroDelta(.001f,.01)==0;
        System.out.println("crowded layouts placed per control: "+g.crowded);
        System.out.println("PASS "+cases+" production layouts: 9 phones/tablets/cutout, 5 profiles, 65..160 percent controls, 100..150 percent HUD, both hands, stick or W/A/S/D cross; inside safe area, 44dp targets, no overlap, original HUD clear, rigid thumb cluster; custom/mirror/menu placement; pie sectors; gyro bounds");
    }
}
'''
        with tempfile.TemporaryDirectory() as temporary:
            path = Path(temporary) / "GeometryCheck.java"
            path.write_text(program, encoding="utf-8")
            subprocess.run([java / "javac.exe", "--release", "8", "-d", temporary,
                            ROOT / "android/app/src/org/ut3/port/TouchInput.java",
                            ROOT / "android/app/src/org/ut3/port/TouchLayout.java", path], check=True)
            subprocess.run([java / "java.exe", "-ea", "-cp", temporary, "org.ut3.port.GeometryCheck"], check=True)

    def test_pointer_ownership_edges_and_sampling(self):
        java = Path(os.environ.get("JAVA_HOME", "C:/Portable/jdk-24.0.1")) / "bin"
        program = r'''
package org.ut3.port;
import java.util.ArrayList;
final class TouchCheck {
    static final class Capture implements TouchInput.Sink {
        float x,y,dx,dy,totalX,totalY,sx,sy; int held,resets,sticks; boolean acceptStick=true;
        ArrayList<Integer> masks = new ArrayList<>();
        public void submit(float a,float b,float c,float d,int mask) {
            x=a;y=b;dx=c;dy=d;held=mask;totalX+=c;totalY+=d;masks.add(mask);
            assert Float.isFinite(a)&&Float.isFinite(b)&&Float.isFinite(c)&&Float.isFinite(d);
        }
        public boolean stick(float a,float b) {sx=a;sy=b;++sticks;return acceptStick;}
        public void reset() {x=y=dx=dy=0;held=0;resets++;}
    }
    static void close(float a,float b) {assert Math.abs(a-b)<.0001f:a+" != "+b;}
    public static void main(String[] args) {
        Capture c = new Capture(); TouchInput t = new TouchInput(c);
        t.configure(100,2,1,false);
        assert t.down(17,100,200,0,true);
        t.move(17,108,200);close(c.x,0);close(c.y,0);
        t.move(17,200,100);close((float)Math.hypot(c.x,c.y),1);
        assert t.down(4,900,400,TouchInput.FIRE,false);
        t.move(4,920,410);close(c.dx,40);close(c.dy,20);assert c.held==TouchInput.FIRE;
        assert t.down(30,900,400,TouchInput.FIRE,false);
        t.up(4,925,410);close(c.totalX,50);assert c.held==TouchInput.FIRE;
        float before=c.totalX;t.move(30,1000,400);close(c.totalX,before); // no ownership transfer
        t.up(30,1000,400);assert c.held==0;
        t.up(17,200,100);close(c.x,0);close(c.y,0);
        t.down(5,0,0,TouchInput.FIRE,false);t.down(6,0,0,TouchInput.JUMP,false);
        t.cancel();assert c.resets==1&&c.held==0&&!t.moving();
        before=c.totalX;t.up(5,999,999);t.up(6,999,999);close(c.totalX,before);
        t.down(100,0,0,0,false);t.move(100,1,2);close(c.dx,2);close(c.dy,4);
        t.down(101,0,0,TouchInput.ALT,false);t.configure(100,2,.5f,true);
        t.move(100,2,4);close(c.dx,1);close(c.dy,-2);
        t.relativeLook(4,6);close(c.dx,2);close(c.dy,-3);
        before=c.totalX;t.move(100,Float.NaN,99);t.relativeLook(Float.POSITIVE_INFINITY,0);close(c.totalX,before);
        assert !t.down(100,0,0,0,false);assert !t.down(-1,0,0,0,false);
        t.cancel();for(int i=0;i<10;i++)assert t.down(i,0,0,0,false);
        assert !t.down(11,0,0,TouchInput.FIRE,false);t.cancel();
        int count=c.masks.size();t.pulse(TouchInput.USE);t.pulse(TouchInput.USE);
        assert c.masks.size()==count+4;
        assert c.masks.get(count)==TouchInput.USE&&c.masks.get(count+1)==0;
        assert c.masks.get(count+2)==TouchInput.USE&&c.masks.get(count+3)==0;
        t.configure(Float.NaN,Float.NaN,Float.NaN,false);
        t.down(2,0,0,0,true);t.move(2,60,0);close(c.x,1);t.cancel();
        t.down(3,0,0,TouchInput.FIRE,false);count=c.masks.size();t.pulse(TouchInput.FIRE);
        assert c.masks.size()==count; t.cancel();
        t.toggle(TouchInput.SCORES);assert c.held==TouchInput.SCORES&&t.latched(TouchInput.SCORES);
        t.down(4,0,0,TouchInput.FIRE,false);assert c.held==(TouchInput.SCORES|TouchInput.FIRE);
        t.up(4,0,0);assert c.held==TouchInput.SCORES;t.cancel();assert c.held==0&&!t.latched(TouchInput.SCORES);
        // A second finger in the stick zone turns the camera instead of taking the stick.
        t.configure(100,2,1,false);
        assert t.down(1,100,500,0,TouchInput.MOVEMENT,-1)&&t.moving()&&!t.looking();
        assert t.down(2,120,520,0,TouchInput.MOVEMENT,-1)&&t.looking();
        t.move(2,125,520);close(c.dx,10);close(c.x,0);t.cancel();
        // A passive contact (weapon or scores) neither looks nor moves.
        assert t.down(3,500,500,0,TouchInput.PASSIVE,-1)&&!t.looking()&&!t.moving();
        t.move(3,600,500);close(c.dx,0);
        // Weapon picker: shoulder held first, then a normalized direction only while held.
        assert t.hold(3,TouchInput.PICKER)&&c.held==TouchInput.PICKER;
        assert t.stick(0,1)&&c.sx==0&&c.sy==1;
        assert t.stick(3,4)&&Math.abs(c.sx-.6f)<1e-6f&&Math.abs(c.sy-.8f)<1e-6f;
        int sticks=c.sticks;assert t.stick(.6f,.8f)&&c.sticks==sticks;
        c.acceptStick=false;assert !t.stick(-1,0);c.acceptStick=true;
        t.up(3,600,500);assert c.held==0&&t.stickX()==0&&t.stickY()==0;
        sticks=c.sticks;assert t.stick(1,0)&&c.sticks==sticks&&t.stickX()==0;
        assert t.down(3,0,0,0,TouchInput.PASSIVE,-1)&&t.hold(3,TouchInput.PICKER);
        assert t.stick(1,0)&&t.hold(3,0)&&c.held==0&&t.stickX()==0;t.cancel();
        assert !t.down(9,0,0,1<<28,TouchInput.BUTTON,-1)&&!t.hold(9,TouchInput.FIRE);
        // Look acceleration follows finger speed in dp/ms, never the sample count.
        TouchInput a=new TouchInput(c);a.configure(100,1,1,false);a.density(1);
        a.down(1,0,0,0,TouchInput.LOOK,0);c.totalX=0;
        for(int i=1;i<=50;++i)a.move(1,i*.2f,0,i);
        close(c.totalX,10);
        a.cancel();a.down(1,0,0,0,TouchInput.LOOK,0);c.totalX=0;
        for(int i=1;i<=50;++i)a.move(1,i*4,0,i);
        assert c.totalX>360&&c.totalX<=200*TouchInput.ACCEL_MAX+.01f:c.totalX;
        a.cancel();a.down(1,0,0,0,TouchInput.LOOK,0);c.totalX=0;
        for(int i=1;i<=50;++i){a.move(1,i*2-1,0,i);a.move(1,i*2,0,i);}
        float shared=c.totalX;a.cancel();a.down(1,0,0,0,TouchInput.LOOK,0);c.totalX=0;
        for(int i=1;i<=50;++i)a.move(1,i*2,0,i);
        assert Math.abs(shared-c.totalX)<c.totalX*.02f:shared+" vs "+c.totalX;
        a.cancel();a.acceleration(false);a.down(1,0,0,0,TouchInput.LOOK,0);c.totalX=0;
        for(int i=1;i<=50;++i)a.move(1,i*4,0,i);
        close(c.totalX,200);
        a.acceleration(true);a.cancel();a.down(1,0,0,0,TouchInput.LOOK,100);c.totalX=0;
        a.move(1,50,0,50);a.move(1,60,0,-1);close(c.totalX,60);
        assert TouchInput.accelerationGain(0)==1&&TouchInput.accelerationGain(10)==TouchInput.ACCEL_MAX;
        assert TouchInput.accelerationGain(Float.NaN)==1;
        System.out.println("PASS stable IDs, radial dead zone/diagonal normalization, independent move/fire/look, fire drag, duplicate holders, final-UP sample, no look handover, cancel/repeated-UP, ALT sensitivity/invert, relative sensor deltas, invalid packets, 10-pointer bound, fast pulse edges, finite configuration");
        System.out.println("PASS second stick finger looks, passive holds, weapon picker shoulder/direction/neutral release, 28-bit actions, speed-based look acceleration independent of sample batching");
    }
}
'''
        with tempfile.TemporaryDirectory() as temporary:
            source = Path(temporary) / "TouchCheck.java"
            source.write_text(program, encoding="utf-8")
            subprocess.run([java / "javac.exe", "--release", "8", "-d", temporary,
                            ROOT / "android/app/src/org/ut3/port/TouchInput.java", source], check=True)
            subprocess.run([java / "java.exe", "-ea", "-cp", temporary, "org.ut3.port.TouchCheck"], check=True)



if __name__ == "__main__":
    unittest.main()
