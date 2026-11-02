package org.ut3.port;

import android.content.Context;
import android.view.Gravity;
import android.view.View;
import android.view.ViewGroup;
import android.view.MotionEvent;
import android.widget.AdapterView;
import android.widget.ArrayAdapter;
import android.widget.CheckBox;
import android.widget.LinearLayout;
import android.widget.SeekBar;
import android.widget.Spinner;
import android.widget.TextView;

/** Tools over GameControls' editable preview. Edits stay in memory until Save. */
final class TouchLayoutEditor extends LinearLayout {
    private final GameControls controls;
    private final Spinner selection;
    private final SeekBar size;
    private final CheckBox visible;
    private final TextView caption;
    private final int[] ids={TouchLayout.FIRE,TouchLayout.FIRE2,TouchLayout.ALT,TouchLayout.JUMP,TouchLayout.DUCK,
        TouchLayout.USE,TouchLayout.WEAPON,TouchLayout.BOARD,TouchLayout.MORE,TouchLayout.PAUSE,TouchLayout.SCORES,TouchLayout.STICK};
    private boolean updating;
    private final boolean russian;

    TouchLayoutEditor(Context context, GameControls controls, boolean russian) {
        super(context);
        this.controls=controls;this.russian=russian;
        setOrientation(VERTICAL);setPadding(dp(12),dp(6),dp(12),dp(6));setBackgroundColor(0xED141820);
        caption=new TextView(context);caption.setTextColor(0xFFFFFFFF);caption.setTextSize(12);addView(caption);
        selection=new Spinner(context);
        String[] names=new String[ids.length];
        for(int i=0;i<ids.length;i++)names[i]=controls.editLabel(ids[i]);
        ArrayAdapter<String> adapter=new ArrayAdapter<String>(context,android.R.layout.simple_spinner_item,names) {
            @Override public View getView(int position,View recycled,ViewGroup parent) {
                TextView row=(TextView)super.getView(position,recycled,parent);
                row.setTextColor(0xFFFFFFFF);row.setBackgroundColor(0xFF252C38);return row;
            }
            @Override public View getDropDownView(int position,View recycled,ViewGroup parent) {
                TextView row=(TextView)super.getDropDownView(position,recycled,parent);
                row.setTextColor(0xFFFFFFFF);row.setBackgroundColor(0xFF252C38);return row;
            }
        };
        adapter.setDropDownViewResource(android.R.layout.simple_spinner_dropdown_item);
        selection.setAdapter(adapter);addView(selection,new LayoutParams(-1,dp(44)));
        selection.setOnItemSelectedListener(new AdapterView.OnItemSelectedListener() {
            @Override public void onItemSelected(AdapterView<?> parent,View view,int index,long id) {
                if(!updating){controls.editSelect(ids[index]);refresh();}
            }
            @Override public void onNothingSelected(AdapterView<?> parent) { }
        });
        size=new SeekBar(context);size.setMax(156);addView(size,new LayoutParams(-1,dp(40)));
        size.setOnSeekBarChangeListener(new SeekBar.OnSeekBarChangeListener() {
            @Override public void onProgressChanged(SeekBar bar,int value,boolean user) {
                if(user&&!updating){controls.editSize(value+44);refresh();}
            }
            @Override public void onStartTrackingTouch(SeekBar bar) { }
            @Override public void onStopTrackingTouch(SeekBar bar) { }
        });
        visible=new CheckBox(context);visible.setText(russian?"Отображать элемент":"Show control");visible.setTextColor(0xFFFFFFFF);
        addView(visible,new LayoutParams(-1,dp(40)));
        visible.setOnCheckedChangeListener((button,checked)->{if(!updating)controls.editHidden(!checked);});
        LinearLayout actions=new LinearLayout(context);actions.setGravity(Gravity.CENTER);addView(actions);
        button(actions,russian?"Сброс":"Reset",()->new android.app.AlertDialog.Builder(context)
            .setMessage(russian?"Вернуть исходную раскладку?":"Restore the default layout?")
            .setNegativeButton(android.R.string.cancel,null).setPositiveButton(android.R.string.ok,(d,w)->{controls.resetEditedLayout();refresh();}).show());
        button(actions,russian?"Отмена":"Cancel",()->controls.endLayoutEditor(false));
        button(actions,russian?"Сохранить":"Save",()->controls.endLayoutEditor(true));
        caption.setOnTouchListener(new View.OnTouchListener() {
            float x,y,tx,ty;
            @Override public boolean onTouch(View view,MotionEvent event) {
                if(event.getActionMasked()==MotionEvent.ACTION_DOWN) {
                    x=event.getRawX();y=event.getRawY();tx=getTranslationX();ty=getTranslationY();return true;
                }
                if(event.getActionMasked()==MotionEvent.ACTION_MOVE) {
                    View parent=(View)getParent();
                    if(parent!=null) {
                        setTranslationX(Math.max(-getLeft(),Math.min(parent.getWidth()-getRight(),tx+event.getRawX()-x)));
                        setTranslationY(Math.max(-getTop(),Math.min(parent.getHeight()-getBottom(),ty+event.getRawY()-y)));
                    }
                    return true;
                }
                return true;
            }
        });
        post(this::refresh);
    }
    private int dp(int value) { return Math.round(value*getResources().getDisplayMetrics().density); }
    private void button(LinearLayout row,String text,Runnable action) {
        TextView button=new TextView(getContext());button.setText(text);button.setTextSize(11);button.setTextColor(0xFFFFFFFF);
        button.setGravity(Gravity.CENTER);button.setBackgroundColor(0xFF344355);button.setFocusable(true);
        button.setAccessibilityDelegate(new View.AccessibilityDelegate() {
            @Override public void onInitializeAccessibilityNodeInfo(View view,android.view.accessibility.AccessibilityNodeInfo info) {
                super.onInitializeAccessibilityNodeInfo(view,info);info.setClassName("android.widget.Button");
            }
        });
        LayoutParams params=new LayoutParams(0,dp(44),1);params.setMargins(dp(3),0,dp(3),0);
        row.addView(button,params);button.setOnClickListener(v->action.run());
    }
    void refresh() {
        updating=true;
        for(int i=0;i<ids.length;i++)if(ids[i]==controls.editSelection())selection.setSelection(i);
        int diameter=Math.round(controls.editSize());size.setProgress(Math.max(0,diameter-44));
        visible.setChecked(!controls.editHidden());
        caption.setText((russian?"Кнопки — перетаскивать, размер — двумя пальцами. Панель — за заголовок. Размер: ":
            "Drag controls; pinch to resize. Drag this heading to move tools. Size: ")+diameter+" dp");
        updating=false;
    }
}
