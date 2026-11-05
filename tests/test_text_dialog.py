"""Execute the production dialog routing against a small platform/bridge fixture."""
import os
from pathlib import Path
import subprocess
import tempfile
import unittest
from tests import test_touch_controls as touch_checks

ROOT = Path(__file__).resolve().parents[1]


class TextDialogTest(unittest.TestCase):
    def test_context_commit_cancel_focus_and_retired_sessions(self):
        source=(ROOT/'android/app/src/org/ut3/port/GameControls.java').read_text(encoding='utf-8')
        methods='\n'.join(touch_checks.TouchControlsTest._method(source,sig) for sig in
            ('private boolean textUi(', 'void windowFocusLost(', 'void hideKeyboard(', 'private void showKeyboard('))
        start=source.index('    private final Runnable dialogPoll=')
        poll=source[start:source.index('\n    };',start)+7]
        program=r'''
import java.util.function.Predicate;
class GameActivity {
    static long current=7,target;static int command,calls;static String value;static String[] state;
    static long nativeTouchContext(){return current;}
    static boolean nativeTextDialogRequest(int c,String v,long context,long t){command=c;value=v;target=t;++calls;return true;}
    static String[] nativeTextDialogState(long context){return state;}
    static boolean nativeText(String text,long context){command=4;value=text;return true;}
}
class TextInputDialog {
    final Predicate<String> apply;final Runnable closed;final String initial,title,note;boolean showing,password;int limit;
    TextInputDialog(GameActivity a,String text,boolean password,int limit,String title,String note,Predicate<String> apply,Runnable closed){
        this.apply=apply;this.closed=closed;initial=text;this.password=password;this.limit=limit;this.title=title;this.note=note;
    }
    void show(){showing=true;}void dismiss(){showing=false;closed.run();}
}
public class DialogCheck {
    boolean authoritative=true,imeRequested;long cursorContext=7,imeSession,imeContext,imeTouchContext;
    int imePolls,releases,notices;TextInputDialog textDialog;Runnable imeOpen=()->{};
    GameActivity activity(){return new GameActivity();}String text(String a,String b){return a;}
    void removeCallbacks(Runnable r){}void post(Runnable r){}void postDelayed(Runnable r,int delay){}
    void syncContext(){}void releaseInput(){++releases;}boolean requestFocus(){return true;}
    void textNotice(){++notices;}
''' + methods + poll + r'''
    void open(int flags,String initial){open(flags,initial,"123");}
    void open(int flags,String initial,String target){
        GameActivity.current=cursorContext=7;showKeyboard();assert imeRequested&&GameActivity.command==1;
        GameActivity.state=new String[]{initial,Integer.toString(flags),target};dialogPoll.run();
        assert textDialog!=null&&textDialog.showing&&textDialog.initial.equals(initial);
    }
    public static void main(String[] args){
        DialogCheck c=new DialogCheck();c.open(32<<8,"old");TextInputDialog first=c.textDialog;
        c.windowFocusLost();assert c.imeRequested&&first.showing;
        assert first.apply.test("new")&&GameActivity.command==2&&GameActivity.value.equals("new");
        first.dismiss();assert !c.imeRequested&&c.textDialog==null&&GameActivity.command==2;
        c.open((12<<8)|1,"secret");assert c.textDialog.password&&c.textDialog.limit==12;
        c.textDialog.dismiss();assert GameActivity.command==3&&!c.imeRequested;
        c.open(32<<8,"initial");TextInputDialog old=c.textDialog;c.hideKeyboard();assert !old.showing;
        c.open(32<<8,"current");TextInputDialog live=c.textDialog;int calls=GameActivity.calls;
        old.closed.run();assert c.textDialog==live&&c.imeRequested&&GameActivity.calls==calls;
        assert !old.apply.test("stale")&&GameActivity.calls==calls;
        GameActivity.current=11;assert !live.apply.test("wrong scene");c.hideKeyboard();
        assert !live.showing&&!c.imeRequested;
        c.open((256<<8)|2,"","0");assert c.textDialog.apply.test("typed")&&GameActivity.command==4&&c.textDialog.note==null;
        c.textDialog.dismiss();assert GameActivity.command==3;
        // The console's line (4): shown, replaced whole by a commit without a target.
        c.open((256<<8)|4,"Say helo","0");assert c.textDialog.title.equals("Console")&&c.textDialog.note==null;
        assert c.textDialog.apply.test("Say hello")&&GameActivity.command==2&&GameActivity.value.equals("Say hello")&&GameActivity.target==0;
        c.textDialog.dismiss();assert !c.imeRequested&&GameActivity.command==2;
        // Longer than the dialog shows (2|8): opens empty with a note, typed text goes to the receiver's InputChar.
        c.open((256<<8)|8|2|1,"","0");assert c.textDialog.note!=null&&c.textDialog.password&&c.textDialog.limit==256;
        assert c.textDialog.apply.test("more")&&GameActivity.command==4&&GameActivity.value.equals("more");
        c.textDialog.dismiss();assert GameActivity.command==3&&c.notices==0;
        // 300 UTF-16 units against a limit of 256 never open cut short: a notice, the request is cancelled.
        c.showKeyboard();GameActivity.state=new String[]{"a".repeat(300),Integer.toString(256<<8),"123"};c.dialogPoll.run();
        assert c.textDialog==null&&!c.imeRequested&&GameActivity.command==3&&c.notices==1;
        c.open(32<<8,"unchanged");calls=GameActivity.calls;assert c.textDialog.apply.test("unchanged");
        assert GameActivity.calls==calls;c.textDialog.dismiss();
        c.showKeyboard();GameActivity.state=null;for(int i=0;i<101;++i)c.dialogPoll.run();assert !c.imeRequested&&c.notices==2;
        c.showKeyboard();GameActivity.state=new String[0];c.dialogPoll.run();assert !c.imeRequested&&c.notices==3;
        // A changed scene closes the request without a notice.
        c.showKeyboard();GameActivity.current=11;c.dialogPoll.run();assert !c.imeRequested&&c.notices==3;
        System.out.println("PASS actual modal routing: existing text, password metadata, commit survives dismissal, cancel, focus, context, retired sessions, typed receivers, console line, long text note, no cut values, notices on refusal and timeout, unchanged text and bounded startup");
    }
}
'''
        java=Path(os.environ['JAVA_HOME'])/'bin'
        with tempfile.TemporaryDirectory() as folder:
            path=Path(folder)/'DialogCheck.java';path.write_text(program,encoding='utf-8')
            for command in ([java/'javac.exe','-d',folder,path],[java/'java.exe','-ea','-cp',folder,'DialogCheck']):
                result=subprocess.run(list(map(str,command)),capture_output=True,text=True)
                self.assertEqual(result.returncode,0,result.stdout+result.stderr)
            print(result.stdout.strip())


if __name__=='__main__':
    unittest.main()
