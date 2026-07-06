package org.tvp.kirikiri2;

import android.app.Activity;
import android.content.Context;
import android.os.Handler;
import android.os.Looper;
import android.view.View;
import android.view.ViewGroup;
import android.view.inputmethod.InputMethodManager;
import android.widget.FrameLayout;
import android.widget.Toast;

public final class NativeUiHost {
    private static final Object LOCK = new Object();
    private static final Handler MAIN_HANDLER = new Handler(Looper.getMainLooper());
    private static final DialogMessage DIALOG_MESSAGE = new DialogMessage();

    private static Activity currentActivity;
    private static FrameLayout inputLayer;
    private static View coordinateView;
    private static int logicalWidth;
    private static int logicalHeight;
    private static View textEdit;

    private NativeUiHost() {
    }

    public static void attach(Activity activity, FrameLayout layer) {
        if (activity == null || layer == null) {
            return;
        }
        synchronized (LOCK) {
            currentActivity = activity;
            inputLayer = layer;
            coordinateView = layer;
            logicalWidth = 0;
            logicalHeight = 0;
        }
    }

    public static void attach(Activity activity, FrameLayout layer, View coordinates,
                              int logicalW, int logicalH) {
        if (activity == null || layer == null) {
            return;
        }
        synchronized (LOCK) {
            currentActivity = activity;
            inputLayer = layer;
            coordinateView = coordinates != null ? coordinates : layer;
            logicalWidth = Math.max(0, logicalW);
            logicalHeight = Math.max(0, logicalH);
        }
    }

    public static void detach(Activity activity) {
        MAIN_HANDLER.post(() -> {
            synchronized (LOCK) {
                if (activity != null && currentActivity != activity) {
                    return;
                }
                hideTextInputNowLocked();
                removeTextEditLocked();
                inputLayer = null;
                coordinateView = null;
                logicalWidth = 0;
                logicalHeight = 0;
                currentActivity = null;
            }
        });
    }

    public static Activity getActivityContextOrNull() {
        synchronized (LOCK) {
            return currentActivity;
        }
    }

    public static Activity requireActivityContext() {
        Activity activity = getActivityContextOrNull();
        if (activity == null) {
            throw new IllegalStateException("native UI host is not attached");
        }
        return activity;
    }

    public static Context requireApplicationContext() {
        return requireActivityContext().getApplicationContext();
    }

    public static boolean isTextInputVisible() {
        synchronized (LOCK) {
            return textEdit != null && textEdit.getVisibility() == View.VISIBLE;
        }
    }

    public static void showTextInput(int x, int y, int w, int h) {
        MAIN_HANDLER.post(new ShowTextInputTask(x, y, w, h));
    }

    static void showTextInputNow(int x, int y, int w, int h) {
        synchronized (LOCK) {
            Activity activity = currentActivity;
            FrameLayout layer = inputLayer;
            if (activity == null || layer == null) {
                return;
            }

            FrameLayout.LayoutParams params = makeInputLayoutParamsLocked(x, y, w, h);

            if (textEdit == null || textEdit.getContext() != activity) {
                removeTextEditLocked();
                textEdit = new DummyEdit(activity);
                layer.addView(textEdit, params);
            } else if (textEdit.getParent() == null) {
                layer.addView(textEdit, params);
            } else {
                textEdit.setLayoutParams(params);
            }

            textEdit.setVisibility(View.VISIBLE);
            textEdit.requestFocus();

            InputMethodManager imm = (InputMethodManager) activity.getSystemService(
                    Context.INPUT_METHOD_SERVICE);
            if (imm != null) {
                imm.showSoftInput(textEdit, 0);
            }
        }
    }

    private static FrameLayout.LayoutParams makeInputLayoutParamsLocked(
            int x, int y, int w, int h) {
        int left = x;
        int top = y;
        int width = w;
        int height = h + ShowTextInputTask.HEIGHT_PADDING;
        View coordinates = coordinateView;
        if (coordinates != null && logicalWidth > 0 && logicalHeight > 0 &&
                coordinates.getWidth() > 0 && coordinates.getHeight() > 0) {
            float scaleX = coordinates.getWidth() / (float) logicalWidth;
            float scaleY = coordinates.getHeight() / (float) logicalHeight;
            left = coordinates.getLeft() + Math.round(x * scaleX);
            top = coordinates.getTop() + Math.round(y * scaleY);
            width = Math.max(1, Math.round(w * scaleX));
            height = Math.max(1, Math.round((h + ShowTextInputTask.HEIGHT_PADDING) * scaleY));
        }
        FrameLayout.LayoutParams params = new FrameLayout.LayoutParams(width, height);
        params.leftMargin = left;
        params.topMargin = top;
        return params;
    }

    public static void hideTextInput() {
        MAIN_HANDLER.post(() -> {
            synchronized (LOCK) {
                hideTextInputNowLocked();
            }
        });
    }

    private static void hideTextInputNowLocked() {
        if (textEdit == null) {
            return;
        }
        textEdit.setVisibility(View.GONE);
        Activity activity = currentActivity;
        if (activity != null) {
            InputMethodManager imm = (InputMethodManager) activity.getSystemService(
                    Context.INPUT_METHOD_SERVICE);
            if (imm != null) {
                imm.hideSoftInputFromWindow(textEdit.getWindowToken(), 0);
            }
        }
    }

    private static void removeTextEditLocked() {
        if (textEdit == null) {
            return;
        }
        ViewGroup parent = (ViewGroup) textEdit.getParent();
        if (parent != null) {
            parent.removeView(textEdit);
        }
        textEdit = null;
    }

    public static void showMessageBox(String title, String text, String[] buttons) {
        DIALOG_MESSAGE.Init(title, text, buttons);
        MAIN_HANDLER.post(DIALOG_MESSAGE::ShowMessageBox);
    }

    public static void showInputBox(String title, String prompt, String text, String[] buttons) {
        DIALOG_MESSAGE.Init(title, prompt, buttons);
        MAIN_HANDLER.post(() -> DIALOG_MESSAGE.ShowInputBox(text));
    }

    public static void showPluginToast(String title, String text) {
        MAIN_HANDLER.post(() -> {
            Activity activity = getActivityContextOrNull();
            if (activity == null) {
                return;
            }
            String message = "";
            if (title != null && !title.isEmpty()) {
                message = title;
            }
            if (text != null && !text.isEmpty()) {
                message = message.isEmpty() ? text : message + "\n" + text;
            }
            if (!message.isEmpty()) {
                Toast.makeText(activity, message, Toast.LENGTH_LONG).show();
            }
        });
    }

    static boolean dispatchKeyAction(int keyCode, boolean pressed) {
        return nativeKeyAction(keyCode, pressed);
    }

    static void dispatchCharInput(int keyCode) {
        nativeCharInput(keyCode);
    }

    static void dispatchCommitText(String text, int newCursorPosition) {
        nativeCommitText(text, newCursorPosition);
    }

    static void dispatchDeleteBackward() {
        nativeDeleteBackward();
    }

    static void onMessageBoxOK(int button) {
        nativeMessageBoxOK(button);
    }

    static void onMessageBoxText(String text) {
        nativeMessageBoxText(text);
    }

    private static native boolean nativeKeyAction(int keyCode, boolean pressed);
    private static native void nativeCharInput(int keyCode);
    private static native void nativeCommitText(String text, int newCursorPosition);
    private static native void nativeDeleteBackward();
    private static native void nativeMessageBoxOK(int button);
    private static native void nativeMessageBoxText(String text);
}
