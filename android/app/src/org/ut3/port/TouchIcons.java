package org.ut3.port;

import android.graphics.Canvas;
import android.graphics.Paint;
import android.graphics.Path;
import android.graphics.RectF;

/**
 * Vector icons for the touch controls, drawn in a 24x24 design box centred on
 * the control. Plain strokes and fills: no bitmaps, no language, crisp at every
 * control size and density.
 */
final class TouchIcons {
    private TouchIcons() {}
    static final int FIRE = 1, ALT = 2, JUMP = 3, DUCK = 4, USE = 5, EXIT = 6, UP = 7, DOWN = 8, BRAKE = 9,
        WEAPON = 10, SEATS = 11, BOARD = 12, MORE = 13, PAUSE = 14, SCORES = 15, MAP = 16, ORDERS = 17,
        BEST = 18, FEIGN = 19, TAUNT = 20, TITAN = 21, HORN = 22, SETTINGS = 23, KEYBOARD = 24, CLOSE = 25,
        BACK = 26, PLAY = 27, MENU = 28, DONE = 29, RESET = 30, DEPLOY = 31, UNDEPLOY = 32;
    /** The controls' fill colour, also used to cut details out of filled icons. */
    static final int BUTTON_RGB = 0x12161C;
    // UI thread only; created on the first draw.
    private static Path path;
    private static RectF oval;

