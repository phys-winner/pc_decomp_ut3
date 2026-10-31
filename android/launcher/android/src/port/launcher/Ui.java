package port.launcher;

import android.app.Activity;
import android.app.AlertDialog;
import android.content.res.ColorStateList;
import android.graphics.Canvas;
import android.graphics.Color;
import android.graphics.Paint;
import android.graphics.Typeface;
import android.graphics.drawable.ClipDrawable;
import android.graphics.drawable.Drawable;
import android.graphics.drawable.GradientDrawable;
import android.graphics.drawable.LayerDrawable;
import android.graphics.drawable.RippleDrawable;
import android.os.Build;
import android.view.Gravity;
import android.view.View;
import android.view.WindowInsets;
import android.widget.LinearLayout;
import android.widget.ProgressBar;
import android.widget.TextView;
import org.json.JSONArray;
import org.json.JSONObject;

/** The launcher's look from the theme tokens of port.json: cards, buttons, a drawn switch (the
 *  platform's one is near invisible on dark backgrounds), progress bars and edge-to-edge insets. */
final class Ui {
    static final int PRIMARY = 0, SECONDARY = 1, QUIET = 2;

    final Activity activity;
    final int[] background;
    final int card, cardStroke, cell, accent, accentDark, primaryFill, text, muted, error, good;
    final Typeface titleFont, bold, regular;

    Ui(Activity activity) {
        this.activity = activity;
        JSONObject t = Spec.get(activity).theme;
        JSONArray gradient = t.optJSONArray("background");
        int[] colours = {0xFF141A22, 0xFF0B0F14, 0xFF030405};
        if (gradient != null && gradient.length() > 0) {
            colours = new int[gradient.length()];
            for (int i = 0; i < colours.length; ++i) colours[i] = parse(gradient.optString(i), 0xFF0B0F14);
        }
        background = colours;
        card = colour(t, "card", 0xEB181B22);
        cardStroke = colour(t, "cardStroke", 0x40FFFFFF);
        cell = colour(t, "cell", 0xFF262A33);
        accent = colour(t, "accent", 0xFFE8742A);
        accentDark = colour(t, "accentDark", 0xFF9A4A16);
        primaryFill = colour(t, "primaryFill", 0xFF3A2414);
        text = colour(t, "text", 0xFFF2F4F7);
        muted = colour(t, "muted", 0xFFA7AFBC);
        error = colour(t, "error", 0xFFFF8C7A);
        good = colour(t, "good", 0xFF9FD59A);
        titleFont = font(t.optString("titleFont", "sans-serif-medium"));
        bold = Typeface.create("sans-serif-medium", Typeface.NORMAL);
        regular = Typeface.create("sans-serif", Typeface.NORMAL);
    }

    private static int colour(JSONObject theme, String key, int fallback) { return parse(theme.optString(key, ""), fallback); }

    static int parse(String text, int fallback) {
        try { return text.isEmpty() ? fallback : Color.parseColor(text); }
        catch (IllegalArgumentException e) { return fallback; }
    }

    /** "serif-bold", "sans-serif-medium", "serif-italic", "monospace". */
    static Typeface font(String name) {
        int style = Typeface.NORMAL;
        if (name.endsWith("-bold")) { style = Typeface.BOLD; name = name.substring(0, name.length() - 5); }
        else if (name.endsWith("-italic")) { style = Typeface.ITALIC; name = name.substring(0, name.length() - 7); }
        return Typeface.create(name, style);
    }

    int dp(float v) { return Math.round(v * activity.getResources().getDisplayMetrics().density); }

    boolean wide() { return activity.getResources().getConfiguration().screenWidthDp >= 720; }

    Drawable backdrop() {
        return background.length == 1 ? new GradientDrawable(GradientDrawable.Orientation.TOP_BOTTOM, new int[] {background[0], background[0]})
            : new GradientDrawable(GradientDrawable.Orientation.TOP_BOTTOM, background);
    }

