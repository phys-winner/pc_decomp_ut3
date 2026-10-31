package org.ut3.port;

import android.content.Context;
import android.graphics.Canvas;
import android.graphics.Paint;
import android.graphics.Rect;
import android.hardware.Sensor;
import android.hardware.SensorEvent;
import android.hardware.SensorEventListener;
import android.hardware.SensorManager;
import android.os.Build;
import android.os.Bundle;
import android.os.CombinedVibration;
import android.os.SystemClock;
import android.os.VibrationEffect;
import android.os.Vibrator;
import android.os.VibratorManager;
import android.text.InputType;
import android.view.Display;
import android.view.HapticFeedbackConstants;
import android.view.InputDevice;
import android.view.InputEvent;
import android.view.KeyEvent;
import android.view.MotionEvent;
import android.view.Surface;
import android.view.View;
import android.view.WindowInsets;
import android.view.ViewConfiguration;
import android.view.accessibility.AccessibilityEvent;
import android.view.accessibility.AccessibilityManager;
import android.view.accessibility.AccessibilityNodeInfo;
import android.view.accessibility.AccessibilityNodeProvider;
import android.view.inputmethod.BaseInputConnection;
import android.view.inputmethod.EditorInfo;
import android.view.inputmethod.InputConnection;
import android.view.inputmethod.InputMethodManager;
import java.util.ArrayList;

final class GameControls extends View implements SensorEventListener {
    /** Control behaviours beyond TouchInput's held action bits. */
    private static final int MORE=-1, MODE=-2, SETTINGS=-3, KEYBOARD=-4, WEAPON=-5, SEATS=-6;
    private static final int PIE_ACTIONS=0, PIE_SEATS=1, SEAT_COUNT=5;
    private static final int ACCENT=0xFFFFAE42;
    /** A held weapon button opens the original quick pick; a shorter tap or flick switches weapons. */
    private static final long PICK_HOLD_MS=180, SCORES_TAP_MS=300, HINT_MS=5500, RESET_CONFIRM_MS=3000;
    private static final int HINT_LOOK=1, HINT_WEAPON=2, HINT_MORE=4, HINT_VEHICLE=8, HINT_CROUCH=16, HINT_STEER=32, HINT_LEFT_FIRE=64;
    /** Script vehicle flags: Jump deploys, deployed, a gunner seat passes Jump on, look steering;
     *  16|32 without look steering is the gyroscope's sniper mode (scope zoom, a steered Redeemer). */
    private static final int VEHICLE_DEPLOY=1, VEHICLE_DEPLOYED=2, VEHICLE_SEAT_JUMP=4, VEHICLE_LOOK_STEER=8, VEHICLE_SCOPED=48;
    /** Gyroscope aim modes of the ANDROID settings. */
    static final int GYRO_OFF=0, GYRO_SNIPER=1, GYRO_FIRING=2, GYRO_ALWAYS=3;
    /** Script weapon bar word: slot bits 0-9, the selected slot in 10-13, shown in 14, HUD minimap in 15. */
    private static final int BAR_SHOWN=1<<14,HUD_MAP=1<<15;
    private static final int TOOL_DONE=0, TOOL_RESET=1;
    private final Paint paint=new Paint(Paint.ANTI_ALIAS_FLAG);
    private final Rect safe=new Rect();
    private final float[] controlX=new float[TouchLayout.COUNT],controlY=new float[TouchLayout.COUNT],
        controlR=new float[TouchLayout.COUNT],customX=new float[TouchLayout.COUNT],customY=new float[TouchLayout.COUNT];
    private final boolean[] shown=new boolean[TouchLayout.COUNT];
    private final ArrayList<float[]> occupied=new ArrayList<>(),reserved=new ArrayList<>();
    /** Utility controls under a finger, acting on release over the button, and their pointer ids. */
    private final int[] utility=new int[10],utilityIds=new int[10];
    private final TouchInput input;
    private final UiTouchInput uiInput;
    private final SensorManager sensors;
    private final Sensor gyro;
    private Runnable settingsAction;
    private float dp,scale=1,layoutScale=1,opacity=.65f,sensitivity=.22f,gyroSensitivity=1,uiScale=1;
    private boolean playing,editing,leftHanded,invertY,pausedByControl,focused,gyroListening,gyroActive,wasd;
    /** On-screen W/A/S/D cross: its centre and cell size while shown, the finger driving it and its direction. */
    private float dpadX,dpadY,dpadCell;
    private boolean dpadShown;
    private int dpadId=-1,dpadDirection=-1;
    private int menuId=-1,profile,profileOverride,gyroMode,sensorRotation=-1;
    private boolean authoritative,layoutDirty=true;
    private boolean imeRequested,imeWasVisible;
    private long imeSession;
    private int accessibilityFocus=-1,hovered=-1;
    private long sensorTime,resetDeadline;
    private long cursorContext;
    private boolean layoutPending;
    private boolean settingsPending;
    // Layout editor: the dragged control and the toolbar.
    private int editId=-1,editControl=-1,toolId=-1,toolPressed=-1;
    private float editDX,editDY;
    private final float[][] tools=new float[2][];
    // Weapon button: tap next, swipe along the weapon bar, hold the original quick pick.
    private int weaponId=-1;
    private boolean weaponPicker;
    private float weaponX,weaponY,weaponLastX,weaponLastY;
    // Weapon bar swipe: starts on the selected weapon, one card per step, switches on release.
    private int scrubId=-1,scrubStart,scrubIndex,weaponSlots,vehicleFlags;
    private int[] scrubSlots=new int[0];
    private float scrubX,scrubStep;
    private boolean scrubButton;
    private Vibrator vibrator;
    // Scores: held shows while held, a tap keeps them shown until the next tap.
    private int scoresId=-1;
    private long scoresTime;
    // Pie: More actions or vehicle seats; drag and release, or tap to keep it open.
    private boolean pieOpen,pieDragged,pieSticky;
    private int pieId=-1,pieKind,pieSelected=-1,pieSource=-1,swallowed=-1;
    /** A gesture that began over a menu while a movie played: it skipped the movie and stays its own. */
    private boolean movieTouch;
    private int[] pieItems=new int[0];
    private float pieX,pieY,pieR,pieStartX,pieStartY;
    private String pieHint;
    // One-time hints.
    private int hintsSeen;
    private String hintText;
    private long hintUntil;
    private final Runnable contextPoll=new Runnable() {
        @Override public void run() { syncContext();applyPendingSettings();postDelayed(this,100); }
    };
    private final Runnable uiPoll=new Runnable() {
        @Override public void run() {
            syncContext();uiInput.poll();
            // Also while a finger points at a list: held beyond its edge, the selection keeps stepping.
            if(uiInput.polling())postOnAnimation(this);
        }
    };
    private final Runnable weaponHold=new Runnable() {
        @Override public void run() { if(weaponId>=0&&!weaponPicker)startPicker(); }
    };
    private final Runnable hintCheck=new Runnable() {
        @Override public void run() { offerHint(); }
    };
    /*
     * The original UI stops its pointer short of the right and bottom edges (at
     * 2401x1053 on a 2424x1080 screen), and the host waits until the UI reaches
     * each point a finger gives it. UI points stay inside that reach, with a
     * margin that also covers other screen sizes.
     */
    private boolean uiPointer(float x,float y) {
        float w=getWidth(),h=getHeight();
        if(w<=0||h<=0)return false;
        float right=w-Math.max(24f,h*.024f),bottom=h-Math.max(28f,h*.028f);
        return GameActivity.nativePointer(Math.min(x,right)/w,Math.min(y,bottom)/h,false);
    }

