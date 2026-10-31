package org.ut3.port;

import java.util.List;

/** Deterministic control placement and radial geometry, independent of Android for host checks. */
final class TouchLayout {
    private TouchLayout() {}
    /** Stable control ids; saved custom positions use NAMES, never these numbers. */
    static final int FIRE = 0, ALT = 1, JUMP = 2, DUCK = 3, USE = 4, WEAPON = 5, BOARD = 6, MORE = 7,
        PAUSE = 8, SCORES = 9, SETTINGS = 10, KEYBOARD = 11, MODE = 12, FIRE2 = 13, COUNT = 14;
    static final String[] NAMES = {"fire", "alt", "jump", "duck", "use", "weapon", "board", "more",
        "pause", "scores", "settings", "keyboard", "mode", "fire2"};
    /** Anchor corners of the safe area. */
    static final int BOTTOM_RIGHT = 0, BOTTOM_LEFT = 1, TOP_LEFT = 2, TOP_RIGHT = 3;
    static final int[] CORNER = {BOTTOM_RIGHT, BOTTOM_RIGHT, BOTTOM_RIGHT, BOTTOM_RIGHT, BOTTOM_RIGHT, BOTTOM_RIGHT,
        BOTTOM_LEFT, TOP_LEFT, TOP_LEFT, TOP_LEFT, TOP_RIGHT, TOP_RIGHT, TOP_LEFT, BOTTOM_LEFT};
    /**
     * Centres in dp from the anchor corner, and diameters. The right thumb's arc
     * holds the combat cluster: Fire in its middle, Jump below it, secondary fire
     * and crouch at the edge, weapon and use inward. A second Fire sits above the
     * left stick, so the left thumb can shoot while the right one aims.
     * Utilities keep the top row.
     */
    static final float[] DX = {140, 48, 140, 48, 230, 230, 58, 58, 30, 82, 34, 88, 30, 150};
    static final float[] DY = {142, 156, 50, 80, 218, 128, 196, 100, 30, 30, 34, 34, 84, 200};
    static final float[] SIZE = {92, 68, 70, 62, 62, 68, 50, 48, 44, 44, 44, 44, 44, 64};
    /** Smallest finger target, whatever the player's control size. */
    static final float MIN_DP = 44;
    /** The smallest designed combat control; below its scale the floor keeps the arc. */
    static final float SMALLEST_DP = 62;
    /** Combat controls follow the thumbs in left-handed mode; utilities keep their corner. */
    static boolean mirrored(int control) { return control <= MORE || control == FIRE2; }
    /** Order of placement: what the thumb uses most claims its place first. */
    static final int[] ORDER = {FIRE, JUMP, ALT, DUCK, WEAPON, USE, FIRE2, BOARD, MORE, PAUSE, SCORES, SETTINGS, KEYBOARD, MODE};
    /** Height of the default cluster in dp; scale is limited so it fits the safe area. */
    static final float CLUSTER_DP = 266;

    /** Largest control scale at which the right cluster still fits a short screen. */
    static float fitScale(float scale, float safeHeightDp) {
        float limit = Math.max(.65f, (safeHeightDp - 40) / CLUSTER_DP);
        return Math.max(.65f, Math.min(Float.isFinite(scale) ? scale : 1, limit));
    }

    /**
     * The W/A/S/D cross that replaces the floating stick when the player turns it
     * on: three cells of DPAD_CELL dp, centred above the corner where the stick
     * rests and clear of the original health panel; mirrored for the left hand.
     */
    static final float DPAD_CELL = 52, DPAD_DX = 132, DPAD_DY = 112;
    static float[] dpadAnchor(float left, float top, float right, float bottom, float density, float scale, boolean leftHanded) {
        float spacing = Math.max(scale, MIN_DP / SMALLEST_DP);
        float dx = DPAD_DX * density * spacing, dy = DPAD_DY * density * spacing;
        return new float[]{leftHanded ? right - dx : left + dx, bottom - dy};
    }
    static float dpadCell(float density, float scale) { return Math.max(MIN_DP, DPAD_CELL * scale) * density; }

    /** Default centre {x,y} in pixels for a control inside the safe rectangle. */
    static float[] anchor(int control, float left, float top, float right, float bottom, float density,
                          float scale, boolean leftHanded) {
        int corner = CORNER[control];
        if (leftHanded && mirrored(control))
            corner = corner == BOTTOM_RIGHT ? BOTTOM_LEFT : corner == BOTTOM_LEFT ? BOTTOM_RIGHT :
                corner == TOP_LEFT ? TOP_RIGHT : TOP_LEFT;
        // Below the smallest designed control the 44dp floor keeps sizes, so spacing stops shrinking too.
        float spacing = mirrored(control) ? Math.max(scale, MIN_DP / SMALLEST_DP) : 1;
        float dx = DX[control] * density * spacing;
        float dy = DY[control] * density * spacing;
        float x = corner == BOTTOM_RIGHT || corner == TOP_RIGHT ? right - dx : left + dx;
        float y = corner == BOTTOM_RIGHT || corner == BOTTOM_LEFT ? bottom - dy : top + dy;
        return new float[]{x, y};
    }
    static float size(int control, float density, float scale) {
        return Math.max(MIN_DP, SIZE[control] * (mirrored(control) ? scale : 1)) * density;
    }

