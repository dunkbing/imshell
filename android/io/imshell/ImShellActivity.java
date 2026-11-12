package io.imshell;

import android.app.NativeActivity;
import android.app.Notification;
import android.app.NotificationChannel;
import android.app.NotificationManager;
import android.app.PendingIntent;
import android.content.pm.PackageManager;
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
import android.text.Editable;
import android.text.InputType;
import android.view.ViewGroup;
import android.view.inputmethod.BaseInputConnection;
import android.view.inputmethod.EditorInfo;
import android.view.inputmethod.InputConnection;
import android.view.inputmethod.InputMethodManager;
import java.io.File;
import java.io.FileOutputStream;
import java.io.InputStream;
import java.io.OutputStream;
import java.util.concurrent.ConcurrentLinkedQueue;

// the activity of a imshell app: NativeActivity plus what the ndk cannot reach. the native
// side calls these over jni from its own thread
public class ImShellActivity extends NativeActivity {
    private static final int PICK = 0x6d79;
    private final ConcurrentLinkedQueue<Integer> chars = new ConcurrentLinkedQueue<>();
    private final ConcurrentLinkedQueue<String> picked = new ConcurrentLinkedQueue<>();
    private final ConcurrentLinkedQueue<Integer> edits = new ConcurrentLinkedQueue<>();
    private ActionMode editMenu;
    private String pickInto;
    private Editor editor;
    // keys in the character queue, after what was typed before them
    private static final int BACKSPACE = -1, DELETE = -2, ENTER = -3;
    // left, top, right, bottom (system bars, cutout), keyboard height; window pixels
    private volatile int[] insets = new int[5];

