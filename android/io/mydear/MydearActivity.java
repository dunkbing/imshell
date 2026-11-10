package io.mydear;

import android.app.NativeActivity;
import android.content.ClipData;
import android.content.ClipboardManager;
import android.content.Context;
import android.view.KeyEvent;
import android.view.inputmethod.InputMethodManager;
import java.util.concurrent.ConcurrentLinkedQueue;

// the activity of a mydear app: NativeActivity plus what the ndk cannot reach. the native
// side calls these over jni from its own thread
public class MydearActivity extends NativeActivity {
    private final ConcurrentLinkedQueue<Integer> chars = new ConcurrentLinkedQueue<>();

    // the native key event has no character: take it here, imgui still gets the key
    @Override
    public boolean dispatchKeyEvent(KeyEvent event) {
        if (event.getAction() == KeyEvent.ACTION_DOWN) {
            int c = event.getUnicodeChar(event.getMetaState());
            if (c >= 32 && c != 127)
                chars.offer(c);
        } else if (event.getAction() == KeyEvent.ACTION_MULTIPLE && event.getCharacters() != null) {
            event.getCharacters().codePoints().forEach(chars::offer);
        }
        return super.dispatchKeyEvent(event);
    }

    public int pollChar() {
        Integer c = chars.poll();
        return c == null ? 0 : c;
    }

    public void showKeyboard(boolean show) {
        runOnUiThread(() -> {
            InputMethodManager imm = (InputMethodManager) getSystemService(Context.INPUT_METHOD_SERVICE);
            if (show)
                imm.showSoftInput(getWindow().getDecorView(), 0);
            else
                imm.hideSoftInputFromWindow(getWindow().getDecorView().getWindowToken(), 0);
        });
    }

    public String getClipboard() {
        ClipboardManager cm = (ClipboardManager) getSystemService(Context.CLIPBOARD_SERVICE);
        ClipData clip = cm.getPrimaryClip();
        if (clip == null || clip.getItemCount() == 0)
            return "";
        CharSequence text = clip.getItemAt(0).coerceToText(this);
        return text == null ? "" : text.toString();
    }

    public void setClipboard(String text) {
        ClipboardManager cm = (ClipboardManager) getSystemService(Context.CLIPBOARD_SERVICE);
        cm.setPrimaryClip(ClipData.newPlainText("text", text));
    }

    public String filesDir() {
        return getFilesDir().getAbsolutePath();
    }
}