    GameControls(Context context) {
        super(context);
        setFocusable(true);
        setFocusableInTouchMode(true);
        setHapticFeedbackEnabled(true);
        setImportantForAccessibility(View.IMPORTANT_FOR_ACCESSIBILITY_YES);
        for(int i=0;i<utility.length;++i)utility[i]=-1;
        input=new TouchInput(new TouchInput.Sink() {
            @Override public void submit(float x,float y,float dx,float dy,int held) {
                if(!GameActivity.nativeTouch(x,y,dx,dy,held,cursorContext)) { input.cancel();syncContext(); }
                updateGyro();
            }
            @Override public boolean stick(float x,float y) { return GameActivity.nativeTouchStick(x,y,cursorContext); }
            @Override public void reset() { GameActivity.nativeTouchReset(); }
        });
        uiInput=new UiTouchInput(new UiTouchInput.Sink() {
            @Override public boolean begin(float x,float y) {
                return pointer(x,y)&&GameActivity.nativeUiBegin(cursorContext);
            }
            @Override public int control() { return GameActivity.nativeUiControl(cursorContext); }
            @Override public boolean pointer(float x,float y) { return uiPointer(x,y); }
            @Override public boolean button(boolean down) {
                return GameActivity.nativeTouch(0,0,0,0,down?TouchInput.FIRE:0,cursorContext);
            }
            @Override public boolean scroll(int rows) { return GameActivity.nativeUiScroll(rows,cursorContext); }
            @Override public void reset() { GameActivity.nativeTouchReset(); }
        });
        sensors=(SensorManager)context.getSystemService(Context.SENSOR_SERVICE);
        vibrator=context.getSystemService(Vibrator.class);
        gyro=sensors==null?null:sensors.getDefaultSensor(Sensor.TYPE_GYROSCOPE);
        reloadSettings();
    }
    private GameActivity activity() { return (GameActivity)getContext(); }
    private PortPreferences preferences() { return activity().portSettings(); }
    private String text(String english,String russian) {
        PortPreferences p=preferences();
        return p!=null&&"RUS".equals(p.getString("language","INT"))?russian:english;
    }
    void setSettingsAction(Runnable action) { settingsAction=action; }
    void reloadSettings() {
        release();settingsPending=false;
        dp=getResources().getDisplayMetrics().density;
        PortPreferences p=preferences();
        for(int i=0;i<TouchLayout.COUNT;++i)customX[i]=customY[i]=Float.NaN;
        if(p!=null) {
            sensitivity=clamp(p.getFloat("touchSensitivity",.22f),.04f,3);
            scale=clamp(p.getFloat("touchScale",1),.65f,1.6f);
            opacity=clamp(p.getFloat("touchOpacity",.65f),.2f,1);
            uiScale=clamp(p.getFloat("uiScale",1),1,1.5f);
            leftHanded=p.getBoolean("touchLeftHanded",false);
            invertY=p.getBoolean("touchInvertY",false);
            gyroMode=gyroMode(p);
            wasd=p.getBoolean("touchWasd",false);
            profileOverride=Math.max(0,Math.min(4,p.getInt("touchVehicleProfile",0)));
            gyroSensitivity=clamp(p.getFloat("touchGyroSensitivity",1),.1f,5);
            hintsSeen=p.getInt("touchHints",0);
            for(int i=0;i<TouchLayout.COUNT;++i) {
                float x=p.getFloat("touchX"+TouchLayout.NAMES[i],Float.NaN),y=p.getFloat("touchY"+TouchLayout.NAMES[i],Float.NaN);
                if(x>=0&&x<=1&&y>=0&&y<=1){customX[i]=x;customY[i]=y;}
            }
        }
        input.configure(60*scale*dp,sensitivity*10/dp,
            p==null?1:clamp(p.getFloat("touchAltSensitivity",1),.1f,3),invertY);
        input.density(dp);
        uiInput.configure(ViewConfiguration.get(getContext()).getScaledTouchSlop(),24*dp*uiScale);
        int observed=GameActivity.nativeTouchVehicle();
        authoritative=observed>=0;
        profile=profileOverride>0?profileOverride:Math.max(0,observed);
        readDetails();
        setContentDescription(text("Touch controls. Drag on the right to look; drag Fire to aim while shooting.",
            "Сенсорное управление. Ведите пальцем справа для обзора; ведите по кнопке огня, чтобы целиться при стрельбе."));
        layoutDirty=true;updateGyro(); invalidate();
    }
    void requestSettingsReload() { settingsPending=true;applyPendingSettings(); }
    private void applyPendingSettings() {
        // A native UI click can still await mouse-poll acknowledgement after Android UP.
        if(settingsPending&&(cursorContext&3)!=3&&menuId<0&&editId<0&&!input.hasContacts()&&!imeRequested)reloadSettings();
    }
    private static float clamp(float value,float minimum,float maximum) {
        return Math.max(minimum,Math.min(maximum,value));
    }
    /** The ANDROID tab's gyroscope aim mode; before it was chosen, an older build's touchGyro
     *  (1 secondary fire, mostly the sniper rifle's zoom; 2 always) counts. */
    static int gyroMode(PortPreferences p) {
        int mode=p.getInt("touchGyroMode",-1);
        if(mode>=0)return Math.min(GYRO_ALWAYS,mode);
        int legacy=p.getInt("touchGyro",0);
        return legacy==2?GYRO_ALWAYS:legacy==1?GYRO_SNIPER:GYRO_OFF;
    }
    void release() {
        hideKeyboard();releaseInput();
    }
    private void releaseInput() {
        removeCallbacks(uiPoll);removeCallbacks(weaponHold);padRelease();
        if(uiInput!=null)uiInput.cancel();
        if(input!=null) input.cancel();
        menuId=editId=editControl=toolId=toolPressed=-1;
        weaponId=scoresId=swallowed=scrubId=dpadId=-1;weaponPicker=false;
        for(int i=0;i<utility.length;++i)utility[i]=-1;
        pieOpen=false;pieId=-1;pieSelected=-1;
        sensorTime=0; gyroActive=false; stopGyro(); invalidate();
    }
    void hideKeyboard() {
        if(!imeRequested&&!imeWasVisible)return;
        ++imeSession;
        imeRequested=imeWasVisible=false;
        InputMethodManager manager=(InputMethodManager)getContext().getSystemService(Context.INPUT_METHOD_SERVICE);
        if(manager!=null) {
            manager.restartInput(this);
            manager.hideSoftInputFromWindow(getWindowToken(),0);
        }
    }
    private boolean textUi(long context) {
        return authoritative&&(context&3)==3&&context==cursorContext&&context==GameActivity.nativeTouchContext();
    }
    private void showKeyboard() {
        syncContext();if(!textUi(cursorContext))return;
        if(imeRequested){hideKeyboard();return;}
        releaseInput();++imeSession;imeRequested=true;
        if(!requestFocus()){imeRequested=false;return;}
        final long context=cursorContext;
        final InputMethodManager manager=(InputMethodManager)getContext().getSystemService(Context.INPUT_METHOD_SERVICE);
        if(manager==null){imeRequested=false;return;}
        manager.restartInput(this);
        post(new Runnable(){@Override public void run(){if(imeRequested&&textUi(context))manager.showSoftInput(GameControls.this,InputMethodManager.SHOW_IMPLICIT);}});
    }
    @Override public boolean onCheckIsTextEditor() { return imeRequested&&authoritative&&(cursorContext&3)==3; }
    @Override public InputConnection onCreateInputConnection(EditorInfo info) {
        if(!onCheckIsTextEditor())return null;
        info.inputType=InputType.TYPE_CLASS_TEXT|InputType.TYPE_TEXT_FLAG_NO_SUGGESTIONS;
        info.imeOptions=EditorInfo.IME_ACTION_DONE|EditorInfo.IME_FLAG_NO_EXTRACT_UI|
            EditorInfo.IME_FLAG_NO_FULLSCREEN|EditorInfo.IME_FLAG_NO_PERSONALIZED_LEARNING;
        info.initialSelStart=info.initialSelEnd=0;
        final long context=cursorContext;
        final long session=++imeSession;
        final TouchText text=new TouchText(new TouchText.Sink(){
            @Override public boolean commit(String value){return GameActivity.nativeText(value,context);}
            @Override public boolean delete(int before){return GameActivity.nativeDelete(before,context);}
        });
        return new BaseInputConnection(this,false) {
            private boolean ready(){if(imeRequested&&session==imeSession&&textUi(context))return true;if(session==imeSession)hideKeyboard();return false;}
            @Override public boolean commitText(CharSequence value,int position){return ready()&&position==1&&text.commit(value);}
            @Override public boolean setComposingText(CharSequence value,int position){return ready()&&position==1&&text.compose(value);}
            @Override public boolean finishComposingText(){return ready()&&text.finish();}
            @Override public boolean deleteSurroundingText(int before,int after){return ready()&&text.delete(before,after);}
            @Override public boolean deleteSurroundingTextInCodePoints(int before,int after){return ready()&&text.deleteCodePoints(before,after);}
            @Override public CharSequence getTextBeforeCursor(int length,int flags){return ready()?text.before(length):"";}
            @Override public CharSequence getTextAfterCursor(int length,int flags){return "";}
            @Override public CharSequence getSelectedText(int flags){return null;}
            @Override public int getCursorCapsMode(int modes){return 0;}
            @Override public boolean performEditorAction(int action) {
                if(!ready())return false;
                boolean result=text.finish();hideKeyboard();return result;
            }
            @Override public boolean sendKeyEvent(KeyEvent event) {
                if(!ready())return false;
                int code=event.getKeyCode();
                if(code==KeyEvent.KEYCODE_DEL)return event.getAction()!=KeyEvent.ACTION_DOWN||text.backspace();
                if(code==KeyEvent.KEYCODE_ENTER||code==KeyEvent.KEYCODE_BACK) {
                    if(event.getAction()==KeyEvent.ACTION_DOWN)return performEditorAction(EditorInfo.IME_ACTION_DONE);
                    return true;
                }
                int unicode=event.getUnicodeChar();
                return event.getAction()==KeyEvent.ACTION_DOWN&&unicode>0&&text.commit(new String(Character.toChars(unicode)));
            }
        };
    }
    @Override public boolean onKeyPreIme(int code,KeyEvent event) {
        if(code==KeyEvent.KEYCODE_BACK&&imeRequested) {
            if(event.getAction()==KeyEvent.ACTION_UP)hideKeyboard();
            return true;
        }
        return super.onKeyPreIme(code,event);
    }
    private void mode(boolean next) {
        if(authoritative||(cursorContext&3)==1)return;
        release(); playing=next;layoutDirty=true;
        if(next&&pausedByControl) { input.pulse(TouchInput.PAUSE); pausedByControl=false; }
        if(next&&layoutPending&&(cursorContext&2)==0)editLayout();
        updateGyro(); invalidate();
    }
    void editLayout() {
        release(); editing=true;layoutPending=false;layoutDirty=true;resetDeadline=0;
        invalidate();
        announceForAccessibility(text("Drag a control. Tap Done to save.","Перетащите кнопки. Нажмите «Готово» для сохранения."));
    }
    void requestLayoutEditor() { layoutPending=true;syncContext(); }
    boolean handleBack() {
        if(imeRequested) { hideKeyboard();return true; }
        if(editing) { finishEditing();return true; }
        if(pieOpen) { closePie();return true; }
        return false;
    }
    private void finishEditing() {
        release(); editing=false;layoutDirty=true;
        PortPreferences p=preferences();
        if(p!=null) {
            for(int i=0;i<TouchLayout.COUNT;++i) {
                boolean custom=!Float.isNaN(customX[i]);
                p.put("touchX"+TouchLayout.NAMES[i],custom?Float.toString(customX[i]):"");
                p.put("touchY"+TouchLayout.NAMES[i],custom?Float.toString(customY[i]):"");
            }
            activity().savePortSettings(null);
        }
        updateGyro(); invalidate();
    }
    private void resetLayout() {
        long now=SystemClock.uptimeMillis();
        if(now>resetDeadline) {
            resetDeadline=now+RESET_CONFIRM_MS;postInvalidateDelayed(RESET_CONFIRM_MS);
            announceForAccessibility(text("Tap Reset again to confirm.","Нажмите «Сброс» ещё раз для подтверждения."));
            invalidate(); return;
        }
        for(int i=0;i<TouchLayout.COUNT;++i)customX[i]=customY[i]=Float.NaN;
        resetDeadline=0;layoutDirty=true; invalidate();
    }
    @Override public WindowInsets onApplyWindowInsets(WindowInsets insets) {
        releaseInput();
        android.graphics.Insets bars=insets.getInsets(WindowInsets.Type.systemBars());
        safe.set(bars.left,bars.top,bars.right,bars.bottom);
        boolean shown=insets.isVisible(WindowInsets.Type.ime());
        if(shown)imeWasVisible=true;
        else if(imeWasVisible)hideKeyboard();
        if(insets.getDisplayCutout()!=null) {
            safe.left=Math.max(safe.left,insets.getDisplayCutout().getSafeInsetLeft());
            safe.top=Math.max(safe.top,insets.getDisplayCutout().getSafeInsetTop());
            safe.right=Math.max(safe.right,insets.getDisplayCutout().getSafeInsetRight());
            safe.bottom=Math.max(safe.bottom,insets.getDisplayCutout().getSafeInsetBottom());
        }
        layoutDirty=true;invalidate(); return insets;
    }
    @Override protected void onSizeChanged(int width,int height,int oldWidth,int oldHeight) {
        release(); layoutDirty=true; super.onSizeChanged(width,height,oldWidth,oldHeight);
    }
    @Override public void onWindowFocusChanged(boolean value) {
        super.onWindowFocusChanged(value); focused=value;
        if(!value) release(); else updateGyro();
    }
    @Override protected void onDetachedFromWindow() {
        removeCallbacks(contextPoll);removeCallbacks(uiPoll);removeCallbacks(hintCheck);
        removeCallbacks(rumblePoll);rumblePolling=false;rumble(0);
        release();super.onDetachedFromWindow();
    }
    @Override protected void onAttachedToWindow() { super.onAttachedToWindow();post(contextPoll); }
    private void syncContext() {
        long context=GameActivity.nativeTouchContext();
        int observed=GameActivity.nativeTouchVehicle();
        authoritative=observed>=0;
        int nextProfile=profileOverride>0?profileOverride:Math.max(0,observed);
        if(profile!=nextProfile){profile=nextProfile;layoutDirty=true;invalidate();}
        readDetails();
        if(context!=cursorContext) {
            cursorContext=context;
            release();playing=(context&3)==2;pausedByControl=false;
            if(editing&&!playing){editing=false;requestSettingsReload();}
            layoutDirty=true;updateGyro();invalidate();
            removeCallbacks(hintCheck);
            if(playing&&!editing)postDelayed(hintCheck,1500);
        }
        if(layoutPending&&(context&3)==2)editLayout();
    }
    /** Weapon bar and vehicle details, which change without a new input context. */
    private void readDetails() {
        long details=GameActivity.nativeTouchDetails();
        if(((weaponSlots^(int)details)&HUD_MAP)!=0){layoutDirty=true;invalidate();}
        weaponSlots=(int)details;
        int flags=(int)(details>>>32);
        if(flags!=vehicleFlags){vehicleFlags=flags;layoutDirty=true;invalidate();updateGyro();}
    }
    /** A zoomed scope or a steered Redeemer missile (the script's 16|32 without look steering). */
    private boolean scoped() { return (vehicleFlags&(VEHICLE_LOOK_STEER|VEHICLE_SCOPED))==VEHICLE_SCOPED; }
    private boolean deploys() { return !editing&&profile!=0&&(vehicleFlags&VEHICLE_DEPLOY)!=0; }
    private float width() { return Math.max(1,getWidth()-safe.left-safe.right); }
    private float height() { return Math.max(1,getHeight()-safe.top-safe.bottom); }

