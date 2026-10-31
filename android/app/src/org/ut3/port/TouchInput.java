package org.ut3.port;

/** Pointer ownership and sampling, independent of Android and the render rate. */
final class TouchInput {
    static final int FIRE = 1, ALT = 2, JUMP = 4, DUCK = 8, USE = 16;
    static final int PREVIOUS = 32, NEXT = 64, PAUSE = 128, SCORES = 256, MAP = 512;
    static final int BOARD = 1024, HORN = 2048, BEST = 4096;
    static final int WEAPON1 = 1 << 13, HERO = 1 << 23, FEIGN = 1 << 24, COMMAND = 1 << 25;
    /** Taunt 1 (J) and the virtual pad's left shoulder, the original weapon picker. */
    static final int TAUNT = 1 << 26, PICKER = 1 << 27;
    /** What a contact does: drive the stick, turn the camera, or only hold its action. */
    static final int MOVEMENT = 1, LOOK = 2, BUTTON = 3, PASSIVE = 4;
    /**
     * Look acceleration: a drag slower than ACCEL_SLOW dp/ms turns linearly; the
     * gain then rises evenly to ACCEL_MAX at ACCEL_FAST, so a quick swipe across
     * half the screen turns around while slow aiming keeps its precision.
     */
    static final float ACCEL_SLOW = .5f, ACCEL_FAST = 2.5f, ACCEL_MAX = 2.5f;
    /** Speed smoothing time constant; touch samples arrive every 4-8 ms. */
    static final float ACCEL_SMOOTH_MS = 30;
    interface Sink {
        void submit(float moveX, float moveY, float lookX, float lookY, int held);
        /** Weapon picker direction, up and right positive; false rejects the gesture. */
        boolean stick(float x, float y);
        void reset();
    }
    private final Sink sink;
    private final int[] ids = new int[10], actions = new int[10], roles = new int[10];
    private final float[] x = new float[10], y = new float[10], speed = new float[10], untimed = new float[10];
    private final long[] times = new long[10];
    private int movement = -1, look = -1;
    private int latched, extHeld;
    /** A gamepad's left stick beside the touch contacts: it moves while no finger drives the stick. */
    private float extX, extY;
    private float anchorX, anchorY, moveX, moveY, stickX, stickY;
    private float radius = 60, gain = 2.2f, altGain = 1, density = 1, padDead = 1;
    private boolean invertY, acceleration = true, pad;

    TouchInput(Sink sink) { this.sink = sink; clear(); }

    void configure(float radiusPixels, float countsPerPixel, float alternativeGain, boolean invert) {
        radius = Math.max(1, finite(radiusPixels, 60));
        gain = Math.max(0.001f, finite(countsPerPixel, 2.2f));
        altGain = Math.max(0.1f, Math.min(3, finite(alternativeGain, 1)));
        invertY = invert;
    }
    void density(float value) { density = Math.max(.5f, Math.min(8, finite(value, 1))); }
    void acceleration(boolean enabled) { acceleration = enabled; }

    private static float finite(float value, float fallback) {
        return Float.isNaN(value) || Float.isInfinite(value) ? fallback : value;
    }
    /** Camera gain at a smoothed finger speed in dp/ms (1 when slow). */
    static float accelerationGain(float dpPerMs) {
        float t = (dpPerMs - ACCEL_SLOW) / (ACCEL_FAST - ACCEL_SLOW);
        return 1 + (ACCEL_MAX - 1) * (Float.isFinite(t) ? Math.max(0, Math.min(1, t)) : 0);
    }

    private int find(int id) {
        for (int i = 0; i < ids.length; ++i) if (ids[i] == id) return i;
        return -1;
    }

    /** Legacy form: a contact outside buttons drives the stick in its zone, else looks. */
    boolean down(int id, float px, float py, int action, boolean movementZone) {
        return down(id, px, py, action, action != 0 ? BUTTON : movementZone ? MOVEMENT : LOOK, -1);
    }