    /**
     * Draws icon at (cx,cy); size is the side of the 24-unit design box. The
     * paint's style, stroke, cap, join and colour are restored afterwards.
     */
    static boolean draw(Canvas canvas, Paint paint, int icon, float cx, float cy, float size, int color) {
        if (icon < FIRE || icon > UNDEPLOY || !(size > 0)) return false;
        if (path == null) { path = new Path(); oval = new RectF(); }
        Paint.Style style = paint.getStyle();
        float stroke = paint.getStrokeWidth();
        Paint.Cap cap = paint.getStrokeCap();
        Paint.Join join = paint.getStrokeJoin();
        int previous = paint.getColor();
        float s = size / 24f;
        canvas.save();
        canvas.translate(cx - 12 * s, cy - 12 * s);
        canvas.scale(s, s);
        paint.setColor(color);
        paint.setStrokeWidth(2f); paint.setStrokeCap(Paint.Cap.ROUND); paint.setStrokeJoin(Paint.Join.ROUND);
        paint.setStyle(Paint.Style.STROKE);
        switch (icon) {
        case FIRE: bullet(canvas, paint); break;
        case ALT: crosshair(canvas, paint); break;
        case JUMP: arrowUp(canvas, paint, 12, 18, 4); line(canvas, paint, 5, 21.5f, 19, 21.5f); break;
        case DUCK: arrowDown(canvas, paint, 12, 2.5f, 15); line(canvas, paint, 5, 21, 19, 21); break;
        case USE: hand(canvas, paint); break;
        case EXIT: exit(canvas, paint); break;
        case UP: chevron(canvas, paint, 12, 6, true); chevron(canvas, paint, 12, 13, true); break;
        case DOWN: chevron(canvas, paint, 12, 11, false); chevron(canvas, paint, 12, 18, false); break;
        case BRAKE: brake(canvas, paint); break;
        case WEAPON: rifle(canvas, paint); break;
        case SEATS: seats(canvas, paint); break;
        case BOARD: board(canvas, paint); break;
        case MORE: fill(paint); for (int i = 0; i < 3; ++i) canvas.drawCircle(5.5f + 6.5f * i, 12, 2.2f, paint); break;
        case PAUSE: fill(paint); canvas.drawRect(6.5f, 5, 10, 19, paint); canvas.drawRect(14, 5, 17.5f, 19, paint); break;
        case SCORES: podium(canvas, paint); break;
        case MAP: map(canvas, paint); break;
        case ORDERS: flag(canvas, paint); break;
        case BEST: star(canvas, paint); break;
        case FEIGN: skull(canvas, paint); break;
        case TAUNT: taunt(canvas, paint); break;
        case TITAN: crown(canvas, paint); break;
        case HORN: horn(canvas, paint); break;
        case SETTINGS: gear(canvas, paint); break;
        case KEYBOARD: keyboard(canvas, paint); break;
        case CLOSE: line(canvas, paint, 6, 6, 18, 18); line(canvas, paint, 18, 6, 6, 18); break;
        case BACK: arrowLeft(canvas, paint); break;
        case PLAY: fill(paint); path.reset(); path.moveTo(7, 4.5f); path.lineTo(19.5f, 12); path.lineTo(7, 19.5f);
            path.close(); canvas.drawPath(path, paint); break;
        case MENU: for (int i = 0; i < 3; ++i) line(canvas, paint, 4.5f, 6.5f + 5.5f * i, 19.5f, 6.5f + 5.5f * i); break;
        case DONE: check(canvas, paint); break;
        case RESET: reset(canvas, paint); break;
        // Artillery legs: spread onto the ground, or folded under a rising arrow.
        case DEPLOY: arrowDown(canvas, paint, 12, 2.5f, 11); line(canvas, paint, 12, 14, 5, 20); line(canvas, paint, 12, 14, 19, 20);
            line(canvas, paint, 3, 21.5f, 21, 21.5f); break;
        case UNDEPLOY: arrowUp(canvas, paint, 12, 13, 2.5f); line(canvas, paint, 12, 16, 9.5f, 21.5f);
            line(canvas, paint, 12, 16, 14.5f, 21.5f); break;
        default: break;
        }
        canvas.restore();
        paint.setStyle(style); paint.setStrokeWidth(stroke); paint.setStrokeCap(cap); paint.setStrokeJoin(join);
        paint.setColor(previous);
        return true;
    }
    private static void fill(Paint paint) { paint.setStyle(Paint.Style.FILL); }
    private static void line(Canvas c, Paint p, float x0, float y0, float x1, float y1) { c.drawLine(x0, y0, x1, y1, p); }
    private static void bullet(Canvas c, Paint p) {
        fill(p);
        path.reset();
        path.moveTo(8.5f, 21); path.lineTo(8.5f, 10.5f);
        path.cubicTo(8.5f, 6.5f, 10, 3.5f, 12, 2);
        path.cubicTo(14, 3.5f, 15.5f, 6.5f, 15.5f, 10.5f);
        path.lineTo(15.5f, 21); path.close();
        c.drawPath(path, p);
        // The case mouth in the button colour, a gap rather than a line lost on black.
        int color = p.getColor();
        p.setStyle(Paint.Style.STROKE); p.setStrokeWidth(1.4f); p.setColor((color & 0xFF000000) | BUTTON_RGB);
        c.drawLine(8.5f, 16.5f, 15.5f, 16.5f, p);
        p.setColor(color); p.setStrokeWidth(2f);
    }
    private static void crosshair(Canvas c, Paint p) {
        c.drawCircle(12, 12, 7, p);
        line(c, p, 12, 1.5f, 12, 6); line(c, p, 12, 18, 12, 22.5f);
        line(c, p, 1.5f, 12, 6, 12); line(c, p, 18, 12, 22.5f, 12);
        fill(p); c.drawCircle(12, 12, 1.8f, p);
    }
    private static void arrowUp(Canvas c, Paint p, float x, float bottom, float top) {
        line(c, p, x, bottom, x, top);
        path.reset(); path.moveTo(x - 6, top + 6); path.lineTo(x, top); path.lineTo(x + 6, top + 6);
        c.drawPath(path, p);
    }
    private static void arrowDown(Canvas c, Paint p, float x, float top, float bottom) {
        line(c, p, x, top, x, bottom);
        path.reset(); path.moveTo(x - 6, bottom - 6); path.lineTo(x, bottom); path.lineTo(x + 6, bottom - 6);
        c.drawPath(path, p);
    }
    private static void chevron(Canvas c, Paint p, float x, float y, boolean up) {
        p.setStrokeWidth(2.4f);
        path.reset();
        if (up) { path.moveTo(x - 7, y + 6); path.lineTo(x, y); path.lineTo(x + 7, y + 6); }
        else { path.moveTo(x - 7, y - 6); path.lineTo(x, y); path.lineTo(x + 7, y - 6); }
        c.drawPath(path, p);
        p.setStrokeWidth(2f);
    }
    private static void arrowLeft(Canvas c, Paint p) {
        line(c, p, 20, 12, 5, 12);
        path.reset(); path.moveTo(11, 6); path.lineTo(5, 12); path.lineTo(11, 18);
        c.drawPath(path, p);
    }
    /** An open hand: the usual interaction sign. */
    private static void hand(Canvas c, Paint p) {
        p.setStrokeWidth(2.6f);
        line(c, p, 9, 13, 9, 5.5f);
        line(c, p, 12.2f, 13, 12.2f, 3.5f);
        line(c, p, 15.4f, 13, 15.4f, 4.5f);
        line(c, p, 18.4f, 14, 18.4f, 7.5f);
        line(c, p, 8.5f, 16.5f, 4.5f, 12);
        p.setStrokeWidth(2f);
        fill(p);
        oval.set(7.5f, 11.5f, 19.8f, 22); c.drawRoundRect(oval, 4.5f, 4.5f, p);
    }
    private static void exit(Canvas c, Paint p) {
        path.reset(); path.moveTo(13, 7); path.lineTo(13, 3.5f); path.lineTo(4, 3.5f); path.lineTo(4, 20.5f);
        path.lineTo(13, 20.5f); path.lineTo(13, 17); c.drawPath(path, p);
        line(c, p, 9, 12, 21, 12);
        path.reset(); path.moveTo(17, 8); path.lineTo(21, 12); path.lineTo(17, 16); c.drawPath(path, p);
    }
    private static void brake(Canvas c, Paint p) {
        c.drawCircle(12, 12, 6, p);
        oval.set(2, 2, 22, 22);
        c.drawArc(oval, 135, 90, false, p);
        c.drawArc(oval, -45, 90, false, p);
        line(c, p, 12, 8.5f, 12, 12.5f);
        fill(p); c.drawCircle(12, 15.3f, 1.1f, p);
    }
    /** A rifle pointing right: receiver, barrel, magazine and grip. */
    private static void rifle(Canvas c, Paint p) {
        fill(p);
        oval.set(2, 8.5f, 16.5f, 13.5f); c.drawRoundRect(oval, 1.6f, 1.6f, p);
        c.drawRect(16, 9.6f, 22, 11.6f, p);
        path.reset(); path.moveTo(5, 13); path.lineTo(3.8f, 19); path.lineTo(7.4f, 19); path.lineTo(8.6f, 13); path.close();
        c.drawPath(path, p);
        path.reset(); path.moveTo(10.6f, 13); path.lineTo(11.4f, 18.5f); path.lineTo(14, 18); path.lineTo(13.6f, 13); path.close();
        c.drawPath(path, p);
        p.setStyle(Paint.Style.STROKE); p.setStrokeWidth(1.6f);
        line(c, p, 6, 6.2f, 13, 6.2f);
        p.setStrokeWidth(2f);
    }
    private static void seats(Canvas c, Paint p) {
        arrowUp(c, p, 8, 20, 4);
        arrowDown(c, p, 16, 4, 20);
    }
    /** A hoverboard deck with its glow underneath. */
    private static void board(Canvas c, Paint p) {
        fill(p);
        oval.set(2, 10.5f, 22, 14.5f); c.drawRoundRect(oval, 2, 2, p);
        p.setStyle(Paint.Style.STROKE); p.setStrokeWidth(1.8f);
        line(c, p, 4.5f, 18.5f, 8, 18.5f); line(c, p, 10.3f, 18.5f, 13.7f, 18.5f); line(c, p, 16, 18.5f, 19.5f, 18.5f);
        line(c, p, 7, 21.5f, 9.5f, 21.5f); line(c, p, 14.5f, 21.5f, 17, 21.5f);
        p.setStrokeWidth(2f);
        line(c, p, 12, 3.5f, 12, 8); path.reset(); path.moveTo(9, 6); path.lineTo(12, 3); path.lineTo(15, 6);
        c.drawPath(path, p);
    }
    private static void podium(Canvas c, Paint p) {
        path.reset();
        path.moveTo(2.5f, 20.5f); path.lineTo(2.5f, 13.5f); path.lineTo(8.5f, 13.5f); path.lineTo(8.5f, 8);
        path.lineTo(15.5f, 8); path.lineTo(15.5f, 15.5f); path.lineTo(21.5f, 15.5f); path.lineTo(21.5f, 20.5f); path.close();
        c.drawPath(path, p);
        line(c, p, 8.5f, 13.5f, 8.5f, 20.5f); line(c, p, 15.5f, 15.5f, 15.5f, 20.5f);
        fill(p); star(c, p, 12, 3.4f, 2.6f);
    }
    private static void map(Canvas c, Paint p) {
        path.reset();
        path.moveTo(3, 6); path.lineTo(9, 3.5f); path.lineTo(15, 6); path.lineTo(21, 3.5f);
        path.lineTo(21, 18); path.lineTo(15, 20.5f); path.lineTo(9, 18); path.lineTo(3, 20.5f); path.close();
        c.drawPath(path, p);
        line(c, p, 9, 3.5f, 9, 18); line(c, p, 15, 6, 15, 20.5f);
    }
    private static void flag(Canvas c, Paint p) {
        line(c, p, 6, 3, 6, 21.5f);
        fill(p);
        path.reset(); path.moveTo(6, 4); path.lineTo(19, 4); path.lineTo(15.5f, 8.5f); path.lineTo(19, 13); path.lineTo(6, 13); path.close();
        c.drawPath(path, p);
    }
    private static void star(Canvas c, Paint p) { fill(p); star(c, p, 12, 12.5f, 10); }
    private static void star(Canvas c, Paint p, float cx, float cy, float r) {
        path.reset();
        for (int i = 0; i < 10; ++i) {
            double a = Math.PI * i / 5 - Math.PI / 2;
            float radius = i % 2 == 0 ? r : r * .45f;
            float x = cx + (float)Math.cos(a) * radius, y = cy + (float)Math.sin(a) * radius;
            if (i == 0) path.moveTo(x, y); else path.lineTo(x, y);
        }
        path.close();
        c.drawPath(path, p);
    }
    private static void skull(Canvas c, Paint p) {
        oval.set(4.5f, 3, 19.5f, 17); c.drawOval(oval, p);
        oval.set(8.5f, 15, 15.5f, 21); c.drawRoundRect(oval, 1.5f, 1.5f, p);
        line(c, p, 12, 17.5f, 12, 21);
        fill(p);
        c.drawCircle(9.2f, 10.5f, 2, p); c.drawCircle(14.8f, 10.5f, 2, p);
    }
    private static void taunt(Canvas c, Paint p) {
        oval.set(3, 4, 21, 16); c.drawRoundRect(oval, 3.5f, 3.5f, p);
        path.reset(); path.moveTo(8, 16); path.lineTo(7, 21); path.lineTo(12.5f, 16); c.drawPath(path, p);
        line(c, p, 12, 7, 12, 10.5f);
        fill(p); c.drawCircle(12, 13.2f, 1.2f, p);
    }
    private static void crown(Canvas c, Paint p) {
        fill(p);
        path.reset();
        path.moveTo(3, 17); path.lineTo(4, 6.5f); path.lineTo(8.5f, 11); path.lineTo(12, 4); path.lineTo(15.5f, 11);
        path.lineTo(20, 6.5f); path.lineTo(21, 17); path.close();
        c.drawPath(path, p);
        c.drawRect(3, 18.5f, 21, 20.5f, p);
    }
    private static void horn(Canvas c, Paint p) {
        fill(p);
        path.reset(); path.moveTo(3.5f, 9); path.lineTo(7.5f, 9); path.lineTo(12.5f, 5); path.lineTo(12.5f, 19);
        path.lineTo(7.5f, 15); path.lineTo(3.5f, 15); path.close();
        c.drawPath(path, p);
        p.setStyle(Paint.Style.STROKE);
        oval.set(9.5f, 8, 17.5f, 16); c.drawArc(oval, -50, 100, false, p);
        oval.set(7, 4.5f, 21.5f, 19.5f); c.drawArc(oval, -50, 100, false, p);
    }
    private static void gear(Canvas c, Paint p) {
        path.reset();
        for (int i = 0; i < 16; ++i) {
            double a = Math.PI * 2 * i / 16 - Math.PI / 16;
            float r = i % 2 == 0 ? 10 : 7.6f;
            double b = a + Math.PI * 2 / 16;
            float x0 = 12 + (float)Math.cos(a) * r, y0 = 12 + (float)Math.sin(a) * r;
            float x1 = 12 + (float)Math.cos(b) * r, y1 = 12 + (float)Math.sin(b) * r;
            if (i == 0) path.moveTo(x0, y0); else path.lineTo(x0, y0);
            path.lineTo(x1, y1);
        }
        path.close();
        c.drawPath(path, p);
        c.drawCircle(12, 12, 3.2f, p);
    }
    private static void keyboard(Canvas c, Paint p) {
        oval.set(2, 6, 22, 18.5f); c.drawRoundRect(oval, 2.5f, 2.5f, p);
        fill(p);
        for (int row = 0; row < 2; ++row) for (int i = 0; i < 5; ++i) c.drawCircle(5.5f + 3.25f * i, 9.6f + 3 * row, 1, p);
        c.drawRect(7.5f, 14.6f, 16.5f, 16.1f, p);
    }
    private static void check(Canvas c, Paint p) {
        p.setStrokeWidth(2.6f);
        path.reset(); path.moveTo(4.5f, 12.5f); path.lineTo(9.5f, 17.5f); path.lineTo(19.5f, 6.5f);
        c.drawPath(path, p);
        p.setStrokeWidth(2f);
    }
    private static void reset(Canvas c, Paint p) {
        oval.set(4.5f, 4.5f, 19.5f, 19.5f);
        c.drawArc(oval, -30, 290, false, p);
        path.reset(); path.moveTo(20.6f, 3.4f); path.lineTo(19.3f, 8.6f); path.lineTo(14.2f, 7.3f);
        c.drawPath(path, p);
    }
}
