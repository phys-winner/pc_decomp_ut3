package org.ut3.port;

import android.app.Dialog;
import android.text.InputFilter;
import android.text.InputType;
import android.view.Gravity;
import android.view.View;
import android.view.WindowManager;
import android.view.inputmethod.EditorInfo;
import android.view.inputmethod.InputMethodManager;
import android.widget.Button;
import android.widget.EditText;
import android.widget.LinearLayout;
import android.widget.TextView;
import java.util.function.Predicate;

/** A platform editor owns composition, selection and the visible text above the IME.
 *  note: a line above the editor (null for none), e.g. that the field's text is not shown. */
final class TextInputDialog extends Dialog {
    private final EditText editor;

    TextInputDialog(GameActivity activity,String initial,boolean password,int limit,String title,String note,
                    Predicate<String> apply,Runnable closed) {
        super(activity,android.R.style.Theme_Material_Light_NoActionBar);
        int padding=Math.round(16*activity.getResources().getDisplayMetrics().density);
        LinearLayout content=new LinearLayout(getContext());
        content.setOrientation(LinearLayout.VERTICAL);
        content.setFitsSystemWindows(true);
        content.setPadding(padding,padding,padding,padding);
        content.setImportantForAutofill(View.IMPORTANT_FOR_AUTOFILL_NO_EXCLUDE_DESCENDANTS);
        content.setImportantForContentCapture(View.IMPORTANT_FOR_CONTENT_CAPTURE_NO_EXCLUDE_DESCENDANTS);
        editor=new EditText(getContext());
        editor.setSingleLine(true);
        editor.setTextSize(24);
        editor.setHint(title);
        editor.setInputType(InputType.TYPE_CLASS_TEXT|InputType.TYPE_TEXT_FLAG_NO_SUGGESTIONS|
            (password?InputType.TYPE_TEXT_VARIATION_PASSWORD:InputType.TYPE_TEXT_VARIATION_NORMAL));
        editor.setImeOptions(EditorInfo.IME_ACTION_DONE|EditorInfo.IME_FLAG_NO_FULLSCREEN|
            EditorInfo.IME_FLAG_NO_EXTRACT_UI|EditorInfo.IME_FLAG_NO_PERSONALIZED_LEARNING);
        editor.setImportantForAutofill(View.IMPORTANT_FOR_AUTOFILL_NO_EXCLUDE_DESCENDANTS);
        editor.setAutofillHints((String[])null);
        editor.setSaveEnabled(false);
        editor.setText(initial);
        editor.setFilters(new InputFilter[]{new InputFilter.LengthFilter(limit)});
        editor.setSelection(editor.length());
        if(note!=null) {
            TextView message=new TextView(getContext());
            message.setText(note);
            message.setPadding(0,0,0,padding/2);
            content.addView(message,new LinearLayout.LayoutParams(-1,-2));
        }
        content.addView(editor,new LinearLayout.LayoutParams(-1,-2));
        LinearLayout buttons=new LinearLayout(getContext());
        buttons.setGravity(Gravity.END);
        Button cancel=new Button(getContext()),done=new Button(getContext());
        cancel.setText(android.R.string.cancel);done.setText(android.R.string.ok);
        buttons.addView(cancel);buttons.addView(done);content.addView(buttons);
        setContentView(content);
        Runnable submit=()->{if(apply.test(editor.getText().toString()))dismiss();};
        done.setOnClickListener(view->submit.run());cancel.setOnClickListener(view->cancel());
        editor.setOnEditorActionListener((view,action,event)->{
            if(action==EditorInfo.IME_ACTION_DONE || (event!=null&&event.getKeyCode()==android.view.KeyEvent.KEYCODE_ENTER)) {
                if(event==null||event.getAction()==android.view.KeyEvent.ACTION_DOWN)submit.run();
                return true;
            }
            return false;
        });
        setOnDismissListener(dialog->{editor.getText().clear();closed.run();});
        setOnShowListener(dialog->{
            getWindow().clearFlags(WindowManager.LayoutParams.FLAG_FULLSCREEN);
            getWindow().setLayout(-1,-1);
            getWindow().setSoftInputMode(WindowManager.LayoutParams.SOFT_INPUT_ADJUST_RESIZE|
                WindowManager.LayoutParams.SOFT_INPUT_STATE_ALWAYS_VISIBLE);
            editor.requestFocus();
            editor.post(()->{
                if(isShowing()) {
                    InputMethodManager manager=activity.getSystemService(InputMethodManager.class);
                    if(manager!=null)manager.showSoftInput(editor,InputMethodManager.SHOW_IMPLICIT);
                }
            });
        });
    }
}
