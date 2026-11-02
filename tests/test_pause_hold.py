"""Run the production pause gesture without Android, including cancellation."""
import os
from pathlib import Path
import subprocess
import tempfile
import unittest

from tests import test_touch_controls as touch_checks

ROOT = Path(__file__).resolve().parents[1]


class PauseHoldTest(unittest.TestCase):
    def test_pause_tap_hold_and_cancel(self):
        source = (ROOT / 'android/app/src/org/ut3/port/GameControls.java').read_text(encoding='utf-8')
        methods = '\n'.join(touch_checks.TouchControlsTest._method(source, name) for name in
                            ('private void pauseDown', 'private void cancelPause', 'private void pauseMove', 'private void pauseUp'))
        callback = touch_checks.TouchControlsTest._method(source, 'private final Runnable pauseHold') + ';'
        program = '''
class TouchLayout { static final int PAUSE=7; }
class ViewConfiguration { static int getLongPressTimeout(){return 500;} }
class HapticFeedbackConstants { static final int LONG_PRESS=0,VIRTUAL_KEY=1; }
public class PauseCheck {
    int pauseId=-1,pauses,editors;boolean playing=true,editing,authoritative=true,inside=true;long cursorContext=6;
    Runnable pending;long delay;
    void postDelayed(Runnable r,long t){pending=r;delay=t;}
    void removeCallbacks(Runnable r){if(pending==r)pending=null;}
    void haptic(int value){}
    void requestLayoutEditor(){++editors;editing=true;cancelPause();}
    boolean inside(int control,float x,float y){assert control==TouchLayout.PAUSE;return inside;}
    void activate(int control){assert control==TouchLayout.PAUSE;++pauses;}
''' + callback + '\n' + methods + '''
    public static void main(String[] args) {
        PauseCheck v=new PauseCheck();
        v.pauseDown(1);assert v.pending!=null&&v.delay==500&&v.pauses==0;
        v.pauseUp(0,0);assert v.pauses==1&&v.editors==0&&v.pending==null&&v.pauseId==-1;
        v.pauseDown(2);v.pauseDown(3);assert v.pauseId==2;
        v.pending.run();assert v.editors==1&&v.pauses==1&&v.pauseId==-1&&v.pending==null;
        v.pauseHold.run();assert v.editors==1;
        v.editing=false;v.pauseDown(4);v.inside=false;v.pauseMove(0,0);
        assert v.pauseId==-1&&v.pending==null;v.pauseHold.run();assert v.editors==1;
        v.inside=true;v.pauseDown(5);v.cancelPause();v.pauseHold.run();assert v.editors==1;
        v.pauseDown(6);v.cursorContext=7;v.pauseHold.run();assert v.editors==1;v.cancelPause();
        v.cursorContext=6;v.playing=false;v.pauseDown(7);v.pauseHold.run();assert v.editors==1;v.cancelPause();
        System.out.println("PASS pause: short release opens menu, hold opens editor once, slide-off/cancel/context/lifecycle retire hold");
    }
}
'''
        java = Path(os.environ['JAVA_HOME']) / 'bin'
        with tempfile.TemporaryDirectory(prefix='ut3-pause-') as directory:
            path = Path(directory) / 'PauseCheck.java'
            path.write_text(program, encoding='utf-8')
            subprocess.run([java / 'javac.exe', '-encoding', 'UTF-8', '-d', directory, path], check=True)
            subprocess.run([java / 'java.exe', '-ea', '-cp', directory, 'PauseCheck'], check=True)


if __name__ == '__main__':
    unittest.main()