    // edge to edge on every version (api 35+ targets get it anyway): the surface covers the
    // whole window, and the shell keeps imgui clear of what the insets say is covered
    @Override
    protected void onCreate(Bundle state) {
        super.onCreate(state);
        takePressed(getIntent()); // a click on a notification started the app
        editor = new Editor(this);
        addContentView(editor, new ViewGroup.LayoutParams(1, 1));
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
            if (!java.util.Arrays.equals(next, insets)) {
                insets = next;
                wakeLoop();
            }
            return v.onApplyWindowInsets(in);
        });
    }

    // wakes the native loop (registered by the shell); before that the loop is not asleep yet
    private native void wake();

    private void wakeLoop() {
        try {
            wake();
        } catch (UnsatisfiedLinkError e) {
        }
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

    // the field kind the editor opens the keyboard for (onCreateInputConnection reads it):
    // imshell::TextKind, 0 code, 1 prose, 2 one-time code, 3 password
    private volatile int fieldKind = 0;

    public void showKeyboard(boolean show, int kind) {
        composing = "";
        fieldKind = kind;
        runOnUiThread(() -> {
            InputMethodManager imm = (InputMethodManager) getSystemService(Context.INPUT_METHOD_SERVICE);
            if (show) {
                editor.requestFocus();
                imm.restartInput(editor);
                imm.showSoftInput(editor, 0);
            } else {
                imm.hideSoftInputFromWindow(editor.getWindowToken(), 0);
            }
        });
    }

    // the ime's composing word, for the native side to underline under the caret; empty when
    // nothing composes or the composition is not at the end of what was typed
    private volatile String composing = "";

    public String composing() {
        return composing;
    }

    // the caret moved under the keyboard (a tap): it must not keep editing the old word
    public void resetInput() {
        composing = "";
        runOnUiThread(() -> {
            InputMethodManager imm = (InputMethodManager) getSystemService(Context.INPUT_METHOD_SERVICE);
            imm.restartInput(editor);
        });
    }

    private void type(String text) {
        text.codePoints().forEach(c -> chars.offer(c == '\n' ? ENTER : c));
    }

    // the keyboard's editor: an invisible view whose InputConnection turns the keyboard's
    // edits into characters and keys for imgui, which keeps the real text
    // shortcut: the ime sees only what was typed since the last reset (a tap restarts it)
    private final class Editor extends View {
        Editor(Context context) {
            super(context);
            setFocusable(true);
            setFocusableInTouchMode(true);
        }

        @Override
        public boolean onCheckIsTextEditor() {
            return true;
        }

        @Override
        public InputConnection onCreateInputConnection(EditorInfo info) {
            info.imeOptions = EditorInfo.IME_FLAG_NO_ENTER_ACTION | EditorInfo.IME_FLAG_NO_FULLSCREEN
                | EditorInfo.IME_FLAG_NO_EXTRACT_UI;
            switch (fieldKind) {
            case 1: // prose
                info.inputType = InputType.TYPE_CLASS_TEXT | InputType.TYPE_TEXT_FLAG_MULTI_LINE
                    | InputType.TYPE_TEXT_FLAG_AUTO_CORRECT | InputType.TYPE_TEXT_FLAG_CAP_SENTENCES;
                break;
            case 2: // one-time code
                info.inputType = InputType.TYPE_CLASS_NUMBER;
                info.imeOptions |= EditorInfo.IME_FLAG_NO_PERSONALIZED_LEARNING;
                break;
            case 3: // password: no suggestions, no learning, no autocorrect
                info.inputType = InputType.TYPE_CLASS_TEXT | InputType.TYPE_TEXT_VARIATION_PASSWORD;
                info.imeOptions |= EditorInfo.IME_FLAG_NO_PERSONALIZED_LEARNING;
                break;
            default: // code: as before kinds existed (suggestions, so input methods compose)
                info.inputType = InputType.TYPE_CLASS_TEXT | InputType.TYPE_TEXT_FLAG_MULTI_LINE
                    | InputType.TYPE_TEXT_FLAG_AUTO_CORRECT;
            }
            return new Connection(this);
        }
    }

    // the ime edits a real buffer (BaseInputConnection's own editable: what was typed since the
    // last reset), so it can read back, compose, autocorrect and replace. after each edit the
    // buffer is diffed against what imgui already got, and only the difference is sent:
    // backspaces to where they part, then the new text
    private final class Connection extends BaseInputConnection {
        private String sent = ""; // the buffer as imgui has it
        private int batch = 0;

        Connection(View view) {
            super(view, true);
        }

        private boolean sync(boolean ok) {
            if (batch > 0)
                return ok;
            Editable e = getEditable();
            String now = e == null ? "" : e.toString();
            int same = 0; // never split a surrogate pair
            while (same < sent.length() && same < now.length() && sent.charAt(same) == now.charAt(same))
                same++;
            if (same > 0 && Character.isHighSurrogate(sent.charAt(same - 1)))
                same--;
            int erase = sent.codePointCount(same, sent.length());
            for (int i = 0; i < erase; i++)
                chars.offer(BACKSPACE);
            type(now.substring(same));
            if (erase > 0 || same < now.length())
                wakeLoop();
            sent = now;
            int cs = getComposingSpanStart(e), ce = getComposingSpanEnd(e);
            composing = e != null && cs >= 0 && ce > cs && ce == now.length() ? now.substring(cs, ce) : "";
            return ok;
        }

        @Override
        public boolean beginBatchEdit() {
            batch++;
            return super.beginBatchEdit();
        }

        @Override
        public boolean endBatchEdit() {
            boolean ok = super.endBatchEdit();
            batch = Math.max(batch - 1, 0);
            return sync(ok);
        }

        @Override
        public boolean commitText(CharSequence text, int newCursorPosition) {
            return sync(super.commitText(text, newCursorPosition));
        }

        @Override
        public boolean setComposingText(CharSequence text, int newCursorPosition) {
            return sync(super.setComposingText(text, newCursorPosition));
        }

        @Override
        public boolean setComposingRegion(int start, int end) {
            return sync(super.setComposingRegion(start, end));
        }

        @Override
        public boolean finishComposingText() {
            return sync(super.finishComposingText());
        }

        @Override
        public boolean deleteSurroundingText(int before, int after) {
            return sync(super.deleteSurroundingText(before, after));
        }

        @Override
        public boolean deleteSurroundingTextInCodePoints(int before, int after) {
            return sync(super.deleteSurroundingTextInCodePoints(before, after));
        }

        @Override
        public boolean replaceText(int start, int end, CharSequence text, int newCursorPosition,
                                   android.view.inputmethod.TextAttribute attribute) {
            return sync(super.replaceText(start, end, text, newCursorPosition, attribute));
        }

        // keys from the keyboard (backspace on an empty word, enter): edit the buffer the same
        // way so it stays what imgui has
        @Override
        public boolean sendKeyEvent(KeyEvent event) {
            if (event.getAction() != KeyEvent.ACTION_DOWN)
                return true;
            Editable e = getEditable();
            switch (event.getKeyCode()) {
            case KeyEvent.KEYCODE_DEL:
                if (e != null && e.length() > 0) {
                    super.deleteSurroundingTextInCodePoints(1, 0);
                    return sync(true);
                }
                chars.offer(BACKSPACE); // before the buffer: imgui's text
                break;
            case KeyEvent.KEYCODE_FORWARD_DEL:
                chars.offer(DELETE);
                break;
            case KeyEvent.KEYCODE_ENTER:
                return sync(super.commitText("\n", 1));
            default:
                int c = event.getUnicodeChar(event.getMetaState());
                if (c >= 32 && c != 127)
                    return sync(super.commitText(new String(Character.toChars(c)), 1));
                return true;
            }
            wakeLoop();
            return true;
        }
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

    // imshell::Clipboard::Read and Write: items as [mime, bytes, mime, bytes, ..., "" (next item)]
    private static final java.nio.charset.Charset UTF8 = java.nio.charset.StandardCharsets.UTF_8;

    public byte[][] readClipboard() {
        ClipboardManager cm = (ClipboardManager) getSystemService(Context.CLIPBOARD_SERVICE);
        ClipData clip = cm.getPrimaryClip();
        java.util.ArrayList<byte[]> out = new java.util.ArrayList<>();
        for (int i = 0; clip != null && i < clip.getItemCount(); ++i) {
            ClipData.Item item = clip.getItemAt(i);
            if (item.getText() != null)
                addPart(out, "text/plain", item.getText().toString().getBytes(UTF8));
            if (item.getHtmlText() != null)
                addPart(out, "text/html", item.getHtmlText().getBytes(UTF8));
            Uri uri = item.getUri();
            if (uri != null) {
                addPart(out, "text/uri-list", uri.toString().getBytes(UTF8));
                // a copied picture's bytes, and always as png
                String type = "content".equals(uri.getScheme()) ? getContentResolver().getType(uri) : null;
                if (type != null && type.startsWith("image/")) {
                    byte[] bytes = readUri(uri);
                    if (bytes != null) {
                        addPart(out, type, bytes);
                        if (!type.equals("image/png")) {
                            android.graphics.Bitmap bitmap = android.graphics.BitmapFactory.decodeByteArray(bytes, 0, bytes.length);
                            java.io.ByteArrayOutputStream png = new java.io.ByteArrayOutputStream();
                            if (bitmap != null && bitmap.compress(android.graphics.Bitmap.CompressFormat.PNG, 100, png))
                                addPart(out, "image/png", png.toByteArray());
                        }
                    }
                }
            }
            out.add(new byte[0]);
        }
        return out.toArray(new byte[0][]);
    }

    private static void addPart(java.util.ArrayList<byte[]> out, String mime, byte[] bytes) {
        out.add(mime.getBytes(UTF8));
        out.add(bytes);
    }

    private byte[] readUri(Uri uri) {
        try (InputStream in = getContentResolver().openInputStream(uri)) {
            java.io.ByteArrayOutputStream bytes = new java.io.ByteArrayOutputStream();
            byte[] buf = new byte[1 << 16];
            for (int n; (n = in.read(buf)) > 0;)
                bytes.write(buf, 0, n);
            return bytes.toByteArray();
        } catch (Exception e) {
            return null;
        }
    }

    // text, html and urls; pictures and files need a content provider of the app's own to be
    // offered to other apps, so they are left out
    public void writeClipboard(byte[][] parts) {
        java.util.ArrayList<ClipData.Item> items = new java.util.ArrayList<>();
        java.util.LinkedHashSet<String> mimes = new java.util.LinkedHashSet<>();
        String text = null, html = null, uri = null;
        for (int i = 0; i <= parts.length; ) {
            String mime = i < parts.length ? new String(parts[i], UTF8) : "";
            if (mime.isEmpty()) { // the end of an item
                if (html != null)
                    items.add(new ClipData.Item(text != null ? text : html, html));
                else if (text != null)
                    items.add(new ClipData.Item(text));
                else if (uri != null && !uri.startsWith("file:"))
                    items.add(new ClipData.Item(Uri.parse(uri)));
                text = html = uri = null;
                i += 1;
                continue;
            }
            String value = new String(parts[i + 1], UTF8);
            if (mime.equals("text/plain")) {
                text = value;
                mimes.add(android.content.ClipDescription.MIMETYPE_TEXT_PLAIN);
            } else if (mime.equals("text/html")) {
                html = value;
                mimes.add(android.content.ClipDescription.MIMETYPE_TEXT_HTML);
            } else if (mime.equals("text/uri-list")) {
                uri = value.split("\\r?\\n", 2)[0];
                mimes.add(android.content.ClipDescription.MIMETYPE_TEXT_URILIST);
            }
            i += 2;
        }
        if (items.isEmpty())
            return;
        ClipData clip = new ClipData(new android.content.ClipDescription("imshell", mimes.toArray(new String[0])), items.get(0));
        for (int i = 1; i < items.size(); ++i)
            clip.addItem(items.get(i));
        ClipboardManager cm = (ClipboardManager) getSystemService(Context.CLIPBOARD_SERVICE);
        cm.setPrimaryClip(clip);
    }


    // the system's floating text toolbar at (x, y), window pixels. a pick lands in pollEdit as
    // the letter of its shortcut: 'X' cut, 'C' copy, 'V' paste, 'A' select all
    // imshell::Haptic in order: tap, selection, long press, success, warning, error
    public void haptic(int kind) {
        final int api = Build.VERSION.SDK_INT;
        final int[] constants = {
            HapticFeedbackConstants.VIRTUAL_KEY,
            HapticFeedbackConstants.CLOCK_TICK,
            HapticFeedbackConstants.LONG_PRESS,
            api >= 30 ? HapticFeedbackConstants.CONFIRM : HapticFeedbackConstants.VIRTUAL_KEY,
            HapticFeedbackConstants.LONG_PRESS,
            api >= 30 ? HapticFeedbackConstants.REJECT : HapticFeedbackConstants.LONG_PRESS,
        };
        if (kind < 0 || kind >= constants.length)
            return;
        runOnUiThread(() -> getWindow().getDecorView().performHapticFeedback(constants[kind]));
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
                    wakeLoop();
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
            wakeLoop();
            return;
        }
        new Thread(() -> {
            picked.offer(copy(uri));
            wakeLoop();
        }).start();
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

    // ---- notifications (imshell/notifications.hpp) ----
    // the host's manifest lists android.permission.POST_NOTIFICATIONS (api 33+ asks for it)

    private static final int NOTIFY = 0x6e74;
    private static final String POST = "android.permission.POST_NOTIFICATIONS";
    private static final String PRESSED = "io.imshell.notification";
    private final ConcurrentLinkedQueue<String> pressed = new ConcurrentLinkedQueue<>();
    private volatile int permissionAnswer = -1; // after a prompt; -1: none yet

    // Permission's order: 0 not asked, 1 granted, 2 denied
    public int notificationPermission() {
        if (Build.VERSION.SDK_INT >= 33 && checkSelfPermission(POST) != PackageManager.PERMISSION_GRANTED)
            return getPreferences(MODE_PRIVATE).getBoolean(NOTIFY + "", false) ? 2 : 0;
        return getSystemService(NotificationManager.class).areNotificationsEnabled() ? 1 : 2;
    }

    // the system's prompt; the answer lands in pollPermission
    public void requestNotificationPermission() {
        if (Build.VERSION.SDK_INT < 33 || checkSelfPermission(POST) == PackageManager.PERMISSION_GRANTED) {
            permissionAnswer = notificationPermission();
            wakeLoop();
            return;
        }
        getPreferences(MODE_PRIVATE).edit().putBoolean(NOTIFY + "", true).apply();
        runOnUiThread(() -> requestPermissions(new String[] {POST}, NOTIFY));
    }

    @Override
    public void onRequestPermissionsResult(int request, String[] permissions, int[] results) {
        super.onRequestPermissionsResult(request, permissions, results);
        if (request == NOTIFY) {
            permissionAnswer = notificationPermission();
            wakeLoop();
        }
    }

    public int pollPermission() {
        int answer = permissionAnswer;
        permissionAnswer = -1;
        return answer;
    }

    // tagged with the id: another with the same id replaces it. a click reopens this activity
    // with the id in the intent (onNewIntent, or onCreate when the app was gone)
    public boolean showNotification(String id, String title, String body, boolean silent) {
        if (notificationPermission() != 1)
            return false;
        NotificationManager nm = getSystemService(NotificationManager.class);
        String channel = silent ? "imshell.quiet" : "imshell";
        if (nm.getNotificationChannel(channel) == null)
            nm.createNotificationChannel(new NotificationChannel(channel, silent ? "Quiet notifications" : "Notifications",
                silent ? NotificationManager.IMPORTANCE_LOW : NotificationManager.IMPORTANCE_DEFAULT));
        Intent open = new Intent(this, getClass()).setAction(PRESSED).putExtra(PRESSED, id)
            .addFlags(Intent.FLAG_ACTIVITY_SINGLE_TOP);
        PendingIntent click = PendingIntent.getActivity(this, id.hashCode(), open,
            PendingIntent.FLAG_IMMUTABLE | PendingIntent.FLAG_UPDATE_CURRENT);
        int icon = getApplicationInfo().icon != 0 ? getApplicationInfo().icon : android.R.drawable.ic_dialog_info;
        nm.notify(id, 0, new Notification.Builder(this, channel).setSmallIcon(icon).setContentTitle(title)
            .setContentText(body).setContentIntent(click).setAutoCancel(true).build());
        return true;
    }

    public void cancelNotification(String id) {
        getSystemService(NotificationManager.class).cancel(id, 0);
    }

    public String pollPressed() {
        return pressed.poll();
    }

    @Override
    protected void onNewIntent(Intent intent) {
        super.onNewIntent(intent);
        takePressed(intent);
    }

    private void takePressed(Intent intent) {
        String id = intent == null ? null : intent.getStringExtra(PRESSED);
        if (id == null)
            return;
        intent.removeExtra(PRESSED); // not again when the activity is recreated
        pressed.offer(id);
        wakeLoop();
    }
}