    /** time: MotionEvent time in ms for look acceleration, -1 when unknown. */
    boolean down(int id, float px, float py, int action, int role, long time) {
        if (id < 0 || find(id) >= 0 || !valid(px, py) || (action & ~((PICKER << 1) - 1)) != 0) return false;
        int slot = find(-1);
        if (slot < 0) return false;
        // The stick zone holds one stick; a second finger there turns the camera.
        if (role == MOVEMENT && movement >= 0) role = LOOK;
        ids[slot] = id; actions[slot] = action; roles[slot] = role; x[slot] = px; y[slot] = py;
        times[slot] = time; speed[slot] = untimed[slot] = 0;
        if (role == MOVEMENT) {
            movement = slot; anchorX = px; anchorY = py; moveX = moveY = 0;
        } else if (look < 0 && (role == LOOK || (role == BUTTON && (action == FIRE || action == ALT)))) look = slot;
        send(0, 0);
        return true;
    }

    void move(int id, float px, float py) { move(id, px, py, -1); }

    /** Call for every historical sample, then the current sample. */
    void move(int id, float px, float py, long time) {
        int slot = find(id);
        if (slot < 0 || !valid(px, py)) return;
        float dx = 0, dy = 0;
        if (slot == movement && pad) {
            // A W/A/S/D cross: full speed in eight directions, the diagonals between two buttons.
            float nx = px - anchorX, ny = anchorY - py;
            if (Math.hypot(nx, ny) < padDead) moveX = moveY = 0;
            else {
                double step = Math.PI / 4, angle = Math.round(Math.atan2(ny, nx) / step) * step;
                moveX = Math.abs(Math.cos(angle)) < 1e-3 ? 0 : (float)Math.cos(angle);
                moveY = Math.abs(Math.sin(angle)) < 1e-3 ? 0 : (float)Math.sin(angle);
            }
        } else if (slot == movement) {
            float nx = (px - anchorX) / radius, ny = (anchorY - py) / radius;
            float length = (float)Math.hypot(nx, ny);
            float magnitude = Math.max(0, Math.min(1, (length - 0.12f) / 0.88f));
            moveX = length > 0 ? nx / length * magnitude : 0;
            moveY = length > 0 ? ny / length * magnitude : 0;
        }
        if (slot == look) {
            float multiplier = ((held() & ALT) != 0 ? altGain : 1) * lookGain(slot, px - x[slot], py - y[slot], time);
            dx = (px - x[slot]) * gain * multiplier;
            dy = (py - y[slot]) * gain * multiplier * (invertY ? -1 : 1);
        }
        x[slot] = px; y[slot] = py;
        send(dx, dy);
    }

    /** Acceleration from the smoothed finger speed; untimed samples stay linear. */
    private float lookGain(int slot, float dx, float dy, long time) {
        if (!acceleration || time < 0) return 1;
        if (times[slot] < 0 || time < times[slot]) { times[slot] = time; untimed[slot] = speed[slot] = 0; return 1; }
        // Samples sharing a millisecond are timed together with the next one.
        untimed[slot] += (float)Math.hypot(dx, dy) / density;
        long dt = time - times[slot];
        if (dt > 0) {
            speed[slot] += (untimed[slot] / dt - speed[slot]) * (float)(1 - Math.exp(-dt / ACCEL_SMOOTH_MS));
            times[slot] = time; untimed[slot] = 0;
        }
        return accelerationGain(speed[slot]);
    }

    void relativeLook(float dx, float dy) {
        if (!valid(dx, dy)) return;
        float multiplier = (held() & ALT) != 0 ? altGain : 1;
        send(dx * multiplier, dy * multiplier * (invertY ? -1 : 1));
    }

    /** A movement contact on a fixed W/A/S/D cross centred at (cx, cy): no direction within dead
     *  pixels of the centre. False when the stick is already driven or the contact is not new. */
    boolean pad(int id, float px, float py, float cx, float cy, float dead, long time) {
        if (movement >= 0 || !down(id, px, py, 0, MOVEMENT, time)) return false;
        int slot = find(id);
        if (slot != movement) { up(id, px, py, time); return false; }
        anchorX = cx; anchorY = cy; padDead = Math.max(1, finite(dead, 1)); pad = true;
        move(id, px, py, time);
        return true;
    }
    boolean padActive() { return movement >= 0 && pad; }