    /** What a control does in the current vehicle profile; 0 hides it. */
    private int action(int control) {
        int p=editing?0:profile;
        switch(control) {
        case TouchLayout.FIRE:return TouchInput.FIRE;
        case TouchLayout.ALT:return TouchInput.ALT;
        // A gunner seat keeps Jump only where the original passes it on (Hellfire, Leviathan...).
        case TouchLayout.JUMP:return p==4&&(vehicleFlags&VEHICLE_SEAT_JUMP)==0?0:TouchInput.JUMP;
        case TouchLayout.DUCK:return p==4?0:TouchInput.DUCK;
        case TouchLayout.USE:return TouchInput.USE;
        case TouchLayout.WEAPON:return p==0?WEAPON:p==3?0:SEATS;
        case TouchLayout.BOARD:return p==0||p==3?TouchInput.BOARD:0;
        case TouchLayout.MORE:return MORE;
        case TouchLayout.PAUSE:return TouchInput.PAUSE;
        case TouchLayout.SCORES:return TouchInput.SCORES;
        case TouchLayout.SETTINGS:return SETTINGS;
        case TouchLayout.KEYBOARD:return KEYBOARD;
        case TouchLayout.MODE:return MODE;
        case TouchLayout.FIRE2:return TouchInput.FIRE;
        default:return 0;
        }
    }
    private boolean visible(int control) {
        long context=cursorContext&3;
        if(editing)return (control<=TouchLayout.SCORES||control==TouchLayout.FIRE2)&&action(control)!=0;
        if(control==TouchLayout.KEYBOARD)return authoritative&&context==3;
        // The ANDROID settings are a tab of the original settings: no separate button.
        if(control==TouchLayout.SETTINGS)return false;
        if(control==TouchLayout.MODE)return !authoritative&&context!=1;
        if(context==1||!playing)return false;
        return action(control)!=0;
    }
    private int icon(int control) {
        int p=editing?0:profile;
        switch(control) {
        case TouchLayout.FIRE:case TouchLayout.FIRE2:return TouchIcons.FIRE;
        case TouchLayout.ALT:return TouchIcons.ALT;
        case TouchLayout.JUMP:return deploys()?(vehicleFlags&VEHICLE_DEPLOYED)!=0?TouchIcons.UNDEPLOY:TouchIcons.DEPLOY:
            p==2?TouchIcons.UP:TouchIcons.JUMP;
        case TouchLayout.DUCK:return p==2?TouchIcons.DOWN:p==1?TouchIcons.BRAKE:TouchIcons.DUCK;
        case TouchLayout.USE:return p>0?TouchIcons.EXIT:TouchIcons.USE;
        case TouchLayout.WEAPON:return p==0?TouchIcons.WEAPON:TouchIcons.SEATS;
        case TouchLayout.BOARD:return TouchIcons.BOARD;
        case TouchLayout.MORE:return TouchIcons.MORE;
        case TouchLayout.PAUSE:return TouchIcons.PAUSE;
        case TouchLayout.SCORES:return TouchIcons.SCORES;
        case TouchLayout.SETTINGS:return TouchIcons.SETTINGS;
        case TouchLayout.KEYBOARD:return TouchIcons.KEYBOARD;
        default:return playing?TouchIcons.MENU:TouchIcons.PLAY;
        }
    }
    private String label(int control) {
        int p=editing?0:profile;
        switch(control) {
        case TouchLayout.FIRE:return text("Fire","Огонь");
        case TouchLayout.FIRE2:return text("Fire, left thumb","Огонь левой рукой");
        case TouchLayout.ALT:return text("Alternate fire","Второй огонь");
        case TouchLayout.JUMP:return deploys()?(vehicleFlags&VEHICLE_DEPLOYED)!=0?text("Undeploy","Свернуть"):text("Deploy","Развернуть"):
            p==2?text("Up","Вверх"):text("Jump","Прыжок");
        case TouchLayout.DUCK:return p==2?text("Down","Вниз"):p==1?text("Brake","Тормоз"):text("Crouch","Присесть");
        case TouchLayout.USE:return p>0?text("Exit","Выйти"):text("Use","Действие");
        case TouchLayout.WEAPON:return p==0?text("Weapon","Оружие"):text("Seats","Места");
        case TouchLayout.BOARD:return text("Hoverboard / translocator","Ховерборд / транслокатор");
        case TouchLayout.MORE:return text("More","Ещё");
        case TouchLayout.PAUSE:return text("Pause","Пауза");
        case TouchLayout.SCORES:return text("Scores","Счёт");
        case TouchLayout.SETTINGS:return text("Android settings","Настройки Android");
        case TouchLayout.KEYBOARD:return text("Keyboard","Клавиатура");
        default:return playing?text("Menu","Меню"):text("Play","Игра");
        }
    }
    private String accessibilityLabel(int control) {
        switch(control) {
        case TouchLayout.WEAPON:return profile==0&&!editing?
            text("Weapon: tap for next, slide sideways along the weapon bar and release to switch, hold for the wheel",
                "Оружие: касание — следующее, ведите вбок по полосе оружия и отпустите — смена, удержание — колесо оружия"):label(control);
        case TouchLayout.SCORES:return text("Scores: hold to view, tap to keep shown","Счёт: удерживайте для просмотра, касание закрепляет");
        case TouchLayout.MORE:return text("More actions","Ещё действия");
        default:return label(control);
        }
    }

    private void buildLayout() {
        layoutDirty=false;occupied.clear();reserved.clear();
        layoutScale=TouchLayout.fitScale(scale,height()/dp);
        boolean gameplay=editing||playing;
        // The original HUD covers the whole surface; insets only bound the controls.
        if(gameplay)TouchLayout.reserveHud(0,0,getWidth(),getHeight(),uiScale,reserved);
        // The minimap corner is free while the original HUD draws no map (DM, CTF...).
        if(gameplay&&authoritative&&!editing&&(weaponSlots&HUD_MAP)==0)reserved.remove(TouchLayout.HUD_MINIMAP);
        float left=safe.left,top=safe.top,right=getWidth()-safe.right,bottom=getHeight()-safe.bottom;
        // The combat cluster keeps its shape: one shared shift clears the HUD,
        // unless the player placed one of its members by hand.
        float shiftX=0,shiftY=0;
        if(gameplay) {
            boolean custom=false;ArrayList<float[]> squares=new ArrayList<>();
            for(int control:TouchLayout.CLUSTER) {
                custom|=!Float.isNaN(customX[control]);
                if(!visible(control))continue;
                float size=TouchLayout.size(control,dp,layoutScale);
                float[] at=TouchLayout.anchor(control,left,top,right,bottom,dp,layoutScale,leftHanded);
                squares.add(new float[]{at[0]-size*.5f,at[1]-size*.5f,at[0]+size*.5f,at[1]+size*.5f});
            }
            float[] shift=custom?null:TouchLayout.clusterShift(squares,left,top,right,bottom,reserved,4*dp,4*dp,leftHanded);
            if(shift==null&&!custom&&reserved.size()>TouchLayout.HUD_MINIMAP) {
                // Crowded by a large HUD: the vehicle-only minimap's corner may be shared before the shape breaks.
                ArrayList<float[]> panels=new ArrayList<>(reserved);panels.remove(TouchLayout.HUD_MINIMAP);
                shift=TouchLayout.clusterShift(squares,left,top,right,bottom,panels,4*dp,4*dp,leftHanded);
                if(shift!=null)reserved.remove(TouchLayout.HUD_MINIMAP);
            }
            if(shift!=null){shiftX=shift[0];shiftY=shift[1];}
        }
        for(int pass=0;pass<2;++pass)for(int control:TouchLayout.ORDER) {
            boolean custom=gameplay&&!Float.isNaN(customX[control]);
            if(custom!=(pass==0))continue;
            shown[control]=visible(control);
            if(!shown[control])continue;
            float size=TouchLayout.size(control,dp,layoutScale);
            float[] at=TouchLayout.anchor(control,left,top,right,bottom,dp,layoutScale,leftHanded);
            if(custom) {
                float nx=leftHanded&&TouchLayout.mirrored(control)?1-customX[control]:customX[control];
                at[0]=left+nx*width();at[1]=top+customY[control]*height();
            } else if(TouchLayout.clustered(control)) { at[0]+=shiftX;at[1]+=shiftY; }
            // A player's own placement is kept as dropped; defaults avoid it and the HUD.
            float[] r=custom?TouchLayout.place(at[0],at[1],size,left,top,right,bottom,4*dp,0,null,null):
                TouchLayout.place(at[0],at[1],size,left,top,right,bottom,4*dp,4*dp,occupied,gameplay?reserved:null);
            if(r==null){shown[control]=false;continue;}
            occupied.add(r);
            controlX[control]=(r[0]+r[2])*.5f;controlY[control]=(r[1]+r[3])*.5f;controlR[control]=size*.5f;
        }
        // The W/A/S/D cross takes the best room left in its thumb's half, near where the stick
        // rests, with smaller cells on a crowded short screen; without room the stick stays.
        dpadShown=false;
        if(wasd&&playing&&!editing) {
            float mid=left+(right-left)*.5f,from=leftHanded?mid:left,to=leftHanded?right:mid;
            float[] at=TouchLayout.dpadAnchor(left,top,right,bottom,dp,layoutScale,leftHanded),cross=null;
            for(float cell=TouchLayout.dpadCell(dp,layoutScale);cross==null&&cell>=TouchLayout.MIN_DP*dp-.01f;cell-=4*dp) {
                cross=TouchLayout.place(at[0],at[1],cell*3,from,top,to,bottom,4*dp,4*dp,occupied,reserved);
                dpadCell=cell;
            }
            if(cross!=null){occupied.add(cross);dpadX=(cross[0]+cross[2])*.5f;dpadY=(cross[1]+cross[3])*.5f;dpadShown=true;}
        }
        if(editing)buildTools();
    }
    private void buildTools() {
        paint.setTextSize(14*dp);
        String[] names={text("Done","Готово"),text("Reset","Сброс")};
        float gap=10*dp,h=40*dp,total=-gap;float[] widths=new float[2];
        for(int i=0;i<2;++i){widths[i]=Math.max(96*dp,paint.measureText(names[i])+40*dp);total+=widths[i]+gap;}
        float x=safe.left+(width()-total)*.5f,y=safe.top+10*dp;
        for(int i=0;i<2;++i){tools[i]=new float[]{x,y,x+widths[i],y+h};x+=widths[i]+gap;}
    }
    private int hit(float x,float y) {
        if(layoutDirty)buildLayout();
        int best=-1;float bestDistance=Float.POSITIVE_INFINITY;
        for(int c=0;c<TouchLayout.COUNT;++c) {
            if(!shown[c])continue;
            // A small slop helps fingertips; the nearest centre wins between neighbours.
            float distance=(float)Math.hypot(x-controlX[c],y-controlY[c]);
            if(distance<=controlR[c]+4*dp&&distance-controlR[c]<bestDistance){best=c;bestDistance=distance-controlR[c];}
        }
        return best;
    }
    private boolean inside(int control,float x,float y) {
        return control>=0&&shown[control]&&Math.hypot(x-controlX[control],y-controlY[control])<=controlR[control]+12*dp;
    }
    private boolean movementZone(float x,float y) {
        float nx=(x-safe.left)/width(),ny=(y-safe.top)/height();
        return (leftHanded?nx>.6f:nx<.4f)&&ny>.35f;
    }
    private void haptic(int type) { performHapticFeedback(type); }

