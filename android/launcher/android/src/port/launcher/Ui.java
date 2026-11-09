package port.launcher;

import android.animation.ValueAnimator;
import android.graphics.LinearGradient;
import android.graphics.Matrix;
import android.graphics.Path;
import android.graphics.Shader;
import android.os.SystemClock;
import android.provider.Settings;
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
import android.view.accessibility.AccessibilityEvent;
import android.view.accessibility.AccessibilityNodeInfo;
import android.widget.LinearLayout;
import android.widget.ProgressBar;
import android.widget.TextView;
import org.json.JSONArray;
import org.json.JSONObject;

/** The launcher's look from the theme tokens of port.json: cards, buttons, a drawn switch (the
 *  platform's one is near invisible on dark backgrounds), progress bars and edge-to-edge insets. */
final class Ui {
    static final int PRIMARY = 0, SECONDARY = 1, QUIET = 2;
    static final int WARNING = 0xFFFFD166;

    final Activity activity;
    final int[] background;
    final int card, cardStroke, cell, accent, accentDark, primaryFill, playGlow, playParticles, text, muted, error, good;
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
        playGlow = colour(t, "playGlow", accent);
        playParticles = colour(t, "playParticles", text);
        muted = colour(t, "muted", 0xFFA7AFBC);
        error = colour(t, "error", 0xFFFF8C7A);
        good = colour(t, "good", 0xFF9FD59A);
        titleFont = font(t.optString("titleFont", "sans-serif-medium"));
        bold = Typeface.create("sans-serif-medium", Typeface.NORMAL);
        regular = Typeface.create("sans-serif", Typeface.NORMAL);
    }

    private static int colour(JSONObject theme, String key, int fallback) { return theme == null ? fallback : parse(theme.optString(key, ""), fallback); }

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
        return button(value, kind, null, action);
    }

    TextView button(String value, int kind, JSONObject theme, Runnable action) {
        TextView b = new ActionButton(theme);
        b.setText(value);
        b.setTag(value);
        b.setGravity(Gravity.CENTER);
        b.setTextSize(kind == PRIMARY ? 19 : 15);
        b.setTypeface(bold);
        b.setTextColor(kind == PRIMARY ? accent : kind == SECONDARY ? text : muted);
        b.setMinHeight(dp(kind == PRIMARY ? 54 : 46));
        b.setPadding(dp(14), dp(8), dp(14), dp(8));
        GradientDrawable shape = new GradientDrawable();
        shape.setCornerRadius(dp(kind == PRIMARY ? 14 : 12));
        shape.setColor(kind == PRIMARY ? colour(theme, "primaryFill", primaryFill) : kind == SECONDARY ? cell : Color.TRANSPARENT);
        if (kind == PRIMARY) shape.setStroke(dp(2), colour(theme, "accentDark", accentDark));
        GradientDrawable mask = new GradientDrawable();
        mask.setCornerRadius(dp(14));
        mask.setColor(Color.WHITE);
        b.setBackground(new RippleDrawable(ColorStateList.valueOf(0x30FFFFFF), shape, mask));
        b.setFocusable(true);
        b.setOnClickListener(v -> action.run());
        return b;
    }

    void attention(TextView button, boolean sparkle) {
        ((ActionButton) button).effect = sparkle ? 2 : 1;
        if (sparkle) button.setTextColor(text);
        button.invalidate();
    }

    private final class ActionButton extends TextView {
        private final Paint paint = new Paint(Paint.ANTI_ALIAS_FLAG);
        private final Path clip = new Path();
        private final Matrix shift = new Matrix();
        private final int glow, particles;
        private final LinearGradient sheen;
        int effect;

        ActionButton(JSONObject theme) {
            super(activity);
            glow = colour(theme, "playGlow", playGlow);
            particles = colour(theme, "playParticles", playParticles);
            int rgb = particles & 0x00FFFFFF;
            sheen = new LinearGradient(0, 0, dp(80), 0,
                new int[] {rgb, 0x28000000 | rgb, rgb}, null, Shader.TileMode.CLAMP);
        }

        @Override protected void onSizeChanged(int w, int h, int oldw, int oldh) {
            super.onSizeChanged(w, h, oldw, oldh);
            clip.reset();
            clip.addRoundRect(0, 0, w, h, dp(14), dp(14), Path.Direction.CW);
        }

        @Override public void onWindowFocusChanged(boolean focused) {
            super.onWindowFocusChanged(focused);
            invalidate();
        }

        @Override protected void onWindowVisibilityChanged(int visibility) {
            super.onWindowVisibilityChanged(visibility);
            invalidate();
        }

        @Override protected void onDraw(Canvas canvas) {
            if (effect != 0 && isEnabled()) {
                boolean motion = Build.VERSION.SDK_INT >= 26 ? ValueAnimator.areAnimatorsEnabled()
                    : Settings.Global.getFloat(activity.getContentResolver(), Settings.Global.ANIMATOR_DURATION_SCALE, 1f) > 0;
                float phase = motion ? (SystemClock.uptimeMillis() % 3600) / 3600f : 0.25f;
                float pulse = (float) (0.5 - 0.5 * Math.cos(phase * Math.PI * 2));
                float inset = dp(1);
                int saved = canvas.save();
                canvas.clipPath(clip);
                paint.setColor(effect == 2 ? glow : accent);
                paint.setAlpha((int) (12 + 22 * pulse));
                canvas.drawRoundRect(0, 0, getWidth(), getHeight(), dp(14), dp(14), paint);
                paint.setStyle(Paint.Style.STROKE);
                paint.setStrokeWidth(dp(2));
                paint.setAlpha((int) (45 + 85 * pulse));
                canvas.drawRoundRect(inset, inset, getWidth() - inset, getHeight() - inset, dp(13), dp(13), paint);
                paint.setStyle(Paint.Style.FILL);
                if (effect == 2) {
                    shift.setTranslate(-dp(80) + phase * (getWidth() + dp(160)), 0);
                    sheen.setLocalMatrix(shift);
                    paint.setShader(sheen);
                    paint.setAlpha(255);
                    canvas.drawRect(0, 0, getWidth(), getHeight(), paint);
                    paint.setShader(null);
                    paint.setColor(particles);
                    for (int i = 0; i < 6; ++i) {
                        float age = (phase + i / 6f) % 1;
                        float x = getWidth() * (0.08f + ((i * 0.173f) % 0.84f));
                        float y = getHeight() * (0.95f - age * 0.85f);
                        float radius = dp(1.5f) * (float) Math.sin(age * Math.PI);
                        paint.setAlpha((int) (190 * Math.sin(age * Math.PI)));
                        canvas.drawCircle(x, y, radius, paint);
                        if (i % 2 == 0) {
                            paint.setStrokeWidth(dp(1));
                            canvas.drawLine(x - radius * 2, y, x + radius * 2, y, paint);
                            canvas.drawLine(x, y - radius * 2, x, y + radius * 2, paint);
                        }
                    }
                }
                canvas.restoreToCount(saved);
                if (motion && hasWindowFocus() && getWindowVisibility() == View.VISIBLE && isShown()) postInvalidateDelayed(33);
            }
            super.onDraw(canvas);
        }
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

    View foldHeading(String title, String summary, int colour, boolean expanded, BoolSink changed) {
        LinearLayout row = new LinearLayout(activity);
        row.setGravity(Gravity.CENTER_VERTICAL);
        row.setMinimumHeight(dp(48));
        row.setTag("fold:" + title);
        row.addView(heading((expanded ? "▾ " : "▸ ") + title));
        if (summary != null && !summary.isEmpty()) {
            TextView status = label(summary, 13, colour);
            status.setGravity(Gravity.END);
            status.setPadding(dp(8), 0, 0, 0);
            row.addView(status, new LinearLayout.LayoutParams(0, -2, 1));
        }
        row.setFocusable(true);
        row.setOnClickListener(v -> changed.set(!expanded));
        return row;
    }

    /** A label (and hint) with a drawn switch; the whole row toggles. */
    View toggle(String title, String hint, boolean value, BoolSink sink) {
        LinearLayout row = new LinearLayout(activity);
        row.setTag(title);
        row.setGravity(Gravity.CENTER_VERTICAL);
        row.setPadding(0, dp(6), 0, dp(6));
        row.setMinimumHeight(dp(48));
        LinearLayout words = new LinearLayout(activity);
        words.setOrientation(LinearLayout.VERTICAL);
        words.setImportantForAccessibility(View.IMPORTANT_FOR_ACCESSIBILITY_NO_HIDE_DESCENDANTS);
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
        pill.setImportantForAccessibility(View.IMPORTANT_FOR_ACCESSIBILITY_NO);
        LinearLayout.LayoutParams lp = new LinearLayout.LayoutParams(dp(52), dp(30));
        lp.leftMargin = dp(12);
        row.addView(pill, lp);
        row.setFocusable(true);
        row.setContentDescription(hint == null || hint.isEmpty() ? title : title + "\n" + hint);
        row.setAccessibilityDelegate(new View.AccessibilityDelegate() {
            @Override public void onInitializeAccessibilityNodeInfo(View host, AccessibilityNodeInfo info) {
                super.onInitializeAccessibilityNodeInfo(host, info);
                info.setClassName(android.widget.Switch.class.getName());
                info.setCheckable(true);
                info.setChecked(on[0]);
            }

            @Override public void onInitializeAccessibilityEvent(View host, AccessibilityEvent event) {
                super.onInitializeAccessibilityEvent(host, event);
                event.setClassName(android.widget.Switch.class.getName());
                event.setChecked(on[0]);
            }
        });
        row.setOnClickListener(v -> {
            on[0] = !on[0];
            sink.set(on[0]);
            pill.invalidate();
            row.sendAccessibilityEvent(AccessibilityEvent.TYPE_WINDOW_CONTENT_CHANGED);
        });
        return row;
    }

    /** A tappable choice: title, hint, a stroke when chosen, faded when unavailable. */
    View choice(String title, String hint, boolean chosen, boolean available, Runnable action) {
        LinearLayout row = new LinearLayout(activity);
        row.setOrientation(LinearLayout.VERTICAL);
        row.setTag(title);
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
