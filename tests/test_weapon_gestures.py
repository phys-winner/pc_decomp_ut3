"""The production weapon button and bar honor independent swipe settings."""
import os
from pathlib import Path
import subprocess
import unittest
from tests import test_touch_controls as touch_checks

ROOT = Path(__file__).resolve().parents[1]


class WeaponGesturesTest(unittest.TestCase):
    def test_independent_bar_and_button_swipes_keep_tap_and_picker(self):
        source = (ROOT/'android/app/src/org/ut3/port/GameControls.java').read_text(encoding='utf-8')
        methods = '\n'.join(touch_checks.TouchControlsTest._method(source, name) for name in (
            'private void weaponMove(', 'private void weaponUp(', 'private void scrubMove('))
        program = r'''
package org.ut3.port;
class ViewConfiguration {
    static ViewConfiguration get(Object o){return new ViewConfiguration();}int getScaledTouchSlop(){return 10;}
}
class TouchInput {static final int PREVIOUS=1,NEXT=2;}
class GameActivity {static int previews;static boolean nativeTouchWeapon(int p,int s,long c){assert p==1;++previews;return true;}}
class Input {
    int pulses,last;float x,y;void pulse(int p){++pulses;last=p;}void up(int id,float x,float y,long t){}
    void stick(float a,float b){x=a;y=b;}void cancel(){}
}
public class WeaponCheck {
    static final int BAR_SHOWN=1<<14;boolean weaponPicker,weaponSwipe,weaponButtonSwipe,scrubButton,scrubMoved;
    float weaponLastX,weaponLastY,weaponX,weaponY,dp=1,scrubX,scrubStep=40;
    int weaponId=3,weaponSlots=BAR_SHOWN,scrubId=-1,scrubStart,scrubIndex,starts,ticks;
    int[] scrubSlots={0,1,2};long cursorContext=6;Object weaponHold=new Object();Input input=new Input();
    Object getContext(){return this;}void removeCallbacks(Object r){}void invalidate(){}void syncContext(){}
    void weaponStick(float x,float y){input.stick(x,y);}
    boolean scrubStart(int id,float x,boolean button){++starts;scrubId=id;scrubX=x;scrubButton=button;return true;}
    void weaponTick(){++ticks;}
''' + methods + r'''
    public static void main(String[] args){
        for(boolean bar:new boolean[]{false,true})for(boolean button:new boolean[]{false,true}){
            WeaponCheck c=new WeaponCheck();c.weaponSwipe=bar;c.weaponButtonSwipe=button;c.weaponMove(80,0);
            assert (c.starts==1)==button;
            assert (c.scrubIndex==2)==button;
            c=new WeaponCheck();c.weaponSwipe=bar;c.weaponButtonSwipe=button;c.scrubId=4;
            c.scrubButton=false;c.scrubMove(80);assert (c.scrubIndex==2)==bar;
            c=new WeaponCheck();c.weaponSwipe=bar;c.weaponButtonSwipe=button;c.weaponUp(80,0,0);
            assert (c.input.pulses==1)==button;if(button)assert c.input.last==TouchInput.NEXT;
            c=new WeaponCheck();c.weaponSwipe=bar;c.weaponButtonSwipe=button;c.weaponUp(0,0,0);
            assert c.input.pulses==1&&c.input.last==TouchInput.NEXT;
            c=new WeaponCheck();c.weaponSwipe=bar;c.weaponButtonSwipe=button;c.weaponPicker=true;c.weaponUp(80,0,0);
            assert !c.weaponPicker&&c.input.pulses==0&&c.input.x==0&&c.input.y==0;
        }
        System.out.println("PASS all four independent weapon swipe combinations, button preview/flick, bar preview, tap-next and held-wheel release");
    }
}
'''
        out = ROOT/'build/weapon-gestures-test'
        out.mkdir(exist_ok=True)
        path = out/'WeaponCheck.java'
        path.write_text(program)
        java = Path(os.environ['JAVA_HOME'])/'bin'
        for command in ([java/'javac.exe','-d',out,ROOT/'android/app/src/org/ut3/port/TouchLayout.java',path],
                        [java/'java.exe','-ea','-cp',out,'org.ut3.port.WeaponCheck']):
            result = subprocess.run(list(map(str,command)),capture_output=True,text=True)
            self.assertEqual(result.returncode,0,result.stdout+result.stderr)
        print(result.stdout.strip())


if __name__ == '__main__':
    unittest.main()