    @Override protected void onDraw(Canvas canvas) {
        if(layoutDirty)buildLayout();
        if(editing) {
            canvas.drawColor(0x8C0B0D10);
            paint.setStyle(Paint.Style.FILL);
        }
        if(playing||editing)drawStick(canvas);
        for(int c=0;c<TouchLayout.COUNT;++c)if(shown[c])drawControl(canvas,c);
        if(editing)drawTools(canvas);
        if(pieOpen)drawPie(canvas);
        else drawHint(canvas);
    }
    private boolean pressed(int control) {
        int a=action(control);
        if(a>0&&(input.held()&a)!=0)return true;
        for(int c:utility)if(c==control)return true;
        if(a==WEAPON)return weaponId>=0||(scrubId>=0&&scrubButton);
        if((a==MORE||a==SEATS)&&pieOpen&&pieSource==control)return true;
        return editing&&control==editControl;
    }
    private void drawControl(Canvas canvas,int c) {
        float x=controlX[c],y=controlY[c],r=controlR[c];
        boolean active=pressed(c);
        float level=active||editing?Math.max(.82f,opacity):opacity;
        paint.setStyle(Paint.Style.FILL);
        if(active) {
            paint.setColor(0x55000000|(ACCENT&0xFFFFFF));
            canvas.drawCircle(x,y,r+3*dp,paint);
        }
        paint.setColor(((int)(255*level)<<24)|TouchIcons.BUTTON_RGB);canvas.drawCircle(x,y,r,paint);
        paint.setStyle(Paint.Style.STROKE);paint.setStrokeWidth((active?2.4f:1.2f)*dp);
        paint.setColor(active?ACCENT:((int)(110*level)<<24)|0xFFFFFF);
        canvas.drawCircle(x,y,r-paint.getStrokeWidth()*.5f,paint);
        int ink=active?ACCENT:((int)(255*Math.min(1,.35f+level))<<24)|0xFFFFFF;
        TouchIcons.draw(canvas,paint,icon(c),x,y,Math.min(34*dp,r*1.05f),ink);
        if(c==TouchLayout.SCORES&&input.latched(TouchInput.SCORES)) {
            paint.setStyle(Paint.Style.FILL);paint.setColor(ACCENT);canvas.drawCircle(x+r*.7f,y-r*.7f,3.5f*dp,paint);
        }
        if(editing)drawText(canvas,label(c),x,y+r+11*dp,11*dp,Math.max(2*r+24*dp,80*dp),0xFFFFFFFF);
    }
    private void drawStick(Canvas canvas) {
        if(editing)return;
        if(dpadShown){drawDpad(canvas);return;}
        float radius=60*layoutScale*dp,knob=22*layoutScale*dp;
        boolean moving=input.moving();
        float ax=moving?input.anchorX():safe.left+width()*(leftHanded?.80f:.20f);
        float ay=moving?input.anchorY():safe.top+height()*.70f;
        float alpha=moving?Math.max(.45f,opacity):opacity*.22f;
        paint.setStyle(Paint.Style.STROKE);paint.setStrokeWidth(2*dp);
        paint.setColor(((int)(150*alpha)<<24)|0xFFFFFF);
        canvas.drawCircle(ax,ay,radius,paint);
        paint.setStyle(Paint.Style.FILL);
        paint.setColor(((int)(140*alpha)<<24)|0xFFFFFF);
        canvas.drawCircle(ax+input.moveX()*42*layoutScale*dp,ay-input.moveY()*42*layoutScale*dp,knob,paint);
    }
    /** The W/A/S/D cross; the keys of the current direction are lit. */
    private void drawDpad(Canvas canvas) {
        boolean moving=input.padActive();
        float mx=moving?input.moveX():0,my=moving?input.moveY():0,c=dpadCell,h=c*.46f;
        String[] keys={"W","A","S","D"};
        float[][] cells={{0,-1},{-1,0},{0,1},{1,0}};
        boolean[] lit={my>.3f,mx<-.3f,my<-.3f,mx>.3f};
        for(int i=0;i<4;++i) {
            float x=dpadX+cells[i][0]*c,y=dpadY+cells[i][1]*c;
            float level=lit[i]?Math.max(.82f,opacity):opacity;
            paint.setStyle(Paint.Style.FILL);
            paint.setColor(lit[i]?0x55000000|(ACCENT&0xFFFFFF):((int)(255*level)<<24)|TouchIcons.BUTTON_RGB);
            canvas.drawRoundRect(x-h,y-h,x+h,y+h,c*.18f,c*.18f,paint);
            paint.setStyle(Paint.Style.STROKE);paint.setStrokeWidth((lit[i]?2.4f:1.2f)*dp);
            paint.setColor(lit[i]?ACCENT:((int)(110*level)<<24)|0xFFFFFF);
            canvas.drawRoundRect(x-h,y-h,x+h,y+h,c*.18f,c*.18f,paint);
            drawText(canvas,keys[i],x,y,Math.min(22*dp,c*.42f),c*.8f,lit[i]?ACCENT:((int)(255*Math.min(1,.35f+level))<<24)|0xFFFFFF);
        }
    }
    private boolean inDpad(float x,float y) {
        return dpadShown&&Math.abs(x-dpadX)<=dpadCell*1.5f&&Math.abs(y-dpadY)<=dpadCell*1.5f;
    }
    private void drawText(Canvas canvas,String value,float x,float y,float size,float maxWidth,int color) {
        if(value==null||value.isEmpty()||!(maxWidth>0))return;
        paint.setStyle(Paint.Style.FILL);paint.setColor(color);paint.setTextAlign(Paint.Align.CENTER);
        paint.setTextSize(size);
        float measured=paint.measureText(value);
        if(measured>maxWidth)paint.setTextSize(size*maxWidth/measured);
        Paint.FontMetrics metrics=paint.getFontMetrics();
        canvas.drawText(value,x,y-(metrics.ascent+metrics.descent)*.5f,paint);
    }
    private void drawPill(Canvas canvas,float[] r,String value,boolean active) {
        paint.setStyle(Paint.Style.FILL);
        paint.setColor(active?ACCENT:0xE6101317);
        float h=r[3]-r[1];
        canvas.drawRoundRect(r[0],r[1],r[2],r[3],h*.5f,h*.5f,paint);
        drawText(canvas,value,(r[0]+r[2])*.5f,(r[1]+r[3])*.5f,Math.min(14*dp,h*.42f),r[2]-r[0]-16*dp,active?0xFF101317:0xFFFFFFFF);
    }
    private void drawTools(Canvas canvas) {
        if(tools[0]==null)return;
        boolean armed=SystemClock.uptimeMillis()<resetDeadline;
        drawPill(canvas,tools[TOOL_DONE],text("Done","Готово"),true);
        drawPill(canvas,tools[TOOL_RESET],armed?text("Reset?","Сбросить?"):text("Reset","Сброс"),armed||toolPressed==TOOL_RESET);
        drawText(canvas,text("Drag controls to move them · Reset twice restores the original layout",
                "Перетаскивайте кнопки · «Сброс» дважды возвращает исходную раскладку"),
            getWidth()*.5f,tools[0][3]+20*dp,13*dp,width()-32*dp,0xFFFFFFFF);
    }
    private void drawHint(Canvas canvas) {
        if(hintText==null)return;
        if(SystemClock.uptimeMillis()>=hintUntil){hintText=null;return;}
        paint.setTextSize(14*dp);
        float widthLimit=width()*.72f,measured=Math.min(paint.measureText(hintText),widthLimit);
        // Between the original centre messages (about a quarter down) and the crosshair.
        float cy=getHeight()*.37f,cx=getWidth()*.5f;
        float[] r={cx-measured*.5f-18*dp,cy-18*dp,cx+measured*.5f+18*dp,cy+18*dp};
        paint.setStyle(Paint.Style.FILL);paint.setColor(0xD0101317);
        canvas.drawRoundRect(r[0],r[1],r[2],r[3],18*dp,18*dp,paint);
        drawText(canvas,hintText,cx,cy,14*dp,widthLimit,0xFFFFFFFF);
    }

    // ---------------------------------------------------------------- touch
    @Override public boolean onTouchEvent(MotionEvent event) {
        syncContext();
        int action=event.getActionMasked(),index=event.getActionIndex();
        if(action==MotionEvent.ACTION_CANCEL) { movieTouch=false;releaseInput();return true; }
        // A tap skips the movie the engine shows. Over a menu the whole gesture is the movie's, as the
        // original's click is: the menu's own tap waits for a pointer the menu does not move meanwhile.
        if(action==MotionEvent.ACTION_DOWN&&GameActivity.nativeMoviePlaying()) {
            GameActivity.nativeSkipMovie();
            movieTouch=(cursorContext&3)==3;
            android.util.Log.i("UT3","tap skips the movie"+(movieTouch?" over a menu":""));
        }
        if(movieTouch) { if(action==MotionEvent.ACTION_UP)movieTouch=false;return true; }
        if((cursorContext&3)==1)return true;
        if(editing)return editTouch(event);
        int id=event.getPointerId(index);float x=event.getX(index),y=event.getY(index);long time=event.getEventTime();
        if(action==MotionEvent.ACTION_DOWN||action==MotionEvent.ACTION_POINTER_DOWN) {
            pointerDown(id,x,y,time);invalidate();
        } else if(action==MotionEvent.ACTION_MOVE) {
            visualState(visualBefore);
            for(int sample=0;sample<=event.getHistorySize();++sample)for(int p=0;p<event.getPointerCount();++p) {
                boolean history=sample<event.getHistorySize();
                pointerMove(event.getPointerId(p),history?event.getHistoricalX(p,sample):event.getX(p),
                    history?event.getHistoricalY(p,sample):event.getY(p),history?event.getHistoricalEventTime(sample):time);
            }
            // A camera drag redraws nothing: this layer composes over the game every frame it changes.
            visualState(visualAfter);
            if(!java.util.Arrays.equals(visualBefore,visualAfter))invalidate();
        } else if(action==MotionEvent.ACTION_UP||action==MotionEvent.ACTION_POINTER_UP) {
            pointerUp(id,x,y,time);invalidate();
        }
        return true;
    }
    /** The original weapon bar's settled rectangle and card pitch, while it is shown on foot. */
    private float[] weaponBar() {
        if(!playing||editing||(weaponSlots&BAR_SHOWN)==0)return null;
        return TouchLayout.weaponBar(0,0,getWidth(),getHeight(),uiScale,Integer.bitCount(weaponSlots&1023));
    }
    private boolean weaponBarHit(float x,float y) {
        float[] bar=weaponBar();
        // Down to the screen edge: a thumb on the bottom row still reaches the cards.
        return bar!=null&&x>=bar[0]-8*dp&&x<=bar[2]+8*dp&&y>=bar[1]-8*dp;
    }
    private final int[] visualBefore=new int[5],visualAfter=new int[5];
    /** What a move can change on screen: held actions, stick knob, pie selection, picker. */
    private void visualState(int[] out) {
        boolean moving=input.moving();
        out[0]=input.held();
        out[1]=moving?Math.round(input.moveX()*24):Integer.MIN_VALUE;
        out[2]=moving?Math.round(input.moveY()*24):0;
        out[3]=pieOpen?pieSelected:-3;
        out[4]=weaponPicker?1:0;
    }
    private void pointerDown(int id,float x,float y,long time) {
        if(pieOpen&&pieDown(id,x,y))return;
        int control=hit(x,y);
        if(control>=0) {
            int a=action(control);
            if(a==MORE||a==SEATS) { openPie(id,x,y,a==MORE?PIE_ACTIONS:PIE_SEATS,control);return; }
            if(a==WEAPON) { weaponDown(id,x,y,time);return; }
            if(a==TouchInput.SCORES) { scoresDown(id,x,y,time);return; }
            if(a<0||a==TouchInput.PAUSE) {
                // Utilities act on release over the button, so a brushing thumb can slide off.
                for(int i=0;i<utility.length;++i)if(utility[i]<0){utility[i]=control;utilityIds[i]=id;break;}
                haptic(HapticFeedbackConstants.VIRTUAL_KEY);
                return;
            }
            // The left Fire only shoots: the right thumb keeps the camera.
            if(playing&&input.down(id,x,y,a,control==TouchLayout.FIRE2?TouchInput.PASSIVE:TouchInput.BUTTON,time)) {
                haptic(HapticFeedbackConstants.VIRTUAL_KEY);
                if(a==TouchInput.DUCK&&profile==0)offer(HINT_CROUCH,"Double-tap Crouch to stay crouched","Двойное касание «Присесть» — сидеть без удержания");
            }
            return;
        }
        if(!playing) { uiDown(id,x,y);return; }
        if(dpadId<0&&inDpad(x,y)) {
            if(input.pad(id,x,y,dpadX,dpadY,dpadCell*.5f,time)){dpadId=id;haptic(HapticFeedbackConstants.VIRTUAL_KEY);}
            return;
        }
        if(scrubId<0&&weaponBarHit(x,y)&&input.down(id,x,y,0,TouchInput.PASSIVE,time)) {
            if(!scrubStart(id,x,false))input.up(id,x,y,time);
            return;
        }
        // With the W/A/S/D cross shown, the rest of the screen only looks.
        input.down(id,x,y,0,!dpadShown&&movementZone(x,y)?TouchInput.MOVEMENT:TouchInput.LOOK,time);
    }
    private int utilitySlot(int id) {
        for(int i=0;i<utility.length;++i)if(utility[i]>=0&&utilityIds[i]==id)return i;
        return -1;
    }
    private void pointerMove(int id,float x,float y,long time) {
        if(id==swallowed)return;
        if(pieOpen&&id==pieId) { pieMove(x,y);return; }
        if(id==scrubId) { scrubMove(x);return; }
        if(id==weaponId) { weaponMove(x,y);return; }
        if(utilitySlot(id)>=0||id==scoresId)return;
        if(id==menuId) {
            if(uiInput.active())uiInput.move(x,y);
            else if(!authoritative||(cursorContext&3)!=3)uiPointer(x,y);
            return;
        }
        if(playing)input.move(id,x,y,time);
    }
    private void pointerUp(int id,float x,float y,long time) {
        if(id==swallowed) { swallowed=-1;return; }
        if(id==dpadId)dpadId=-1;
        if(pieOpen&&id==pieId) { pieUp(x,y);return; }
        if(id==scrubId) { scrubUp(x,y,time);return; }
        if(id==weaponId) { weaponUp(x,y,time);return; }
        if(id==scoresId) { scoresUp(x,y,time);return; }
        int slot=utilitySlot(id);
        if(slot>=0) {
            int control=utility[slot];utility[slot]=-1;
            if(inside(control,x,y))activate(control);
            return;
        }
        if(id==menuId) {
            if(uiInput.active())uiInput.end(x,y);
            else if(!authoritative||(cursorContext&3)!=3) {
                uiPointer(x,y);
                if(!GameActivity.nativeTouch(0,0,0,0,0,cursorContext))syncContext();
            }
            menuId=-1;performClick();
            return;
        }
        if(playing)input.up(id,x,y,time);
    }
    private void uiDown(int id,float x,float y) {
        if(menuId>=0||uiInput.active())return;
        hideKeyboard();
        if(authoritative&&(cursorContext&3)==3) {
            if(uiInput.begin(x,y)) {
                menuId=id;
                if(uiInput.polling()){removeCallbacks(uiPoll);postOnAnimation(uiPoll);}
            } else syncContext();
        } else if(uiPointer(x,y)) {
            if(GameActivity.nativeTouch(0,0,0,0,TouchInput.FIRE,cursorContext))menuId=id;
            else syncContext();
        }
    }
    private void activate(int control) {
        switch(action(control)) {
        case MODE:mode(!playing);break;
        case KEYBOARD:showKeyboard();break;
        case SETTINGS:release();if(settingsAction!=null)settingsAction.run();break;
        case TouchInput.PAUSE:
            // Escape's own release opens the original menu (GBA_ShowMenu: onrelease ShowMenu).
            release();input.pulse(TouchInput.PAUSE);
            if(!authoritative){playing=false;pausedByControl=true;layoutDirty=true;}
            updateGyro();break;
        default:break;
        }
        invalidate();
    }

