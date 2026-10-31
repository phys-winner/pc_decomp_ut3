package org.ut3.port;

/**
 * One original-UI pointer. A finger moving over a menu only points at it, as a
 * mouse would: the original highlight follows, and in lists the row under the
 * finger is selected without being activated. A tap clicks, or changes the
 * option row under it. A sideways drag that starts on a slider adjusts it.
 */
final class UiTouchInput {
    /** Native probe flags: a tap clicks, list rows follow the finger, a drag adjusts, a tap changes the option. */
    static final int TAP = 1, ROWS = 2, DRAG = 4, OPTION = 8;
    /**
     * List requests beside -8..8 rows: from 64 until the gesture retires the
     * game highlights the row under the moving cursor (held beyond the list,
     * it steps); 65 taps the option row under the cursor.
     */
    static final int HOVER = 64, OPTION_TAP = 65;
    interface Sink {
        boolean begin(float x,float y);
        int control(); // 0 awaiting original hover, positive probe flags, negative retired.
        boolean pointer(float x,float y);
        boolean button(boolean down);
        boolean scroll(int request); // rows or a list request; 0 retires the gesture.
        void reset();
    }
    private final Sink sink;
    private boolean active,ended,classified,moved,dragging;
    private int flags;
    private float startX,startY,x,y,slop=8;

    UiTouchInput(Sink sink) { this.sink=sink; }
    void configure(float slop,float row) {
        if(Float.isFinite(slop)&&slop>0)this.slop=slop;
    }
    boolean active() { return active; }
    boolean waiting() { return active&&!classified; }
    /** True while polling must continue: the original hover is not acknowledged yet. */
    boolean polling() { return waiting(); }
    boolean begin(float x,float y) {
        if(active||!Float.isFinite(x)||!Float.isFinite(y))return false;
        if(!sink.begin(x,y))return false;
        active=true;ended=classified=moved=dragging=false;flags=0;
        startX=this.x=x;startY=this.y=y;
        poll();return active;
    }
    void move(float x,float y) {
        if(!active||ended||!Float.isFinite(x)||!Float.isFinite(y))return;
        this.x=x;this.y=y;
        advance();
    }
    void end(float x,float y) {
        if(!active||ended)return;
        if(Float.isFinite(x)&&Float.isFinite(y)){this.x=x;this.y=y;}
        ended=true;advance();
    }
    void poll() {
        if(!active)return;
        if(!classified) {
            int kind=sink.control();
            if(kind<0){cancel();return;}
            if(kind==0)return;
            flags=kind;classified=true;
            // Rows follow the finger from now on, in the game's own Tick.
            if((flags&ROWS)!=0&&!sink.scroll(HOVER)){cancel();return;}
        }
        advance();
    }
    private void advance() {
        if(!classified||!active)return;
        float dx=x-startX,dy=y-startY;
        if(!moved&&Math.hypot(dx,dy)>slop) {
            moved=true;
            // Only a sideways drag begun on a slider or scrollbar presses; anything else points.
            if((flags&DRAG)!=0&&Math.abs(dx)>Math.abs(dy)) {
                if(!sink.pointer(startX,startY)||!sink.button(true)){cancel();return;}
                dragging=true;
            }
        }
        if(!sink.pointer(x,y)){cancel();return;}
        if(!ended)return;
        if(dragging) {
            if(!sink.button(false)){cancel();return;}
        } else if(!moved) {
            if((flags&OPTION)!=0) {
                if(!sink.scroll(OPTION_TAP)){cancel();return;}
            } else if((flags&TAP)!=0&&(!sink.button(true)||!sink.button(false))){cancel();return;}
        }
        if(!sink.scroll(0)){cancel();return;}
        // Native queued taps may still await UI acknowledgement after Android UP.
        active=false;dragging=false;
    }
    void cancel() {
        if(active)sink.reset();
        active=ended=dragging=classified=moved=false;
    }
}
