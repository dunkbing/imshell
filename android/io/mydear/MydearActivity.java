package io.mydear;

import android.app.NativeActivity;
import android.content.ClipData;
import android.content.ClipboardManager;
import android.content.Context;
import android.content.Intent;
import android.graphics.Color;
import android.graphics.Insets;
import android.database.Cursor;
import android.net.Uri;
import android.os.Build;
import android.os.Bundle;
import android.view.DisplayCutout;
import android.view.WindowInsets;
import android.view.WindowInsetsController;
import android.view.WindowManager;
import android.graphics.Rect;
import android.provider.OpenableColumns;
import android.view.ActionMode;
import android.view.HapticFeedbackConstants;
import android.view.KeyEvent;
import android.view.Menu;
import android.view.MenuItem;
import android.view.View;
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
    private final ConcurrentLinkedQueue<Integer> edits = new ConcurrentLinkedQueue<>();
    private ActionMode editMenu;
    private String pickInto;
    // left, top, right, bottom (system bars, cutout), keyboard height; window pixels
    private volatile int[] insets = new int[5];

    // edge to edge on every version (api 35+ targets get it anyway): the surface covers the
    // whole window, and the shell keeps imgui clear of what the insets say is covered
    @Override
    protected void onCreate(Bundle state) {
        super.onCreate(state);
        getWindow().getAttributes().layoutInDisplayCutoutMode =
            WindowManager.LayoutParams.LAYOUT_IN_DISPLAY_CUTOUT_MODE_SHORT_EDGES;
        View decor = getWindow().getDecorView();
        if (Build.VERSION.SDK_INT >= 30) {
            getWindow().setDecorFitsSystemWindows(false);
        } else {
            decor.setSystemUiVisibility(View.SYSTEM_UI_FLAG_LAYOUT_STABLE
                | View.SYSTEM_UI_FLAG_LAYOUT_FULLSCREEN | View.SYSTEM_UI_FLAG_LAYOUT_HIDE_NAVIGATION);
        }
        if (Build.VERSION.SDK_INT < 35) { // 35+ ignores these: its bars are transparent
            getWindow().setStatusBarColor(Color.TRANSPARENT);
            getWindow().setNavigationBarColor(Color.TRANSPARENT);
        }
        decor.setOnApplyWindowInsetsListener((v, in) -> {
            int[] next;
            if (Build.VERSION.SDK_INT >= 30) {
                Insets bars = in.getInsets(WindowInsets.Type.systemBars() | WindowInsets.Type.displayCutout());
                next = new int[] {bars.left, bars.top, bars.right, bars.bottom,
                                  in.getInsets(WindowInsets.Type.ime()).bottom};
            } else {
                // shortcut: before api 30 the keyboard is part of the bottom inset; untested (no 28/29 image)
                next = new int[] {in.getSystemWindowInsetLeft(), in.getSystemWindowInsetTop(),
                                  in.getSystemWindowInsetRight(), in.getSystemWindowInsetBottom(), 0};
                DisplayCutout c = in.getDisplayCutout();
                if (c != null) {
                    next[0] = Math.max(next[0], c.getSafeInsetLeft());
                    next[1] = Math.max(next[1], c.getSafeInsetTop());
                    next[2] = Math.max(next[2], c.getSafeInsetRight());
                    next[3] = Math.max(next[3], c.getSafeInsetBottom());
                }
            }
            insets = next;
            return v.onApplyWindowInsets(in);
        });
    }

    public int[] windowInsets() {
        return insets;
    }

    // dark bar icons over a light theme
    public void setLightBars(boolean light) {
        runOnUiThread(() -> {
            if (Build.VERSION.SDK_INT >= 30) {
                int both = WindowInsetsController.APPEARANCE_LIGHT_STATUS_BARS
                    | WindowInsetsController.APPEARANCE_LIGHT_NAVIGATION_BARS;
                getWindow().getInsetsController().setSystemBarsAppearance(light ? both : 0, both);
            } else {
                View decor = getWindow().getDecorView();
                int flags = decor.getSystemUiVisibility();
                flags = light ? flags | View.SYSTEM_UI_FLAG_LIGHT_STATUS_BAR
                              : flags & ~View.SYSTEM_UI_FLAG_LIGHT_STATUS_BAR;
                decor.setSystemUiVisibility(flags);
            }
        });
    }

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


    // the system's floating text toolbar at (x, y), window pixels. a pick lands in pollEdit as
    // the letter of its shortcut: 'X' cut, 'C' copy, 'V' paste, 'A' select all
    // the long-press tick other apps give
    public void longPressFeedback() {
        runOnUiThread(() -> getWindow().getDecorView().performHapticFeedback(HapticFeedbackConstants.LONG_PRESS));
    }

    public void showEditMenu(int x, int y) {
        runOnUiThread(() -> {
            if (editMenu != null)
                editMenu.finish();
            View decor = getWindow().getDecorView();
            editMenu = decor.startActionMode(new ActionMode.Callback2() {
                @Override
                public boolean onCreateActionMode(ActionMode mode, Menu menu) {
                    menu.add(0, 'X', 0, android.R.string.cut);
                    menu.add(0, 'C', 1, android.R.string.copy);
                    ClipboardManager cm = (ClipboardManager) getSystemService(Context.CLIPBOARD_SERVICE);
                    if (cm.hasPrimaryClip())
                        menu.add(0, 'V', 2, android.R.string.paste);
                    menu.add(0, 'A', 3, android.R.string.selectAll);
                    return true;
                }

                @Override
                public boolean onPrepareActionMode(ActionMode mode, Menu menu) {
                    return false;
                }

                @Override
                public boolean onActionItemClicked(ActionMode mode, MenuItem item) {
                    edits.offer(item.getItemId());
                    mode.finish();
                    return true;
                }

                @Override
                public void onDestroyActionMode(ActionMode mode) {
                    if (editMenu == mode)
                        editMenu = null;
                }

                @Override
                public void onGetContentRect(ActionMode mode, View view, Rect outRect) {
                    outRect.set(x, y, x + 1, y + 1);
                }
            }, ActionMode.TYPE_FLOATING);
        });
    }

    public void hideEditMenu() {
        runOnUiThread(() -> {
            if (editMenu != null)
                editMenu.finish();
        });
    }

    public int pollEdit() {
        Integer c = edits.poll();
        return c == null ? 0 : c;
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
