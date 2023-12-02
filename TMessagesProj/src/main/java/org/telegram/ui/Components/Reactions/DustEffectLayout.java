package org.telegram.ui.Components.Reactions;

import android.animation.ValueAnimator;
import android.app.Activity;
import android.content.Context;
import android.graphics.Bitmap;
import android.graphics.Canvas;
import android.graphics.Matrix;
import android.graphics.PixelFormat;
import android.util.Log;
import android.view.View;
import android.view.WindowManager;
import android.widget.FrameLayout;

import org.telegram.ui.ChatActivity;

import java.util.List;
import java.util.concurrent.ExecutorService;
import java.util.concurrent.Executors;
import java.util.concurrent.Future;

import static org.telegram.ui.Components.Reactions.ReactionsEffectOverlay.LONG_ANIMATION;
import static org.telegram.ui.Components.Reactions.ReactionsEffectOverlay.ONLY_MOVE_ANIMATION;

public class DustEffectLayout extends View {

    ExecutorService executor = Executors.newSingleThreadExecutor();
    private Future<?> task = null;
    private Bitmap src;
    private Bitmap[] dst = new Bitmap[2];
    private Bitmap[] views = null;
    private int[] areas = null; // array of quads [xmin, xmax, ymin, ymax] for simplify interaction with c++ part
    private int width = 0;
    private int height = 0;
    private long start = 0;
    private boolean useWindow;
    private int state = STATE_INIT;

    public DustEffectLayout(Context context) {
        super(context);
    }

    @Override
    protected void onDraw(Canvas canvas) {
        super.onDraw(canvas);
        if(state != STATE_BUF0 && state != STATE_BUF1) return;
        canvas.drawBitmap(dst[state], 0, 0, null);
        if (task != null && !task.isDone()) return;

        task = executor.submit(() -> {
            long before = System.currentTimeMillis();
            draw(dst[1 - state], src, areas, height, width, System.currentTimeMillis() - start);
            state = 1 - state;
            long now = System.currentTimeMillis();
            Log.v("!!!", "testlib time " + (now - before) + " ms");
            task = null;
        });
    }

    @Override
    protected void onSizeChanged(int w, int h, int oldw, int oldh) {
        //Log.v("!!!", "testlib onSizeChanged " + w + " x " + h);
        boolean same = (w == width && h == height);
        super.onSizeChanged(width = w, height = h, oldw, oldh);
        if(same || state == STATE_ERROR) return;
        try {
            src = Bitmap.createBitmap(h, w, Bitmap.Config.ARGB_8888);
            //two dst bitmap for allow computations for one bitmap, when another drawing on canvas
            dst[0] = Bitmap.createBitmap(w, h, Bitmap.Config.ARGB_8888);
            dst[1] = Bitmap.createBitmap(w, h, Bitmap.Config.ARGB_8888);
        } catch (OutOfMemoryError error) {
            state = STATE_ERROR;
            return;
        }
        int[] location = new int[2];
        getLocationOnScreen(location);

        //input bitmap is rotated for improve performance of C++ code
        Matrix matrix = new Matrix();
        matrix.setRotate(90f);
        matrix.postScale(-1, 1);

        Canvas canvas = new Canvas(src);
        canvas.setMatrix(matrix);

        for(int i = 0; i < views.length; i++)
            if(views[i] != null) canvas.drawBitmap(views[i], areas[i * 4] - location[0], areas[i * 4 + 1] - location[1], null);

        state = STATE_BUF0;
        if(start == 0L) start = System.currentTimeMillis();
    }

    public static void show(ChatActivity chatActivity, List<View> messages, int animationType) {
        if(messages == null || messages.isEmpty()) return;

        DustEffectLayout overlay = new DustEffectLayout(chatActivity.getParentActivity());
        overlay.useWindow = ((animationType == LONG_ANIMATION || animationType == ONLY_MOVE_ANIMATION) && chatActivity.scrimPopupWindow != null && chatActivity.scrimPopupWindow.isShowing());
        overlay.setMessages(messages);

        if (overlay.useWindow) {
            WindowManager.LayoutParams lp = new WindowManager.LayoutParams();
            lp.width = lp.height = WindowManager.LayoutParams.MATCH_PARENT;
            lp.type = WindowManager.LayoutParams.TYPE_APPLICATION_PANEL;
            lp.flags = WindowManager.LayoutParams.FLAG_NOT_FOCUSABLE | WindowManager.LayoutParams.FLAG_NOT_TOUCHABLE | WindowManager.LayoutParams.FLAG_LAYOUT_IN_SCREEN | WindowManager.LayoutParams.FLAG_LAYOUT_INSET_DECOR;
            lp.format = PixelFormat.TRANSLUCENT;
            chatActivity.getParentActivity().getWindowManager().addView(overlay, lp);
        } else {
            ((FrameLayout) chatActivity.getParentActivity().getWindow().getDecorView()).addView(overlay);
        }

        ValueAnimator animator = ValueAnimator.ofFloat(1.0f, 0.0f);
        animator.addUpdateListener(valueAnimator -> {
            float value = (float) animator.getAnimatedValue();
            overlay.invalidate();
            if(value != 0) return;
            overlay.remove();
        });
        animator.setDuration(2800L);
        animator.start();
    }

    private void setMessages(List<View> messages) {
        views = new Bitmap[messages.size()];
        areas = new int[messages.size() * 4];
        int[] location = new int[2];
        for(int i = 0; i < views.length; i++) {
            View view = messages.get(i);
            view.getLocationOnScreen(location);
            Bitmap bmp = Bitmap.createBitmap(view.getWidth(), view.getHeight(), Bitmap.Config.ARGB_8888);
            Canvas cvs = new Canvas(bmp);
            view.layout(view.getLeft(), view.getTop(), view.getRight(), view.getBottom());
            view.draw(cvs);
            views[i] = bmp;
            int j = i * 4;
            areas[j] = location[0];
            areas[j + 1] = location[1];
            areas[j + 2] = location[0] + bmp.getWidth();
            areas[j + 3] = location[1] + bmp.getHeight();
        }
    }

    private void remove() {
        Activity activity = (Activity) getContext();
        if(useWindow) activity.getWindowManager().removeView(this);
        else ((FrameLayout) activity.getWindow().getDecorView()).removeView(this);
    }

    public static native void draw(Bitmap dst, Bitmap src, int[] areas, int width, int height, long time);

    private static final int STATE_BUF0 = 0;
    private static final int STATE_BUF1 = 1;
    private static final int STATE_INIT = 2;
    private static final int STATE_ERROR = 3;
}
