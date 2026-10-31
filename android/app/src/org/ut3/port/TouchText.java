package org.ut3.port;

/** Uncommitted IME composition stays local; the game owns its committed text/caret. */
final class TouchText {
    interface Sink { boolean commit(String text); boolean delete(int before); }
    private final Sink sink;
    private String composing="";
    private String sent="";
    TouchText(Sink sink) { this.sink=sink; }
    static boolean valid(CharSequence text) {
        if(text==null||text.length()>256)return false;
        for(int i=0;i<text.length();++i) {
            char value=text.charAt(i);
            if(value<32||(value>=0x7F&&value<=0x9F)||value==0x2028||value==0x2029)return false;
            if(Character.isHighSurrogate(value)) {
                if(++i>=text.length()||!Character.isLowSurrogate(text.charAt(i)))return false;
            } else if(Character.isLowSurrogate(value))return false;
        }
        return true;
    }
    boolean compose(CharSequence text) { if(!valid(text))return false;composing=text.toString();return true; }
    boolean commit(CharSequence text) {
        if(!valid(text)||!sink.commit(text.toString()))return false;
        sent+=text;
        int start=Math.max(0,sent.length()-256);
        if(start>0&&Character.isLowSurrogate(sent.charAt(start)))++start;
        sent=sent.substring(start);
        composing="";return true;
    }
    boolean finish() { return composing.isEmpty()||commit(composing); }
    boolean delete(int before,int after) {
        if(before<0||before>256||after!=0)return false;
        if(before>0&&!sink.delete(before))return false;
        int keep=Math.max(0,sent.length()-before);
        sent=keep>0&&keep<sent.length()&&Character.isHighSurrogate(sent.charAt(keep-1))?"":sent.substring(0,keep);
        return true;
    }
    boolean deleteCodePoints(int before,int after) {
        if(before<0||before>256||after!=0)return false;
        // A single Backspace delegates character handling to the original widget.
        if(before==1)return delete(1,0);
        if(before>sent.length())return false;
        // Queued text is not widget echo: never infer surrogate deletion counts.
        for(int i=sent.length()-before;i<sent.length();++i)if(Character.isSurrogate(sent.charAt(i)))return false;
        return delete(before,0);
    }
    boolean backspace() {
        if(composing.isEmpty())return delete(1,0);
        int end=composing.length()-1;
        if(end>0&&Character.isLowSurrogate(composing.charAt(end)))--end;
        composing=composing.substring(0,end);return true;
    }
    CharSequence before(int length) { return composing.substring(Math.max(0,composing.length()-Math.max(0,Math.min(256,length)))); }
}
