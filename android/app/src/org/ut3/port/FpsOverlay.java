package org.ut3.port;

import android.content.Context;
import android.graphics.Bitmap;
import android.graphics.Canvas;
import android.graphics.Paint;
import android.graphics.Rect;
import android.graphics.RectF;
import android.graphics.Typeface;
import android.os.Handler;
import android.os.HandlerThread;
import android.os.Looper;
import android.os.SystemClock;
import android.view.View;

/** The launcher's FPS counter, with every renderer: DXVK's HUD items "fps" and "frametimes" redrawn over
 *  the game where and as DXVK draws them (HUD units from (8, 8), times the HUD scale; FpsCounter for the
 *  numbers), from the intervals of the presented frames that android_main.c records; with the system
 *  lines (SystemInfo) below them when asked for. Redrawn 20 times a second, the frame rate every half
 *  second as DXVK's, the system lines every second off the UI thread. A string is one text run and the
 *  graph one bitmap, so a redraw costs a few dozen drawing calls. It sits under the touch controls and
 *  takes no touches. */
final class FpsOverlay extends View {
    /** Copies the newest frame intervals (microseconds, oldest first) and returns how many were recorded. */
    interface Source { long read(int[] intervalsUs); }

    private static final long REDRAW_MS = 50, SYSTEM_MS = 1000;
    /** System lines: label and value columns, in the 12-unit text of DXVK's min/max line. */
    private static final float LINE = 15, VALUE_X = 81;
    private static final int GRAPH_HEIGHT = 40;

    private final Source source;
    private final SystemInfo system;
    private final float unit;
    private final int[] intervals = new int[FpsCounter.POINTS];
    private final int[] graphPixels = new int[FpsCounter.POINTS * GRAPH_HEIGHT];
    private final Bitmap graph = Bitmap.createBitmap(FpsCounter.POINTS, GRAPH_HEIGHT, Bitmap.Config.ARGB_8888);
    private final Rect graphSource = new Rect(0, 0, FpsCounter.POINTS, GRAPH_HEIGHT);
    private final RectF graphTarget = new RectF();
    private final Paint fill = new Paint(Paint.ANTI_ALIAS_FLAG), outline = new Paint(Paint.ANTI_ALIAS_FLAG), pixels = new Paint();
    private final Handler handler = new Handler(Looper.getMainLooper());
    private HandlerThread sampler;
    private Handler samplerHandler;
    private volatile String[][] lines = new String[0][];
    private long sampleFrames = -1, sampleUs, min, max;
    private String frameRate = FpsCounter.frameRate(0, 0);
    private boolean running;
    private final Runnable tick = new Runnable() {
        @Override public void run() {
            refresh();
            if (running) handler.postDelayed(this, REDRAW_MS);
        }
    };
    private final Runnable sampleSystem = new Runnable() {
        @Override public void run() {
            try {
                lines = system.sample();
            } catch (RuntimeException e) {
                android.util.Log.w("UT3", "FPS counter system lines: " + e);
            }
            Handler h = samplerHandler;
            if (h != null) h.postDelayed(this, SYSTEM_MS);
        }
    };

    /** system: null for the frame rate and frame times only. */
    FpsOverlay(Context context, float scale, Source source, SystemInfo system) {
        super(context);
        this.unit = scale;
        this.source = source;
        this.system = system;
        Typeface mono = Typeface.create(Typeface.MONOSPACE, Typeface.BOLD);
        fill.setTypeface(mono);
        outline.setTypeface(mono);
        outline.setStyle(Paint.Style.STROKE);
        outline.setStrokeJoin(Paint.Join.ROUND);
        outline.setColor(0xFF000000);
        setClickable(false);
        setFocusable(false);
        setImportantForAccessibility(IMPORTANT_FOR_ACCESSIBILITY_NO);
    }

    void start() {
        if (running) return;
        running = true;
        sampleFrames = -1;
        handler.post(tick);
        if (system != null && sampler == null) {
            sampler = new HandlerThread("fps-counter");
            sampler.start();
            samplerHandler = new Handler(sampler.getLooper());
            samplerHandler.post(sampleSystem);
        }
    }