    // ------------------------------------------------- weapon button gestures
    private void weaponDown(int id,float x,float y,long time) {
        if(weaponId>=0||!input.down(id,x,y,0,TouchInput.PASSIVE,time))return;
        weaponId=id;weaponPicker=false;weaponX=weaponLastX=x;weaponY=weaponLastY=y;
        postDelayed(weaponHold,PICK_HOLD_MS);
        haptic(HapticFeedbackConstants.VIRTUAL_KEY);
    }
    private void startPicker() {
        // The original GBA_WeaponPicker shows UTHUD's quick pick; drag toward a weapon.
        if(!input.hold(weaponId,TouchInput.PICKER)) { cancelWeapon();return; }
        weaponPicker=true;haptic(HapticFeedbackConstants.LONG_PRESS);
        weaponStick(weaponLastX,weaponLastY);invalidate();
    }
    private void weaponMove(float x,float y) {
        weaponLastX=x;weaponLastY=y;
        if(weaponPicker) { weaponStick(x,y);return; }
        // A sideways slide walks the original weapon bar instead of waiting for the wheel.
        float dx=x-weaponX,dy=y-weaponY;
        if(Math.abs(dx)>ViewConfiguration.get(getContext()).getScaledTouchSlop()&&Math.abs(dx)>Math.abs(dy)*1.2f&&
           (weaponSlots&BAR_SHOWN)!=0&&scrubId<0) {
            int id=weaponId;
            removeCallbacks(weaponHold);
            if(scrubStart(id,weaponX,true)) { weaponId=-1;scrubMove(x);invalidate(); }
        }
    }
    private void weaponStick(float x,float y) {
        float dx=x-weaponX,dy=y-weaponY,distance=(float)Math.hypot(dx,dy);
        boolean accepted=distance<16*dp?input.stick(0,0):input.stick(dx/distance,-dy/distance);
        if(!accepted)cancelWeapon();
    }
    private void weaponUp(float x,float y,long time) {
        removeCallbacks(weaponHold);
        int id=weaponId;weaponId=-1;
        if(weaponPicker) {
            weaponPicker=false;input.stick(0,0);input.up(id,x,y,time);return;
        }
        input.up(id,x,y,time);
        float dx=x-weaponX,dy=y-weaponY;
        if(Math.abs(dx)>=24*dp&&Math.abs(dx)>Math.abs(dy)*1.4f)input.pulse(dx<0?TouchInput.PREVIOUS:TouchInput.NEXT);
        else if(Math.hypot(dx,dy)<=ViewConfiguration.get(getContext()).getScaledTouchSlop()*1.5f)input.pulse(TouchInput.NEXT);
    }
    /** Begin a swipe on the original bar at the selected weapon; false when the bar is not shown. */
    private boolean scrubStart(int id,float x,boolean fromButton) {
        int mask=weaponSlots,selected=(mask>>10)&15,start=-1;
        int[] slots=TouchLayout.weaponSlots(mask);
        for(int i=0;i<slots.length;++i)if(slots[i]==selected)start=i;
        if((mask&BAR_SHOWN)==0||slots.length<2||start<0)return false;
        float[] bar=weaponBar();
        scrubId=id;scrubSlots=slots;scrubStart=scrubIndex=start;scrubX=x;scrubButton=fromButton;
        scrubStep=clamp(bar!=null?bar[4]:48*dp,36*dp,64*dp);
        if(!fromButton)haptic(HapticFeedbackConstants.VIRTUAL_KEY);
        return true;
    }
    private void scrubMove(float x) {
        int next=TouchLayout.weaponIndex(scrubStart,x-scrubX,scrubStep,scrubSlots.length);
        if(scrubId<0||next==scrubIndex)return;
        scrubIndex=next;
        // The original bar enlarges the card and names it; nothing switches until release.
        if(!GameActivity.nativeTouchWeapon(1,scrubSlots[next],cursorContext)) { scrubId=-1;input.cancel();syncContext();return; }
        weaponTick();
    }
    private void scrubUp(float x,float y,long time) {
        scrubMove(x);
        int id=scrubId;scrubId=-1;
        if(id<0)return;
        boolean changed=scrubIndex!=scrubStart;
        GameActivity.nativeTouchWeapon(changed?2:3,scrubSlots[scrubIndex],cursorContext);
        if(changed)haptic(HapticFeedbackConstants.VIRTUAL_KEY);
        input.up(id,x,y,time);
        if(changed&&!scrubButton)markHint(HINT_WEAPON);
    }
    /** A short vibration for each weapon a swipe passes. */
    private void weaponTick() {
        try {
            if(vibrator!=null&&vibrator.hasVibrator()) {
                vibrator.vibrate(VibrationEffect.createPredefined(VibrationEffect.EFFECT_TICK));return;
            }
        } catch(RuntimeException ignored) { }
        haptic(HapticFeedbackConstants.CLOCK_TICK);
    }
    private void cancelWeapon() {
        removeCallbacks(weaponHold);
        if(weaponId>=0){input.stick(0,0);input.up(weaponId,weaponX,weaponY);}
        weaponId=-1;weaponPicker=false;invalidate();
    }

    // ----------------------------------------------------------------- scores
    private void scoresDown(int id,float x,float y,long time) {
        if(scoresId>=0)return;
        haptic(HapticFeedbackConstants.VIRTUAL_KEY);
        if(input.latched(TouchInput.SCORES)) { input.toggle(TouchInput.SCORES);swallowed=id;return; }
        if(input.down(id,x,y,TouchInput.SCORES,TouchInput.PASSIVE,time)){scoresId=id;scoresTime=SystemClock.uptimeMillis();}
    }
    private void scoresUp(float x,float y,long time) {
        int id=scoresId;scoresId=-1;
        // A tap keeps the original scoreboard open: latch before the finger's hold ends.
        if(SystemClock.uptimeMillis()-scoresTime<SCORES_TAP_MS)input.toggle(TouchInput.SCORES);
        input.up(id,x,y,time);
    }

