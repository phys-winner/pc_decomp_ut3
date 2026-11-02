package org.ut3.port;

import java.io.File;
import org.ut3.port.launcher.TextureBackend;

/** Optional launcher job; the game always retains its ordinary DXT loader. */
public final class TextureConverter implements TextureBackend {
    static { System.loadLibrary("texconv"); }
    private static native long[] nativeScan(String game,String index,int archiveFd);
    private static native int nativeConvert(String game,int format,int threads,String index,int archiveFd);
    private static native int nativeUndo(String game);
    private static native void nativeCancel();
    private static native long[] nativeProgress();
    private static native String nativeMessage();
    @Override public Scan scan(File game) {
        return scan(game,null,-1);
    }
    @Override public Scan scan(File game,File index,int archiveFd) {
        Scan out=new Scan();long[] values=nativeScan(game.getAbsolutePath(),index==null?null:index.getAbsolutePath(),archiveFd);
        if(values==null||values.length!=5){out.failure=nativeMessage();return out;}
        out.textures=values[0];out.bytes=values[1];out.converted=(int)values[2];out.convertedTextures=values[3];out.convertedBytes=values[4];
        return out;
    }
    @Override public int convert(File game,int format,int threads) { return convert(game,format,threads,null,-1); }
    @Override public int convert(File game,int format,int threads,File index,int archiveFd) {
        return nativeConvert(game.getAbsolutePath(),format,threads,index==null?null:index.getAbsolutePath(),archiveFd);
    }
    @Override public int undo(File game) { return nativeUndo(game.getAbsolutePath()); }
    @Override public void cancel() { nativeCancel(); }
    @Override public long[] progress() { return nativeProgress(); }
    @Override public String message() { return nativeMessage(); }
}