    void stop() {
        running = false;
        handler.removeCallbacks(tick);
        if (sampler != null) {
            samplerHandler.removeCallbacks(sampleSystem);
            samplerHandler = null;
            sampler.quitSafely();
            sampler = null;
        }
    }

    private void refresh() {
        long now = SystemClock.elapsedRealtimeNanos() / 1000, frames = source.read(intervals);
        if (sampleFrames < 0 || frames < sampleFrames) {
            sampleFrames = frames;
            sampleUs = now;
        } else if (now - sampleUs >= FpsCounter.UPDATE_US) {
            frameRate = FpsCounter.frameRate(frames - sampleFrames, now - sampleUs);
            sampleFrames = frames;
            sampleUs = now;
        }
        /* HudFrameTimeItem::render: each frame a column of the 304 x 40 graph, filled from the bottom */
        long low = Long.MAX_VALUE, high = 0;
        for (int i = 0; i < FpsCounter.POINTS; ++i) {
            int us = intervals[i];
            long tenths = FpsCounter.tenths(us);
            low = Math.min(low, tenths);
            high = Math.max(high, tenths);
            int top = GRAPH_HEIGHT - Math.max(1, Math.round(GRAPH_HEIGHT * FpsCounter.height(us))), color = FpsCounter.color(us);
            for (int y = 0; y < GRAPH_HEIGHT; ++y) graphPixels[y * FpsCounter.POINTS + i] = y >= top ? color : 0;
        }
        graph.setPixels(graphPixels, 0, FpsCounter.POINTS, 0, 0, FpsCounter.POINTS, GRAPH_HEIGHT);
        min = low;
        max = high;
        invalidate(0, 0, (int) Math.ceil(width() * unit), (int) Math.ceil((106 + LINE * lines.length) * unit));
    }

    /** The overlay's width in HUD units: the graph, or the longest system line. */
    private float width() {
        float widest = 8 + FpsCounter.POINTS + 8;
        for (String[] line : lines) widest = Math.max(widest, 8 + VALUE_X + 0.75f * 12 * line[1].length() + 8);
        return widest;
    }

    /** DXVK's HudRenderer::drawText: size in HUD units, (x, y) the baseline's start, one character every
     *  0.75 of the size (its font: advance 24 at size 32) by letter spacing, glyphs in a black outline. */
    private void text(Canvas canvas, String value, float x, float y, float size, int color) {
        float px = size * unit;
        if (fill.getTextSize() != px) {
            fill.setTextSize(px);
            fill.setLetterSpacing(0);
            float spacing = 0.75f - fill.measureText("0") / px;
            fill.setLetterSpacing(spacing);
            outline.setTextSize(px);
            outline.setLetterSpacing(spacing);
            outline.setStrokeWidth(px * 0.12f);
        }
        fill.setColor(color);
        canvas.drawText(value, x * unit, y * unit, outline);
        canvas.drawText(value, x * unit, y * unit, fill);
    }

    @Override protected void onDraw(Canvas canvas) {
        float x = 8, y = 8;
        /* HudFpsItem::render */
        y += 16;
        text(canvas, "FPS:", x, y, 16, FpsCounter.LABEL);
        text(canvas, frameRate, x + 60, y, 16, FpsCounter.VALUE);
        y += 8;
        graphTarget.set(x * unit, y * unit, (x + FpsCounter.POINTS) * unit, (y + GRAPH_HEIGHT) * unit);
        canvas.drawBitmap(graph, graphSource, graphTarget, pixels);
        y += 58;
        text(canvas, "min:", x, y, 12, FpsCounter.LABEL);
        text(canvas, FpsCounter.milliseconds(min), x + 45, y, 12, FpsCounter.VALUE);
        text(canvas, "max:", x + 150, y, 12, FpsCounter.LABEL);
        text(canvas, FpsCounter.milliseconds(max), x + 195, y, 12, FpsCounter.VALUE);
        y += 4;
        /* The system lines, in the style of DXVK's other items */
        for (String[] line : lines) {
            y += LINE;
            text(canvas, line[0], x, y, 12, FpsCounter.LABEL);
            text(canvas, line[1], x + VALUE_X, y, 12, FpsCounter.VALUE);
        }
    }
}