    /** Draws behind the system bars and pads the content by them and the display cutout. */
    void edgeToEdge(View root) {
        if (Build.VERSION.SDK_INT >= 35) { /* edge to edge already */ }
        else if (Build.VERSION.SDK_INT >= 30) activity.getWindow().setDecorFitsSystemWindows(false);
        else root.setSystemUiVisibility(View.SYSTEM_UI_FLAG_LAYOUT_STABLE | View.SYSTEM_UI_FLAG_LAYOUT_FULLSCREEN
            | View.SYSTEM_UI_FLAG_LAYOUT_HIDE_NAVIGATION);
        if (Build.VERSION.SDK_INT < 35) {
            activity.getWindow().setStatusBarColor(Color.TRANSPARENT);
            activity.getWindow().setNavigationBarColor(Color.TRANSPARENT);
        }
        final int left = root.getPaddingLeft(), top = root.getPaddingTop(), right = root.getPaddingRight(), bottom = root.getPaddingBottom();
        root.setOnApplyWindowInsetsListener((view, insets) -> {
            int[] e = edges(insets);
            view.setPadding(left + e[0], top + e[1], right + e[2], bottom + e[3]);
            return insets;
        });
        root.requestApplyInsets();
    }

    private static int[] edges(WindowInsets insets) {
        if (Build.VERSION.SDK_INT >= 30) {
            android.graphics.Insets e = insets.getInsets(WindowInsets.Type.systemBars() | WindowInsets.Type.displayCutout());
            return new int[] {e.left, e.top, e.right, e.bottom};
        }
        return new int[] {insets.getSystemWindowInsetLeft(), insets.getSystemWindowInsetTop(),
            insets.getSystemWindowInsetRight(), insets.getSystemWindowInsetBottom()};
    }

    LinearLayout card(LinearLayout parent) {
        LinearLayout c = new LinearLayout(activity);
        c.setOrientation(LinearLayout.VERTICAL);
        c.setPadding(dp(16), dp(10), dp(16), dp(14));
        GradientDrawable bg = new GradientDrawable();
        bg.setColor(card);
        bg.setCornerRadius(dp(16));
        bg.setStroke(dp(1), cardStroke);
        c.setBackground(bg);
        LinearLayout.LayoutParams lp = new LinearLayout.LayoutParams(-1, -2);
        lp.setMargins(0, dp(12), 0, 0);
        if (parent != null) parent.addView(c, lp);
        else c.setLayoutParams(lp);
        return c;
    }

    TextView label(String value, float sp, int colour) {
        TextView t = new TextView(activity);
        t.setText(value);
        t.setTextSize(sp);
        t.setTextColor(colour);
        t.setTypeface(regular);
        t.setLineSpacing(dp(1), 1.05f);
        t.setPadding(0, dp(3), 0, dp(2));
        return t;
    }

    TextView heading(String value) {
        TextView t = label(value.toUpperCase(java.util.Locale.getDefault()), 12, accent);
        t.setTypeface(bold);
        t.setLetterSpacing(0.08f);
        t.setPadding(0, dp(6), 0, dp(4));
        return t;
    }

    TextView button(String value, int kind, Runnable action) {
        TextView b = new TextView(activity);
        b.setText(value);
        b.setGravity(Gravity.CENTER);
        b.setTextSize(kind == PRIMARY ? 19 : 15);
        b.setTypeface(bold);
        b.setTextColor(kind == PRIMARY ? accent : kind == SECONDARY ? text : muted);
        b.setMinHeight(dp(kind == PRIMARY ? 54 : 46));
        b.setPadding(dp(14), dp(8), dp(14), dp(8));
        GradientDrawable shape = new GradientDrawable();
        shape.setCornerRadius(dp(kind == PRIMARY ? 14 : 12));
        shape.setColor(kind == PRIMARY ? primaryFill : kind == SECONDARY ? cell : Color.TRANSPARENT);
        if (kind == PRIMARY) shape.setStroke(dp(2), accentDark);
        GradientDrawable mask = new GradientDrawable();
        mask.setCornerRadius(dp(14));
        mask.setColor(Color.WHITE);
        b.setBackground(new RippleDrawable(ColorStateList.valueOf(0x30FFFFFF), shape, mask));
        b.setFocusable(true);
        b.setOnClickListener(v -> action.run());
        return b;
    }

    /** Buttons side by side (wide screens) or stacked. */
    LinearLayout buttons(TextView... list) {
        LinearLayout row = new LinearLayout(activity);
        boolean side = list.length > 1 && (wide() || list.length == 2);
        row.setOrientation(side ? LinearLayout.HORIZONTAL : LinearLayout.VERTICAL);
        for (int i = 0; i < list.length; ++i) {
            LinearLayout.LayoutParams lp = side ? new LinearLayout.LayoutParams(0, -2, 1) : new LinearLayout.LayoutParams(-1, -2);
            if (i > 0) { if (side) lp.leftMargin = dp(8); else lp.topMargin = dp(8); }
            row.addView(list[i], lp);
        }
        LinearLayout.LayoutParams lp = new LinearLayout.LayoutParams(-1, -2);
        lp.topMargin = dp(10);
        row.setLayoutParams(lp);
        return row;
    }

