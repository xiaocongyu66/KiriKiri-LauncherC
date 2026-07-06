package org.tvp.kirikiri2;

public class ShowTextInputTask implements Runnable {
    /*
     * This is used to regulate the pan&scan method to have some offset from
     * the bottom edge of the input region and the top edge of an input
     * method (soft keyboard)
     */
    static final int HEIGHT_PADDING = 15;
    public int x, y, w, h;

    public ShowTextInputTask(int x, int y, int w, int h) {
        this.x = x;
        this.y = y;
        this.w = w;
        this.h = h;
    }

    @Override
    public void run() {
        NativeUiHost.showTextInputNow(x, y, w, h);
    }
}