    // -------------------------------------------------------------------- pie
    private int[] pieItems(int kind) {
        if(kind==PIE_SEATS) {
            int[] seats=new int[SEAT_COUNT];
            for(int i=0;i<SEAT_COUNT;++i)seats[i]=TouchInput.WEAPON1<<i;
            return seats;
        }
        switch(profile) {
        case 0:return new int[]{TouchInput.MAP,TouchInput.COMMAND,TouchInput.BEST,TouchInput.FEIGN,
            TouchInput.TAUNT,TouchInput.HERO,SETTINGS};
        case 3:return new int[]{TouchInput.MAP,TouchInput.COMMAND,TouchInput.TAUNT,SETTINGS};
        default:return new int[]{TouchInput.HORN,TouchInput.MAP,TouchInput.COMMAND,TouchInput.TAUNT,SETTINGS};
        }
    }
    private int pieIcon(int item) {
        switch(item) {
        case TouchInput.MAP:return TouchIcons.MAP;
        case TouchInput.COMMAND:return TouchIcons.ORDERS;
        case TouchInput.BEST:return TouchIcons.BEST;
        case TouchInput.FEIGN:return TouchIcons.FEIGN;
        case TouchInput.TAUNT:return TouchIcons.TAUNT;
        case TouchInput.HERO:return TouchIcons.TITAN;
        case TouchInput.HORN:return TouchIcons.HORN;
        case SETTINGS:return TouchIcons.SETTINGS;
        default:return 0;
        }
    }
    /** Short caption inside a sector, or the full name in the pie's centre. */
    private String pieLabel(int item,boolean full) {
        for(int i=0;i<SEAT_COUNT;++i)if(item==TouchInput.WEAPON1<<i)return full?text("Seat ","Место ")+(i+1):Integer.toString(i+1);
        switch(item) {
        case TouchInput.MAP:return text("Map","Карта");
        case TouchInput.COMMAND:return full?text("Orders to bots","Приказы ботам"):text("Orders","Приказы");
        case TouchInput.BEST:return full?text("Best weapon","Лучшее оружие"):text("Best","Лучшее");
        case TouchInput.FEIGN:return full?text("Feign death","Притвориться мёртвым"):text("Feign","Притвор.");
        case TouchInput.TAUNT:return text("Taunt","Насмешка");
        case TouchInput.HERO:return full?text("Become the Titan","Стать Титаном"):text("Titan","Титан");
        case TouchInput.HORN:return text("Horn","Сигнал");
        case SETTINGS:return full?text("Android settings","Настройки Android"):text("Settings","Настройки");
        default:return "";
        }
    }
    private void openPie(int id,float x,float y,int kind,int source) {
        closePie();
        pieItems=pieItems(kind);pieKind=kind;pieSource=source;
        pieOpen=true;pieId=id;pieSelected=-1;pieDragged=pieSticky=false;pieStartX=x;pieStartY=y;
        pieR=pieRadius(pieItems.length);
        pieX=clamp(x,safe.left+pieR,Math.max(safe.left+pieR,getWidth()-safe.right-pieR));
        pieY=clamp(y,safe.top+pieR,Math.max(safe.top+pieR,getHeight()-safe.bottom-pieR));
        haptic(HapticFeedbackConstants.VIRTUAL_KEY);
        // The first time, the pie's own title teaches the faster hold-and-slide gesture.
        pieHint=kind==PIE_ACTIONS&&(hintsSeen&HINT_MORE)==0?text("Hold More and slide toward an action",
            "Удерживайте «Ещё» и проведите к действию"):null;
        if(pieHint!=null)markHint(HINT_MORE);
        invalidate();
    }
    /** Radius of the item icons: the middle of each sector's usable depth. */
    private float pieRing() { return pieR*.70f; }
    private float pieRadius(int count) {
        return Math.min((count>5?128:118)*dp*Math.max(1,layoutScale),Math.min(height(),width())*.45f);
    }
    /**
     * Caption room in a sector: the chord at the inner edge of a caption under
     * the top icon, the narrowest place, so every caption keeps one size and
     * stays inside its own sector.
     */
    private float pieCaptionWidth(int count) {
        float edge=pieRing()-20*dp;
        return Math.min((float)(2*edge*Math.sin(Math.PI/Math.max(2,count)))-6*dp,pieR*.64f-6*dp);
    }
    private void closePie() {
        if(!pieOpen)return;
        pieOpen=false;pieId=-1;pieSelected=-1;pieSource=-1;invalidate();
    }
    /** True when the pie consumed this new contact. */
    private boolean pieDown(int id,float x,float y) {
        if(pieId>=0)return false; // Another finger already drags it: other contacts keep playing.
        int hit=TouchLayout.pieHit(x,y,pieX,pieY,pieR*.36f,pieR,pieItems.length);
        if(hit==-1) {
            // A kept-open pie is dismissed by the next touch elsewhere, which does nothing else.
            closePie();swallowed=id;return true;
        }
        pieId=id;pieSticky=true;pieSelected=hit>=0?hit:-1;invalidate();
        return true;
    }
    private void pieMove(float x,float y) {
        int next;
        if(pieSticky)next=TouchLayout.pieHit(x,y,pieX,pieY,pieR*.36f,pieR,pieItems.length);
        else {
            if(Math.hypot(x-pieStartX,y-pieStartY)>12*dp)pieDragged=true;
            // Marking-menu direction from the finger, wherever the clamped pie is drawn.
            next=pieDragged?TouchLayout.sector(x-pieStartX,y-pieStartY,pieItems.length,24*dp):-1;
        }
        if(next<0)next=-1;
        if(next!=pieSelected) {
            pieSelected=next;
            if(next>=0)haptic(HapticFeedbackConstants.CLOCK_TICK);
        }
    }
    private void pieUp(float x,float y) {
        pieMove(x,y);
        if(!pieSticky&&!pieDragged) {
            // A tap keeps the pie open for a second tap on an item.
            pieSticky=true;pieId=-1;pieSelected=-1;invalidate();return;
        }
        int selected=pieSelected;
        boolean center=pieSticky&&TouchLayout.pieHit(x,y,pieX,pieY,pieR*.36f,pieR,pieItems.length)==TouchLayout.CENTER;
        if(selected>=0||center||!pieSticky) {
            int item=selected>=0?pieItems[selected]:0;
            closePie();
            if(item!=0)pieSelect(item);
        } else { pieId=-1;pieSelected=-1; }
        invalidate();
    }
    private void pieSelect(int item) {
        haptic(HapticFeedbackConstants.VIRTUAL_KEY);
        if(item==SETTINGS){release();if(settingsAction!=null)settingsAction.run();return;}
        input.pulse(item);
    }
    private void drawPie(Canvas canvas) {
        int n=pieItems.length;
        if(n==0)return;
        float r=pieR,inner=r*.36f,sweep=360f/n;
        paint.setStyle(Paint.Style.FILL);paint.setColor(0xE8101317);canvas.drawCircle(pieX,pieY,r,paint);
        if(pieSelected>=0) {
            paint.setColor(0xF0000000|(ACCENT&0xFFFFFF));
            canvas.drawArc(pieX-r,pieY-r,pieX+r,pieY+r,-90-sweep*.5f+pieSelected*sweep,sweep,true,paint);
        }
        paint.setStyle(Paint.Style.STROKE);paint.setStrokeWidth(1.5f*dp);paint.setColor(0x40FFFFFF);
        for(int i=0;i<n;++i) {
            double a=Math.toRadians(-90-sweep*.5f+i*sweep);
            canvas.drawLine(pieX+(float)Math.cos(a)*inner,pieY+(float)Math.sin(a)*inner,
                pieX+(float)Math.cos(a)*r,pieY+(float)Math.sin(a)*r,paint);
        }
        canvas.drawCircle(pieX,pieY,r-.75f*dp,paint);
        paint.setStyle(Paint.Style.FILL);paint.setColor(0xF5181C22);canvas.drawCircle(pieX,pieY,inner,paint);
        float ring=pieRing(),caption=pieCaptionWidth(n);
        for(int i=0;i<n;++i) {
            double a=TouchLayout.sectorAngle(i,n);
            float ix=pieX+(float)Math.sin(a)*ring,iy=pieY-(float)Math.cos(a)*ring;
            int ink=i==pieSelected?0xFF101317:0xFFFFFFFF;
            int icon=pieIcon(pieItems[i]);
            if(icon!=0) {
                TouchIcons.draw(canvas,paint,icon,ix,iy-8*dp,24*dp,ink);
                drawText(canvas,pieLabel(pieItems[i],false),ix,iy+14*dp,11*dp,caption,ink);
            } else drawText(canvas,pieLabel(pieItems[i],false),ix,iy,24*dp,caption,ink);
        }
        // The centre shows what a release does; the full name sits in a pill clear of the pie.
        int selected=pieSelected>=0?pieItems[pieSelected]:0,centreIcon=selected!=0?pieIcon(selected):TouchIcons.CLOSE;
        if(centreIcon!=0)TouchIcons.draw(canvas,paint,centreIcon,pieX,pieY,Math.min(30*dp,inner*1.1f),selected!=0?ACCENT:0xB0FFFFFF);
        else drawText(canvas,pieLabel(selected,false),pieX,pieY,24*dp,inner*1.6f,ACCENT);
        String title=selected!=0?pieLabel(selected,true):pieHint!=null?pieHint:
            pieKind==PIE_SEATS?text("Choose a seat","Выберите место"):text("Choose an action","Выберите действие");
        paint.setTextSize(14*dp);
        float titleWidth=Math.min(paint.measureText(title),pieTitleWidth());
        float ty=pieY-r-safe.top>=40*dp?pieY-r-22*dp:pieY+r+22*dp;
        float[] pill={pieX-titleWidth*.5f-16*dp,ty-16*dp,pieX+titleWidth*.5f+16*dp,ty+16*dp};
        paint.setStyle(Paint.Style.FILL);paint.setColor(0xE8101317);
        canvas.drawRoundRect(pill[0],pill[1],pill[2],pill[3],16*dp,16*dp,paint);
        drawText(canvas,title,pieX,ty,14*dp,pieTitleWidth(),selected!=0?ACCENT:0xFFFFFFFF);
    }
    private float pieTitleWidth() { return Math.min(pieR*2.4f,width()-32*dp); }

    // ------------------------------------------------------------------ hints
    private void offer(int kind,String english,String russian) {
        long now=SystemClock.uptimeMillis();
        if((hintsSeen&kind)!=0||(hintText!=null&&now<hintUntil))return;
        hintText=text(english,russian);hintUntil=now+HINT_MS;
        postInvalidateDelayed(HINT_MS+50);invalidate();
        markHint(kind);
    }
    private void markHint(int kind) {
        hintsSeen|=kind;
        PortPreferences p=preferences();
        if(p!=null){p.put("touchHints",hintsSeen);activity().savePortSettings(null);}
    }
    private void offerHint() {
        if(!playing||editing||!authoritative)return;
        if((hintsSeen&HINT_LOOK)==0)offer(HINT_LOOK,"Drag on the right to look · hold Fire and drag to aim while shooting",
            "Ведите пальцем справа — обзор · держите «Огонь» и ведите — стрельба с прицеливанием");
        else if(profile==0&&(hintsSeen&HINT_WEAPON)==0)offer(HINT_WEAPON,"Weapons: slide along the bar at the bottom and release to switch",
            "Оружие: ведите по полосе внизу и отпустите палец — смена оружия");
        else if(profile==0&&(hintsSeen&HINT_LEFT_FIRE)==0)offer(HINT_LEFT_FIRE,"The left Fire shoots while the right thumb aims",
            "Левая кнопка огня стреляет, пока правый палец целится");
        else if((vehicleFlags&VEHICLE_LOOK_STEER)!=0&&(hintsSeen&HINT_STEER)==0)offer(HINT_STEER,
            "This vehicle drives where you look · pull the stick back to reverse",
            "Техника едет туда, куда вы смотрите · стик назад — задний ход");
        else if(profile!=0&&profile!=3&&(hintsSeen&HINT_VEHICLE)==0)offer(HINT_VEHICLE,
            deploys()?"Vehicle: Deploy sets it up, Seats switches seats, Exit leaves":"Vehicle: Seats switches seats, Exit leaves",
            deploys()?"Техника: «Развернуть» — развернуть, «Места» — пересесть, «Выйти» — покинуть":"Техника: «Места» — пересесть, «Выйти» — покинуть");
    }

    // ----------------------------------------------------------------- editor
    private int toolHit(float x,float y) {
        for(int i=0;i<tools.length;++i) {
            float[] r=tools[i];
            if(r!=null&&x>=r[0]-6*dp&&x<=r[2]+6*dp&&y>=r[1]-6*dp&&y<=r[3]+6*dp)return i;
        }
        return -1;
    }
    private boolean editTouch(MotionEvent event) {
        int action=event.getActionMasked(),index=event.getActionIndex(),id=event.getPointerId(index);
        float x=event.getX(index),y=event.getY(index);
        if(layoutDirty)buildLayout();
        if(action==MotionEvent.ACTION_DOWN||action==MotionEvent.ACTION_POINTER_DOWN) {
            int tool=toolHit(x,y);
            if(tool>=0&&toolId<0){toolId=id;toolPressed=tool;}
            else if(editId<0) {
                int control=hit(x,y);
                if(control>=0){editId=id;editControl=control;editDX=controlX[control]-x;editDY=controlY[control]-y;
                    haptic(HapticFeedbackConstants.VIRTUAL_KEY);}
            }
        } else if(action==MotionEvent.ACTION_MOVE) {
            int pointer=editId<0?-1:event.findPointerIndex(editId);
            if(pointer>=0)dragTo(editControl,event.getX(pointer)+editDX,event.getY(pointer)+editDY);
        } else if(action==MotionEvent.ACTION_UP||action==MotionEvent.ACTION_POINTER_UP) {
            if(id==toolId) {
                int tool=toolPressed;toolId=toolPressed=-1;
                if(toolHit(x,y)==tool)tool(tool);
            }
            if(id==editId){editId=editControl=-1;layoutDirty=true;}
        }
        invalidate();return true;
    }
    private void dragTo(int control,float x,float y) {
        if(control<0)return;
        float nx=clamp((x-safe.left)/width(),0,1),ny=clamp((y-safe.top)/height(),0,1);
        customX[control]=leftHanded&&TouchLayout.mirrored(control)?1-nx:nx;customY[control]=ny;
        layoutDirty=true;
    }
    private void tool(int tool) {
        if(tool==TOOL_DONE)finishEditing();
        else if(tool==TOOL_RESET)resetLayout();
    }