    /**
     * The original HUD in this rectangle (the full game surface) at the
     * interface scale, measured on device at 100 and 150 percent: health and
     * ammo panels in the bottom corners, team scores at the top centre and the
     * vehicle minimap at the top right. Default controls keep clear of them; a
     * player's own placement is never moved. The weapon bar between the bottom
     * panels is informational and grows with the arsenal, so it is not reserved.
     */
    static void reserveHud(float left, float top, float right, float bottom, float uiScale, List<float[]> out) {
        float w = right - left, h = bottom - top;
        if (!(w > 0) || !(h > 0)) return;
        float s = Float.isFinite(uiScale) ? Math.max(1, Math.min(1.5f, uiScale)) : 1;
        out.add(new float[]{left, bottom - .072f * h * s, left + .19f * h * s, bottom});
        out.add(new float[]{right - .20f * h * s, bottom - .072f * h * s, right, bottom});
        out.add(new float[]{left + w * .5f - .46f * h * s, top, left + w * .5f + .36f * h * s, top + .095f * h * s});
        out.add(new float[]{right - .345f * h * s, top, right, top + .335f * h * s});
    }

    static boolean overlaps(float[] a, float[] b, float gap) {
        return a[0] < b[2] + gap && a[2] + gap > b[0] && a[1] < b[3] + gap && a[3] + gap > b[1];
    }
    private static boolean fits(float[] r, List<float[]> others, float gap) {
        if (others != null) for (float[] other : others) if (overlaps(r, other, gap)) return false;
        return true;
    }

    /**
     * Square {left,top,right,bottom} of the given size nearest to (cx,cy) inside
     * the area which overlaps neither occupied nor reserved rectangles; null
     * without room.
     */
    static float[] place(float cx, float cy, float size, float left, float top, float right, float bottom,
                         float step, float gap, List<float[]> occupied, List<float[]> reserved) {
        if (!(size > 0) || !(step > 0) || right - left < size || bottom - top < size) return null;
        cx = Math.max(left + size / 2, Math.min(right - size / 2, cx));
        cy = Math.max(top + size / 2, Math.min(bottom - size / 2, cy));
        float[] preferred = {cx - size / 2, cy - size / 2, cx + size / 2, cy + size / 2};
        if (fits(preferred, occupied, gap) && fits(preferred, reserved, gap)) return preferred;
        float[] best = null;
        float score = Float.POSITIVE_INFINITY;
        for (float y = top; y <= bottom - size + .01f; y += step) for (float x = left; x <= right - size + .01f; x += step) {
            float distance = (x + size / 2 - cx) * (x + size / 2 - cx) + (y + size / 2 - cy) * (y + size / 2 - cy);
            if (distance >= score) continue;
            float[] candidate = {x, y, x + size, y + size};
            if (fits(candidate, occupied, gap) && fits(candidate, reserved, gap)) { best = candidate; score = distance; }
        }
        return best;
    }

    /** The right thumb's combat cluster, moved as one group so muscle memory survives HUD clearance. */
    static final int[] CLUSTER = {FIRE, ALT, JUMP, DUCK, WEAPON, USE};
    /** Largest shared cluster shift as a fraction of the safe area. */
    static final float MAX_SHIFT_X = .15f, MAX_SHIFT_Y = .30f;
    /** Index of the vehicle minimap in reserveHud's list: shown only in vehicle game types. */
    static final int HUD_MINIMAP = 3;
    static boolean clustered(int control) { return control >= FIRE && control <= WEAPON; }