    void up(int id, float px, float py) { up(id, px, py, -1); }

    void up(int id, float px, float py, long time) {
        int slot = find(id);
        if (slot < 0) return;
        move(id, px, py, time);
        boolean picker = (actions[slot] & PICKER) != 0;
        ids[slot] = -1; actions[slot] = roles[slot] = 0;
        if (movement == slot) { movement = -1; moveX = moveY = 0; pad = false; }
        if (look == slot) look = -1; // No mid-gesture ownership transfer.
        if (picker && (held() & PICKER) == 0) stickX = stickY = 0;
        send(0, 0);
    }

    /** Change what an owned contact holds, e.g. the weapon button entering the picker. */
    boolean hold(int id, int action) {
        int slot = find(id);
        if (slot < 0 || (action & ~((PICKER << 1) - 1)) != 0 || actions[slot] == action) return slot >= 0;
        boolean picker = (actions[slot] & PICKER) != 0;
        actions[slot] = action;
        if (picker && (held() & PICKER) == 0) stickX = stickY = 0;
        send(0, 0);
        return true;
    }

    /** Picker direction while some contact holds PICKER; neutral otherwise. */
    boolean stick(float sx, float sy) {
        if (!valid(sx, sy) || (held() & PICKER) == 0) sx = sy = 0;
        float length = (float)Math.hypot(sx, sy);
        if (length > 1) { sx /= length; sy /= length; }
        if (sx == stickX && sy == stickY) return true;
        stickX = sx; stickY = sy;
        return sink.stick(sx, sy);
    }

    void pulse(int action) {
        int base = held();
        if ((base & action) != 0) return;
        boolean finger = movement >= 0;
        sink.submit(finger ? moveX : extX, finger ? moveY : extY, 0, 0, base | action);
        send(0, 0);
    }

    void toggle(int action) { latched ^= action; send(0, 0); }

    /** A physical gamepad's stick and held actions (GameControls maps its buttons). */
    void external(float x, float y, int held) {
        if (!valid(x, y)) x = y = 0;
        float length = (float)Math.hypot(x, y);
        if (length > 1) { x /= length; y /= length; }
        held &= (PICKER << 1) - 1;
        if (x == extX && y == extY && held == extHeld) return;
        extX = x; extY = y; extHeld = held;
        send(0, 0);
    }
    boolean latched(int action) { return (latched & action) != 0; }

    int held() {
        int mask = latched | extHeld;
        for (int i = 0; i < ids.length; ++i) if (ids[i] >= 0) mask |= actions[i];
        return mask;
    }
    boolean owns(int id) { return find(id) >= 0; }
    float anchorX() { return anchorX; }
    float anchorY() { return anchorY; }
    float moveX() { return moveX; }
    float moveY() { return moveY; }
    float stickX() { return stickX; }
    float stickY() { return stickY; }
    boolean moving() { return movement >= 0; }
    boolean looking() { return look >= 0; }
    boolean hasContacts() {
        for (int id : ids) if (id >= 0) return true;
        return false;
    }
    private void send(float dx, float dy) {
        boolean finger = movement >= 0;
        sink.submit(finger ? moveX : extX, finger ? moveY : extY, dx, dy, held());
    }
    private static boolean valid(float x, float y) {
        return !Float.isNaN(x) && !Float.isNaN(y) && !Float.isInfinite(x) && !Float.isInfinite(y);
    }
    private void clear() {
        for (int i = 0; i < ids.length; ++i) { ids[i] = -1; actions[i] = roles[i] = 0; }
        movement = look = -1; moveX = moveY = stickX = stickY = 0; latched = 0; pad = false;
        extX = extY = 0; extHeld = 0;
    }
    void cancel() { clear(); sink.reset(); }
}