    // ------------------------------------------------------------------- gyro
    private void updateGyro() {
        if(input==null)return;
        boolean active=focused&&isShown()&&playing&&!editing&&!pieOpen&&(gyroMode==GYRO_ALWAYS||
            (gyroMode==GYRO_FIRING&&(input.held()&(TouchInput.FIRE|TouchInput.ALT))!=0)||(gyroMode==GYRO_SNIPER&&scoped()));
        if(active!=gyroActive) { sensorTime=0;gyroActive=active; }
        if(active&&gyro!=null&&!gyroListening)gyroListening=sensors.registerListener(this,gyro,SensorManager.SENSOR_DELAY_GAME);
        else if(!active)stopGyro();
    }
    private void stopGyro() {
        if(gyroListening&&sensors!=null)sensors.unregisterListener(this);
        gyroListening=false;sensorTime=0;
    }
    @Override public void onAccuracyChanged(Sensor sensor,int accuracy) { }
    @Override public void onSensorChanged(SensorEvent event) {
        if(!gyroActive||event.sensor.getType()!=Sensor.TYPE_GYROSCOPE) { sensorTime=0;return; }
        Display display=getDisplay();int rotation=display==null?Surface.ROTATION_0:display.getRotation();
        long previous=sensorTime;sensorTime=event.timestamp;
        if(rotation!=sensorRotation) { sensorRotation=rotation;return; }
        double dt=(event.timestamp-previous)*1e-9;
        if(previous==0||dt<=0||dt>.1)return;
        float pitchRate,yawRate;
        switch(rotation) {
        case Surface.ROTATION_90:pitchRate=event.values[1];yawRate=-event.values[0];break;
        case Surface.ROTATION_180:pitchRate=-event.values[0];yawRate=-event.values[1];break;
        case Surface.ROTATION_270:pitchRate=-event.values[1];yawRate=event.values[0];break;
        default:pitchRate=event.values[0];yawRate=event.values[1];
        }
        // Counts per degree are calibrated independently of render FPS.
        input.relativeLook(-gyroDelta(yawRate,dt)*10*gyroSensitivity,gyroDelta(pitchRate,dt)*10*gyroSensitivity);
    }
    static float gyroDelta(float rate,double dt) {
        if(!Float.isFinite(rate)||dt<=0||dt>.1)return 0;
        float degrees=(float)(rate*180/Math.PI*dt),dead=(float)(.4*dt);
        return Math.copySign(Math.min(25,Math.max(0,Math.abs(degrees)-dead)),degrees);
    }

    // ---------------------------------------------------------------- gamepad
    /*
     * A physical gamepad is the game's own XInput controller 0 during play
     * (input.c), with the original UT3 controller bindings: the left stick
     * moves, the right stick looks, RT fires and LT fires the alternate mode,
     * A jumps, X uses, B switches to the melee weapon and crouches while held,
     * Y shows the map, LB holds the weapon picker (the right stick points it),
     * RB switches weapons, the left stick's click crouches and the right
     * stick's picks the best weapon, the D-pad gives orders (left), voice
     * (right) and feign death (down), Start pauses and Back holds the scores.
     * In the original menus the D-pad and the left stick are the arrow keys,
     * A is Enter, B and Start are Escape.
     */
    private static final int PAD_UP=0x0001,PAD_DOWN=0x0002,PAD_LEFT=0x0004,PAD_RIGHT=0x0008;
    /** The pad's XINPUT_GAMEPAD: buttons from key events and from a hat D-pad, sticks with
     *  Android's y down, analog triggers and digital L2/R2. Tracked in every context. */
    private int padKeys,padHat;
    private float padX,padY,padRX,padRY,padLT,padRT,padKeyLT,padKeyRT;
    private boolean padPresent;
    private int padMenuKey;
    /** Buttons whose press skipped a movie over a menu: their releases stay away from the menu too. */
    private int padMovieKeys;
    private final Runnable padRepeat=new Runnable() {
        @Override public void run() {
            if(padMenuKey==0)return;
            GameActivity.nativeKey(padMenuKey,true);
            postDelayed(this,140);
        }
    };
    static boolean gamepad(InputEvent event) {
        int source=event.getSource();
        return (source&InputDevice.SOURCE_GAMEPAD)==InputDevice.SOURCE_GAMEPAD||
            (source&InputDevice.SOURCE_JOYSTICK)==InputDevice.SOURCE_JOYSTICK;
    }
    static float deadZone(float value,float flat) {
        float dead=Math.max(.15f,flat);
        if(!Float.isFinite(value)||Math.abs(value)<dead)return 0;
        return Math.copySign(Math.min(1,(Math.abs(value)-dead)/(1-dead)),value);
    }
    private static float axis(MotionEvent event,int axis) {
        InputDevice device=event.getDevice();
        InputDevice.MotionRange range=device==null?null:device.getMotionRange(axis,event.getSource());
        return deadZone(event.getAxisValue(axis),range==null?0:range.getFlat());
    }
    /** An axis as Android reports it, within -1..1: the game's bindings apply their own dead zones. */
    private static float raw(MotionEvent event,int axis) {
        float value=event.getAxisValue(axis);
        return Float.isFinite(value)?Math.max(-1,Math.min(1,value)):0;
    }
    static int stick16(float value) { return Math.round(Math.max(-1,Math.min(1,value))*32767); }
    /** The XINPUT_GAMEPAD wButtons bit of a gamepad key; L2 and R2 are the triggers. */
    static int padButton(int key) {
        switch(key) {
        case KeyEvent.KEYCODE_DPAD_UP:return PAD_UP;
        case KeyEvent.KEYCODE_DPAD_DOWN:return PAD_DOWN;
        case KeyEvent.KEYCODE_DPAD_LEFT:return PAD_LEFT;
        case KeyEvent.KEYCODE_DPAD_RIGHT:return PAD_RIGHT;
        case KeyEvent.KEYCODE_BUTTON_START:return 0x0010;
        case KeyEvent.KEYCODE_BUTTON_SELECT:return 0x0020;
        case KeyEvent.KEYCODE_BUTTON_THUMBL:return 0x0040;
        case KeyEvent.KEYCODE_BUTTON_THUMBR:return 0x0080;
        case KeyEvent.KEYCODE_BUTTON_L1:return 0x0100;
        case KeyEvent.KEYCODE_BUTTON_R1:return 0x0200;
        case KeyEvent.KEYCODE_BUTTON_A:return 0x1000;
        case KeyEvent.KEYCODE_BUTTON_B:return 0x2000;
        case KeyEvent.KEYCODE_BUTTON_X:return 0x4000;
        case KeyEvent.KEYCODE_BUTTON_Y:return 0x8000;
        default:return 0;
        }
    }
    static int menuKey(int key) {
        switch(key) {
        case KeyEvent.KEYCODE_DPAD_UP:return 0x26;
        case KeyEvent.KEYCODE_DPAD_DOWN:return 0x28;
        case KeyEvent.KEYCODE_DPAD_LEFT:return 0x25;
        case KeyEvent.KEYCODE_DPAD_RIGHT:return 0x27;
        case KeyEvent.KEYCODE_BUTTON_A:case KeyEvent.KEYCODE_DPAD_CENTER:return 13;
        case KeyEvent.KEYCODE_BUTTON_B:case KeyEvent.KEYCODE_BUTTON_START:return 27;
        default:return 0;
        }
    }
    /** The pad's state to input.c, which shows it to the game during play. */
    private void padSubmit() {
        int lt=Math.round(255*Math.max(padLT,padKeyLT)),rt=Math.round(255*Math.max(padRT,padKeyRT));
        GameActivity.nativeGamepad(padPresent,padKeys|padHat,lt,rt,stick16(padX),stick16(-padY),stick16(padRX),stick16(-padRY));
        if(padPresent&&!rumblePolling){rumblePolling=true;postDelayed(rumblePoll,RUMBLE_POLL_MS);}
    }

    // ------------------------------------------------------------------ rumble
    /** The original force feedback (XnaForceFeedbackManager) sets motor speeds every frame of play;
     *  they drive the vibrators of the gamepad in hand, never the phone's. A one-shot outlasts the
     *  poll and is renewed before it ends, so a steady rumble has no gaps. */
    private static final long RUMBLE_POLL_MS=40,RUMBLE_MS=1000,RUMBLE_RENEW_MS=800;
    private int padDevice=-1,rumbleMotors,rumbleLogged;
    private long rumbleSince;
    private boolean rumblePolling;
    private final Runnable rumblePoll=new Runnable() {
        @Override public void run() {
            rumble(padPresent&&focused?GameActivity.nativeRumble():0);
            if(padPresent)postDelayed(this,RUMBLE_POLL_MS);
            else rumblePolling=false;
        }
    };
    /** Left (low-frequency) motor in the high 16 bits, right in the low 16, as XInput has them. */
    private void rumble(int motors) {
        long now=SystemClock.uptimeMillis();
        if(motors==rumbleMotors&&(motors==0||now-rumbleSince<RUMBLE_RENEW_MS))return;
        boolean stop=motors==0;
        rumbleMotors=motors;rumbleSince=now;
        InputDevice device=padDevice>=0?InputDevice.getDevice(padDevice):null;
        int left=motors>>>16,right=motors&0xFFFF;
        if(!stop&&rumbleLogged<3) {
            ++rumbleLogged;
            android.util.Log.i("UT3","gamepad rumble "+left+"/"+right+" for "+(device==null?"no device":device.getName()));
        }
        if(device==null)return;
        try {
            if(Build.VERSION.SDK_INT>=31) {
                VibratorManager manager=device.getVibratorManager();
                int[] ids=manager.getVibratorIds();
                if(ids.length==0)return;
                if(stop){manager.cancel();return;}
                CombinedVibration.ParallelCombination all=CombinedVibration.startParallel();
                if(ids.length>=2) {
                    // A rumble pad's channels: 0 its strong (left, low-frequency) motor, 1 its weak one.
                    if(left>0)all.addVibrator(ids[0],rumbleShot(left));
                    if(right>0)all.addVibrator(ids[1],rumbleShot(right));
                } else all.addVibrator(ids[0],rumbleShot(Math.max(left,right)));
                manager.vibrate(all.combine());
            } else {
                Vibrator pad=device.getVibrator();
                if(!pad.hasVibrator())return;
                if(stop)pad.cancel();
                else pad.vibrate(rumbleShot(Math.max(left,right)));
            }
        } catch(RuntimeException e) {
            android.util.Log.w("UT3","gamepad rumble: "+e);
        }
    }
    private static VibrationEffect rumbleShot(int speed) {
        return VibrationEffect.createOneShot(RUMBLE_MS,Math.max(1,Math.min(255,Math.round(speed*255f/65535f))));
    }