    interface BoolSink { void set(boolean value); }

    /** A label (and hint) with a drawn switch; the whole row toggles. */
    View toggle(String title, String hint, boolean value, BoolSink sink) {
        LinearLayout row = new LinearLayout(activity);
        row.setGravity(Gravity.CENTER_VERTICAL);
        row.setPadding(0, dp(6), 0, dp(6));
        row.setMinimumHeight(dp(48));
        LinearLayout words = new LinearLayout(activity);
        words.setOrientation(LinearLayout.VERTICAL);
        words.addView(label(title, 15, text));
        if (hint != null && !hint.isEmpty()) words.addView(label(hint, 12, muted));
        row.addView(words, new LinearLayout.LayoutParams(0, -2, 1));
        final boolean[] on = {value};
        View pill = new View(activity) {
            private final Paint p = new Paint(Paint.ANTI_ALIAS_FLAG);

            @Override protected void onDraw(Canvas c) {
                float w = getWidth(), h = getHeight(), r = h / 2f;
                p.setColor(on[0] ? accentDark : 0xFF555A63);
                c.drawRoundRect(0, 0, w, h, r, r, p);
                p.setColor(on[0] ? accent : 0xFFD7DAE0);
                c.drawCircle(on[0] ? w - r : r, r, r * 0.78f, p);
            }
        };
        LinearLayout.LayoutParams lp = new LinearLayout.LayoutParams(dp(52), dp(30));
        lp.leftMargin = dp(12);
        row.addView(pill, lp);
        row.setFocusable(true);
        row.setOnClickListener(v -> {
            on[0] = !on[0];
            sink.set(on[0]);
            pill.invalidate();
        });
        return row;
    }

    /** A tappable choice: title, hint, a stroke when chosen, faded when unavailable. */
    View choice(String title, String hint, boolean chosen, boolean available, Runnable action) {
        LinearLayout row = new LinearLayout(activity);
        row.setOrientation(LinearLayout.VERTICAL);
        row.setPadding(dp(12), dp(8), dp(12), dp(8));
        GradientDrawable bg = new GradientDrawable();
        bg.setCornerRadius(dp(12));
        bg.setColor(cell);
        if (chosen) bg.setStroke(dp(2), accent);
        row.setBackground(bg);
        row.addView(label(title, 15, available ? text : muted));
        if (hint != null && !hint.isEmpty()) row.addView(label(hint, 12, muted));
        row.setAlpha(available ? 1f : 0.55f);
        if (action != null) {
            row.setFocusable(true);
            row.setOnClickListener(v -> action.run());
        }
        LinearLayout.LayoutParams lp = new LinearLayout.LayoutParams(-1, -2);
        lp.topMargin = dp(8);
        row.setLayoutParams(lp);
        return row;
    }

    ProgressBar progress() {
        ProgressBar bar = new ProgressBar(activity, null, android.R.attr.progressBarStyleHorizontal);
        bar.setMax(1000);
        GradientDrawable track = new GradientDrawable(), fill = new GradientDrawable();
        track.setColor(cell);
        track.setCornerRadius(dp(4));
        fill.setColor(accent);
        fill.setCornerRadius(dp(4));
        LayerDrawable layers = new LayerDrawable(new Drawable[] {track, new ClipDrawable(fill, Gravity.START, ClipDrawable.HORIZONTAL)});
        layers.setId(0, android.R.id.background);
        layers.setId(1, android.R.id.progress);
        bar.setProgressDrawable(layers);
        bar.setIndeterminateTintList(ColorStateList.valueOf(accent));
        return bar;
    }

    AlertDialog.Builder dialog() { return new AlertDialog.Builder(activity, android.R.style.Theme_Material_Dialog_Alert); }

    /** Shows a dialog with its buttons in the port's accent colour instead of the platform's teal. */
    AlertDialog show(AlertDialog.Builder builder) {
        AlertDialog shown = builder.show();
        for (int which : new int[] {AlertDialog.BUTTON_POSITIVE, AlertDialog.BUTTON_NEGATIVE, AlertDialog.BUTTON_NEUTRAL}) {
            android.widget.Button button = shown.getButton(which);
            if (button != null) button.setTextColor(accent);
        }
        return shown;
    }

    static void enable(View v, boolean on) {
        v.setEnabled(on);
        v.setAlpha(on ? 1f : 0.42f);
    }
}