    /**
     * Smallest shared shift {dx,dy} of the given squares which keeps them inside
     * the area and clear of reserved rectangles: a vertical correction first,
     * then sideways toward the screen centre. Null without a fitting shift.
     */
    static float[] clusterShift(List<float[]> squares, float left, float top, float right, float bottom,
                                List<float[]> reserved, float gap, float step, boolean leftHanded) {
        if (squares.isEmpty() || !(step > 0)) return new float[]{0, 0};
        float minX = Float.POSITIVE_INFINITY, minY = Float.POSITIVE_INFINITY;
        float maxX = Float.NEGATIVE_INFINITY, maxY = Float.NEGATIVE_INFINITY;
        for (float[] r : squares) { minX = Math.min(minX, r[0]); minY = Math.min(minY, r[1]); maxX = Math.max(maxX, r[2]); maxY = Math.max(maxY, r[3]); }
        float loX = left - minX, hiX = right - maxX, loY = top - minY, hiY = bottom - maxY;
        if (loX > hiX + .01f || loY > hiY + .01f) return null;
        // Inward only, and never so far that the cluster leaves its thumb.
        float maxDx = MAX_SHIFT_X * (right - left), maxDy = MAX_SHIFT_Y * (bottom - top);
        if (leftHanded) { loX = Math.max(loX, 0); hiX = Math.min(hiX, maxDx); }
        else { hiX = Math.min(hiX, 0); loX = Math.max(loX, -maxDx); }
        loY = Math.max(loY, -maxDy); hiY = Math.min(hiY, maxDy);
        if (loX > hiX + .01f || loY > hiY + .01f) return null;
        float dx0 = Math.max(loX, Math.min(hiX, 0)), dy0 = Math.max(loY, Math.min(hiY, 0));
        float[] best = null;
        float score = Float.POSITIVE_INFINITY;
        if (shiftFits(squares, dx0, dy0, reserved, gap)) return new float[]{dx0, dy0};
        for (float dy = loY; dy <= hiY + .01f; dy += step) {
            float s = dy * dy;
            if (s < score && shiftFits(squares, dx0, dy, reserved, gap)) { score = s; best = new float[]{dx0, dy}; }
        }
        if (best != null) return best;
        for (float dy = loY; dy <= hiY + .01f; dy += step) for (float dx = loX; dx <= hiX + .01f; dx += step) {
            float s = dx * dx * 4 + dy * dy;
            if (s < score && shiftFits(squares, dx, dy, reserved, gap)) { score = s; best = new float[]{dx, dy}; }
        }
        return best;
    }
    private static boolean shiftFits(List<float[]> squares, float dx, float dy, List<float[]> reserved, float gap) {
        if (reserved == null) return true;
        for (float[] r : squares) {
            float[] moved = {r[0] + dx, r[1] + dy, r[2] + dx, r[3] + dy};
            for (float[] other : reserved) if (overlaps(moved, other, gap)) return false;
        }
        return true;
    }

    /**
     * The original weapon bar (native UTHUD DisplayWeaponBar with UTHUD's
     * defaults): HUDScale is ClipX/1280, each card WeaponBarScale x
     * WeaponBoxWidth = 75 units wide and 0.75 x 64 = 48 tall, the selected one
     * SelectedWeaponScale 1.5 times, the row centred plus WeaponBarXOffset 70
     * with its bottom WeaponBarY 16 above the edge. The port's HUD scale
     * multiplies it from the top-left origin with the clip divided, so the
     * centre and bottom stay anchored.
     */
    static final float BAR_PITCH = 75f / 1280, BAR_OFFSET = 70f / 1280, BAR_BOTTOM = 16f / 1280,
        BAR_HEIGHT = 48f / 1280, BAR_SELECTED = 1.5f;
    /** {left, top, right, bottom, pitch} of the settled bar on the full game surface; null without cards. */
    static float[] weaponBar(float left, float top, float right, float bottom, float uiScale, int count) {
        float w = right - left;
        if (!(w > 0) || !(bottom - top > 0) || count < 2) return null;
        float s = Float.isFinite(uiScale) ? Math.max(1, Math.min(1.5f, uiScale)) : 1;
        float pitch = BAR_PITCH * w * s, total = (count - 1 + BAR_SELECTED) * pitch;
        float x = left + w * .5f + s * BAR_OFFSET * w - total * .5f, y = bottom - s * BAR_BOTTOM * w;
        return new float[]{x, y - s * BAR_HEIGHT * w * BAR_SELECTED, x + total, y, pitch};
    }
    /** Slots 0-9 present in the bar's bit mask, left to right. */
    static int[] weaponSlots(int mask) {
        int[] slots = new int[Integer.bitCount(mask & 1023)];
        for (int slot = 0, i = 0; slot < 10; ++slot) if ((mask & (1 << slot)) != 0) slots[i++] = slot;
        return slots;
    }
    /** The card a swipe reaches: it starts on the selected card and moves one card per step. */
    static int weaponIndex(int start, float dx, float step, int count) {
        if (count < 1 || !(step > 0) || !Float.isFinite(dx)) return Math.max(0, Math.min(count - 1, start));
        return Math.max(0, Math.min(count - 1, start + Math.round(dx / step)));
    }

    /** Pie sector of a drag direction, 0 at twelve o'clock and clockwise; -1 when shorter than dead. */
    static int sector(float dx, float dy, int count, float dead) {
        if (count < 1 || !Float.isFinite(dx) || !Float.isFinite(dy) || Math.hypot(dx, dy) < dead) return -1;
        double width = Math.PI * 2 / count;
        double angle = Math.atan2(dx, -dy) + width / 2;
        if (angle < 0) angle += Math.PI * 2;
        return (int)Math.floor(angle / width) % count;
    }

    /** Absolute pie hit: CENTER inside the inner disc, -1 outside the pie, else the sector. */
    static final int CENTER = -2;
    static int pieHit(float x, float y, float cx, float cy, float inner, float outer, int count) {
        float dx = x - cx, dy = y - cy;
        double distance = Math.hypot(dx, dy);
        if (!Double.isFinite(distance) || distance > outer) return -1;
        if (distance < inner) return CENTER;
        return sector(dx, dy, count, 0);
    }

    /** Centre angle of a sector in radians, measured like sector(). */
    static double sectorAngle(int index, int count) { return Math.PI * 2 * index / count; }
}
