"""Run the production original-UI gesture classifier without Android/JNI."""
import os
from pathlib import Path
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]


class UiTouchTest(unittest.TestCase):
    def test_hover_tap_option_drag_and_ack(self):
        java = Path(os.environ.get("JAVA_HOME", "C:/Portable/jdk-24.0.1")) / "bin"
        program = r'''
package org.ut3.port;
import java.util.ArrayList;
public final class UiTouchCheck {
    static final class Capture implements UiTouchInput.Sink {
        int kind,downs,ups,resets,hovers,taps,ends;boolean reject,held;
        float x,y;final ArrayList<String> events=new ArrayList<>();
        public boolean begin(float x,float y){events.add("begin");this.x=x;this.y=y;return !reject;}
        public int control(){return kind;}
        public boolean pointer(float x,float y){this.x=x;this.y=y;events.add("move");return !reject;}
        public boolean button(boolean down){assert down!=held;held=down;if(down)++downs;else ++ups;events.add(down?"down":"up");return !reject;}
        public boolean scroll(int request){
            assert !held&&(request==0||request==UiTouchInput.HOVER||request==UiTouchInput.OPTION_TAP);
            if(request==0)++ends;else if(request==UiTouchInput.HOVER)++hovers;else ++taps;
            events.add(request==0?"end":request==UiTouchInput.HOVER?"hover":"tap");return !reject;}
        public void reset(){held=false;++resets;}
    }
    static void same(float a,float b){assert Math.abs(a-b)<.001f;}
    public static void main(String[] args){
        Capture c=new Capture();UiTouchInput t=new UiTouchInput(c);t.configure(8,24);
        final int TAP=UiTouchInput.TAP,ROWS=UiTouchInput.ROWS,DRAG=UiTouchInput.DRAG,OPTION=UiTouchInput.OPTION;
        // A button: a tap clicks once the original hover is acknowledged, even after Android UP.
        assert t.begin(100,100)&&t.waiting();assert !t.begin(200,200);
        t.move(100,98);t.end(100,98);assert t.active()&&c.downs==0;
        c.kind=TAP;t.poll();assert !t.active()&&c.downs==1&&c.ups==1&&c.ends==1&&c.resets==0;
        t.cancel();assert c.resets==0; // Normal UP must not reset a queued native tap.
        // A swipe across buttons only points: no press, the cursor follows the finger.
        assert t.begin(100,100);t.move(160,100);t.move(300,180);same(c.x,300);same(c.y,180);
        t.end(320,190);assert c.downs==1&&c.ups==1&&c.hovers==0&&!t.active()&&c.ends==2;
        // A list: one request makes the game's own Tick highlight the row under the moving cursor;
        // moves only move the cursor, so the queue never fills with requests. A swipe never clicks.
        c.kind=ROWS|TAP;assert t.begin(100,200)&&c.hovers==1&&!t.polling();
        t.move(100,150);t.move(100,120);assert c.hovers==1&&c.downs==1;same(c.y,120);
        t.end(100,140);assert c.downs==1&&c.hovers==1&&c.ends==3&&!t.active();
        // A tap on a list row selects it and clicks it, the original activation.
        assert t.begin(50,60)&&c.hovers==2;t.end(52,61);assert c.downs==2&&c.ups==2&&c.taps==0&&!t.active();
        // An option row: a tap changes the option through the script, never by a raw click.
        c.kind=ROWS|OPTION;assert t.begin(70,80)&&c.hovers==3;t.end(70,80);
        assert c.taps==1&&c.downs==2&&!t.active();
        // A swipe over option rows only highlights them.
        assert t.begin(70,80);t.move(70,180);t.end(75,200);assert c.taps==1&&c.downs==2&&c.hovers==4;
        // A slider: a sideways drag presses at its start and adjusts; a vertical one points at rows.
        c.kind=ROWS|DRAG|TAP;assert t.begin(100,100);t.move(140,104);assert c.downs==3&&c.held;same(c.x,140);
        int hovers=c.hovers;t.move(90,160);assert c.hovers==hovers&&c.held;t.end(95,170);
        assert c.ups==3&&!c.held&&!t.active();same(c.x,95);
        assert t.begin(100,100);t.move(102,160);assert c.downs==3&&c.hovers>hovers;t.end(102,160);assert c.downs==3;
        c.kind=DRAG|TAP;assert t.begin(10,10);t.end(10,10);assert c.downs==4&&c.ups==4; // Tap on a slider clicks it.
        // Waiting for the hover acknowledgement keeps the whole gesture.
        c.kind=0;assert t.begin(50,60);t.move(80,120);assert c.downs==4;
        c.kind=DRAG|TAP;t.poll();assert !c.held&&c.downs==4;same(c.x,80);same(c.y,120);
        t.cancel();assert c.resets==1&&!c.held&&!t.active();t.end(99,99);assert c.ups==4;
        // Retired, invalid and rejected input.
        c.kind=0;assert t.begin(100,100);t.end(100,100);c.kind=-1;t.poll();assert c.resets==2&&c.downs==4;
        assert !t.begin(Float.NaN,1);c.kind=ROWS;assert t.begin(100,100);
        t.move(Float.POSITIVE_INFINITY,1);t.end(100,-500);assert c.downs==4&&!t.active();
        c.reject=true;assert !t.begin(1,1);assert c.resets==2;
        c.reject=false;assert t.begin(1,100);c.reject=true;t.move(1,0);assert !t.active()&&c.resets==3;
        c.reject=false;c.kind=TAP;assert t.begin(1,100);c.reject=true;t.end(1,100);assert !t.active()&&c.resets==4;
        System.out.println("PASS production UI gestures: swipes only point and highlight list rows, taps click or change option rows, one pointing request per gesture, sideways slider drags adjust, delayed hover ACK/UP, cancel/stale/invalid/rejected input");
    }
}
'''
        with tempfile.TemporaryDirectory() as temporary:
            source = Path(temporary) / "UiTouchCheck.java"
            source.write_text(program, encoding="utf-8")
            subprocess.run([java / "javac.exe", "--release", "8", "-d", temporary,
                            ROOT / "android/app/src/org/ut3/port/UiTouchInput.java", source], check=True)
            subprocess.run([java / "java.exe", "-ea", "-cp", temporary, "org.ut3.port.UiTouchCheck"], check=True)


if __name__ == "__main__":
    unittest.main()
