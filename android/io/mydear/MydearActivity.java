package io.mydear;

import android.app.NativeActivity;
import android.content.ClipData;
import android.content.ClipboardManager;
import android.content.Context;
import android.content.Intent;
import android.database.Cursor;
import android.net.Uri;
import android.provider.OpenableColumns;
import android.view.KeyEvent;
import android.view.inputmethod.InputMethodManager;
import java.io.File;
import java.io.FileOutputStream;
import java.io.InputStream;
import java.io.OutputStream;
import java.util.concurrent.ConcurrentLinkedQueue;

// the activity of a mydear app: NativeActivity plus what the ndk cannot reach. the native
// side calls these over jni from its own thread
public class MydearActivity extends NativeActivity {
    private static final int PICK = 0x6d79;
    private final ConcurrentLinkedQueue<Integer> chars = new ConcurrentLinkedQueue<>();
    private final ConcurrentLinkedQueue<String> picked = new ConcurrentLinkedQueue<>();
    private String pickInto;

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

    // the document picker; the result lands in pollPicked: a path, or "" when cancelled
    public void pickFile(String mimeTypes, String intoDir) {
        pickInto = intoDir;
        runOnUiThread(() -> {
            Intent intent = new Intent(Intent.ACTION_OPEN_DOCUMENT);
            intent.addCategory(Intent.CATEGORY_OPENABLE);
            intent.setType("*/*");
            if (!mimeTypes.isEmpty())
                intent.putExtra(Intent.EXTRA_MIME_TYPES, mimeTypes.split(","));
            startActivityForResult(intent, PICK);
        });
    }

    public String pollPicked() {
        return picked.poll();
    }

    @Override
    protected void onActivityResult(int request, int result, Intent data) {
        super.onActivityResult(request, result, data);
        if (request != PICK)
            return;
        Uri uri = result == RESULT_OK && data != null ? data.getData() : null;
        if (uri == null) {
            picked.offer("");
            return;
        }
        new Thread(() -> picked.offer(copy(uri))).start();
    }

    private String copy(Uri uri) {
        String name = "picked";
        try (Cursor c = getContentResolver().query(uri, new String[] {OpenableColumns.DISPLAY_NAME},
                                                   null, null, null)) {
            if (c != null && c.moveToFirst() && c.getString(0) != null)
                name = new File(c.getString(0)).getName();
        } catch (Exception ignored) {
        }
        File dir = new File(pickInto);
        dir.mkdirs();
        File out = new File(dir, name);
        try (InputStream in = getContentResolver().openInputStream(uri);
             OutputStream os = new FileOutputStream(out)) {
            byte[] buf = new byte[1 << 16];
            for (int n; (n = in.read(buf)) > 0;)
                os.write(buf, 0, n);
            return out.getAbsolutePath();
        } catch (Exception e) {
            return "";
        }
    }
}