    /** A gamepad button: true when the layer used it. */
    boolean gamepadKey(KeyEvent event) {
        if(!gamepad(event))return false;
        padDevice=event.getDeviceId();
        int key=event.getKeyCode();
        if(event.getAction()!=KeyEvent.ACTION_DOWN&&event.getAction()!=KeyEvent.ACTION_UP)return false;
        boolean down=event.getAction()==KeyEvent.ACTION_DOWN;
        int bit=padButton(key);
        boolean trigger=key==KeyEvent.KEYCODE_BUTTON_L2||key==KeyEvent.KEYCODE_BUTTON_R2;
        if(bit!=0||trigger) {
            if(key==KeyEvent.KEYCODE_BUTTON_L2)padKeyLT=down?1:0;
            else if(key==KeyEvent.KEYCODE_BUTTON_R2)padKeyRT=down?1:0;
            else padKeys=down?padKeys|bit:padKeys&~bit;
            padPresent=true;
            padSubmit();
        }
        syncContext();
        // A, B, X, Y, Start and Back skip the movie the engine shows. A menu below sees neither the press
        // nor its release: B or Start as its Escape would also leave the menu once the movie is gone.
        if(down&&event.getRepeatCount()==0&&(bit&0xF030)!=0&&GameActivity.nativeMoviePlaying()) {
            GameActivity.nativeSkipMovie();
            if((cursorContext&3)==3)padMovieKeys|=bit;
        }
        if((padMovieKeys&bit)!=0) {
            if(!down)padMovieKeys&=~bit;
            return true;
        }
        if((cursorContext&3)==3) {
            int vk=menuKey(key);
            if(vk==0)return bit!=0||trigger;
            GameActivity.nativeKey(vk,down);
            return true;
        }
        return bit!=0||trigger||menuKey(key)!=0;
    }
    /** Gamepad sticks, triggers and a D-pad reported as a hat: true when the layer used them. */
    boolean gamepadMotion(MotionEvent event) {
        if((event.getSource()&InputDevice.SOURCE_JOYSTICK)!=InputDevice.SOURCE_JOYSTICK||event.getActionMasked()!=MotionEvent.ACTION_MOVE)return false;
        padDevice=event.getDeviceId();
        padX=raw(event,MotionEvent.AXIS_X);padY=raw(event,MotionEvent.AXIS_Y);
        padRX=raw(event,MotionEvent.AXIS_Z);padRY=raw(event,MotionEvent.AXIS_RZ);
        if(padRX==0&&padRY==0){padRX=raw(event,MotionEvent.AXIS_RX);padRY=raw(event,MotionEvent.AXIS_RY);}
        padLT=Math.max(0,Math.max(raw(event,MotionEvent.AXIS_LTRIGGER),raw(event,MotionEvent.AXIS_BRAKE)));
        padRT=Math.max(0,Math.max(raw(event,MotionEvent.AXIS_RTRIGGER),raw(event,MotionEvent.AXIS_GAS)));
        float hatX=Math.signum(deadZone(event.getAxisValue(MotionEvent.AXIS_HAT_X),.5f));
        float hatY=Math.signum(deadZone(event.getAxisValue(MotionEvent.AXIS_HAT_Y),.5f));
        padHat=(hatX<0?PAD_LEFT:hatX>0?PAD_RIGHT:0)|(hatY<0?PAD_UP:hatY>0?PAD_DOWN:0);
        padPresent=true;
        padSubmit();
        syncContext();
        if((cursorContext&3)==3) {
            // The stick or the hat as a held arrow key, repeating like a keyboard's.
            float x=hatX!=0?hatX:axis(event,MotionEvent.AXIS_X),y=hatY!=0?hatY:axis(event,MotionEvent.AXIS_Y);
            int vk=Math.abs(y)>=.6f&&Math.abs(y)>=Math.abs(x)?(y<0?0x26:0x28):Math.abs(x)>=.6f?(x<0?0x25:0x27):0;
            if(vk!=padMenuKey) {
                removeCallbacks(padRepeat);
                if(padMenuKey!=0)GameActivity.nativeKey(padMenuKey,false);
                padMenuKey=vk;
                if(vk!=0){GameActivity.nativeKey(vk,true);postDelayed(padRepeat,400);}
            }
            return true;
        }
        padRelease();
        return true;
    }
    /** Ends a menu arrow key held by the stick; the pad's own state stays as the hand holds it. */
    private void padRelease() {
        removeCallbacks(padRepeat);
        if(padMenuKey!=0){GameActivity.nativeKey(padMenuKey,false);padMenuKey=0;}
    }
    /** Focus, the surface or the pad itself is gone: Android sends no further edges, so neutral. */
    void releaseGamepad(boolean attached) {
        padRelease();
        rumble(0);
        padKeys=padHat=padMovieKeys=0;padX=padY=padRX=padRY=padLT=padRT=padKeyLT=padKeyRT=0;
        if(padPresent){padPresent=attached;GameActivity.nativeGamepad(attached,0,0,0,0,0,0,0);}
    }
    /** Whether a gamepad or joystick is still attached (after a device was removed). */
    static boolean gamepadAttached() {
        for(int id:InputDevice.getDeviceIds()) {
            InputDevice device=InputDevice.getDevice(id);
            if(device==null||device.isVirtual())continue;
            int sources=device.getSources();
            if((sources&InputDevice.SOURCE_GAMEPAD)==InputDevice.SOURCE_GAMEPAD||
               (sources&InputDevice.SOURCE_JOYSTICK)==InputDevice.SOURCE_JOYSTICK)return true;
        }
        return false;
    }

    // ---------------------------------------------------------- accessibility
    private static final int NODE_PIE=100,NODE_PIE_CENTER=120,NODE_TOOL=200;
    private String nodeLabel(int id) {
        if(id>=NODE_TOOL)return id==NODE_TOOL+TOOL_DONE?text("Done","Готово"):text("Reset layout, tap twice","Сбросить раскладку, нажмите дважды");
        if(id==NODE_PIE_CENTER)return text("Close","Закрыть");
        if(id>=NODE_PIE)return pieLabel(pieItems[id-NODE_PIE],true);
        return accessibilityLabel(id);
    }
    private boolean nodeValid(int id) {
        if(pieOpen)return id==NODE_PIE_CENTER||(id>=NODE_PIE&&id<NODE_PIE+pieItems.length);
        if(editing&&id>=NODE_TOOL&&id<NODE_TOOL+tools.length)return tools[id-NODE_TOOL]!=null;
        return id>=0&&id<TouchLayout.COUNT&&shown[id];
    }
    private Rect nodeBounds(int id) {
        float x,y,r;
        if(id>=NODE_TOOL){float[] t=tools[id-NODE_TOOL];return new Rect((int)t[0],(int)t[1],(int)t[2],(int)t[3]);}
        if(id==NODE_PIE_CENTER){x=pieX;y=pieY;r=pieR*.36f;}
        else if(id>=NODE_PIE) {
            double a=TouchLayout.sectorAngle(id-NODE_PIE,pieItems.length);
            x=pieX+(float)Math.sin(a)*pieRing();y=pieY-(float)Math.cos(a)*pieRing();r=24*dp;
        } else {x=controlX[id];y=controlY[id];r=controlR[id];}
        return new Rect((int)(x-r),(int)(y-r),(int)(x+r),(int)(y+r));
    }
    private void accessibilityClick(int id) {
        if(id>=NODE_TOOL)tool(id-NODE_TOOL);
        else if(id==NODE_PIE_CENTER)closePie();
        else if(id>=NODE_PIE){int item=pieItems[id-NODE_PIE];closePie();pieSelect(item);}
        else {
            int a=action(id);
            if(a==MORE||a==SEATS)openPie(-1,controlX[id],controlY[id],a==MORE?PIE_ACTIONS:PIE_SEATS,id);
            else if(a==WEAPON)input.pulse(TouchInput.NEXT);
            else if(a==TouchInput.SCORES)input.toggle(TouchInput.SCORES);
            else if(a<0||a==TouchInput.PAUSE)activate(id);
            else if(a>0)input.pulse(a);
            if(pieOpen)pieSticky=true;
        }
        invalidate();
    }
    private int nodeAt(float x,float y) {
        if(pieOpen) {
            int hit=TouchLayout.pieHit(x,y,pieX,pieY,pieR*.36f,pieR,pieItems.length);
            return hit==TouchLayout.CENTER?NODE_PIE_CENTER:hit>=0?NODE_PIE+hit:-1;
        }
        if(editing){int tool=toolHit(x,y);if(tool>=0)return NODE_TOOL+tool;}
        return hit(x,y);
    }
    private void accessibilityEvent(int id,int type) {
        if(id<0||getParent()==null||!nodeValid(id))return;
        AccessibilityEvent event=AccessibilityEvent.obtain(type);
        event.setSource(this,id);event.setPackageName(getContext().getPackageName());
        event.setContentDescription(nodeLabel(id));getParent().requestSendAccessibilityEvent(this,event);
    }
    @Override public boolean dispatchHoverEvent(MotionEvent event) {
        AccessibilityManager manager=(AccessibilityManager)getContext().getSystemService(Context.ACCESSIBILITY_SERVICE);
        if(manager!=null&&manager.isTouchExplorationEnabled()) {
            int next=event.getActionMasked()==MotionEvent.ACTION_HOVER_EXIT?-1:nodeAt(event.getX(),event.getY());
            if(next!=hovered) {
                accessibilityEvent(hovered,AccessibilityEvent.TYPE_VIEW_HOVER_EXIT);hovered=next;
                accessibilityEvent(hovered,AccessibilityEvent.TYPE_VIEW_HOVER_ENTER);
            }
            return next>=0;
        }
        return super.dispatchHoverEvent(event);
    }
    private final AccessibilityNodeProvider accessibility=new AccessibilityNodeProvider() {
        @Override public AccessibilityNodeInfo createAccessibilityNodeInfo(int id) {
            if(layoutDirty)buildLayout();
            if(id==View.NO_ID) {
                AccessibilityNodeInfo info=AccessibilityNodeInfo.obtain(GameControls.this);onInitializeAccessibilityNodeInfo(info);
                if(pieOpen) {
                    for(int i=0;i<pieItems.length;++i)info.addChild(GameControls.this,NODE_PIE+i);
                    info.addChild(GameControls.this,NODE_PIE_CENTER);
                } else {
                    for(int i=0;i<TouchLayout.COUNT;++i)if(shown[i])info.addChild(GameControls.this,i);
                    if(editing)for(int i=0;i<tools.length;++i)if(tools[i]!=null)info.addChild(GameControls.this,NODE_TOOL+i);
                }
                return info;
            }
            if(!nodeValid(id))return null;
            AccessibilityNodeInfo info=AccessibilityNodeInfo.obtain();info.setSource(GameControls.this,id);info.setParent(GameControls.this);
            info.setPackageName(getContext().getPackageName());info.setClassName("android.widget.Button");
            String name=nodeLabel(id);
            info.setText(name);info.setContentDescription(name);info.setEnabled(true);info.setFocusable(true);
            info.setAccessibilityFocused(accessibilityFocus==id);
            info.setClickable(true);info.setVisibleToUser(isShown());
            Rect bounds=nodeBounds(id);info.setBoundsInParent(bounds);
            int[] location=new int[2];getLocationOnScreen(location);bounds.offset(location[0],location[1]);info.setBoundsInScreen(bounds);
            info.addAction(AccessibilityNodeInfo.ACTION_CLICK);info.addAction(AccessibilityNodeInfo.ACTION_ACCESSIBILITY_FOCUS);
            info.addAction(AccessibilityNodeInfo.ACTION_CLEAR_ACCESSIBILITY_FOCUS);return info;
        }
        @Override public boolean performAction(int id,int action,Bundle args) {
            if(!nodeValid(id))return false;
            if(action==AccessibilityNodeInfo.ACTION_CLICK) { accessibilityClick(id);return true; }
            if(action==AccessibilityNodeInfo.ACTION_ACCESSIBILITY_FOCUS||action==AccessibilityNodeInfo.ACTION_CLEAR_ACCESSIBILITY_FOCUS) {
                accessibilityFocus=action==AccessibilityNodeInfo.ACTION_ACCESSIBILITY_FOCUS?id:-1;
                accessibilityEvent(id,action==AccessibilityNodeInfo.ACTION_ACCESSIBILITY_FOCUS?
                    AccessibilityEvent.TYPE_VIEW_ACCESSIBILITY_FOCUSED:AccessibilityEvent.TYPE_VIEW_ACCESSIBILITY_FOCUS_CLEARED);
                return true;
            }
            return false;
        }
    };
    @Override public AccessibilityNodeProvider getAccessibilityNodeProvider() { return accessibility; }
    @Override public boolean performClick() { super.performClick();return true; }
}
