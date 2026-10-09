package com.sukisu.ultra.selinuxprobe;

import android.app.Activity;
import android.os.Bundle;
import android.util.Log;
import android.widget.LinearLayout;
import android.widget.ScrollView;
import android.widget.TextView;
import android.widget.Button;
import android.content.ClipData;
import android.content.ClipboardManager;
import java.io.File;
import java.nio.charset.StandardCharsets;
import java.nio.file.Files;

// Runs as this application's original zygote UID/domain. No root request or permissions.
public final class ProbeActivity extends Activity {
    private static final String TAG = "SELinuxAppProbe";
    static { System.loadLibrary("selinuxprobe"); }
    private static native int runProbe(String reportPath);
    private String report = "Probe running...";

    @Override public void onCreate(Bundle state) {
        super.onCreate(state);
        LinearLayout layout = new LinearLayout(this);
        layout.setOrientation(LinearLayout.VERTICAL);
        TextView text = new TextView(this);
        text.setTextIsSelectable(true);
        text.setPadding(24, 24, 24, 24);
        text.setText(report);
        Button copy = new Button(this);
        copy.setText("Copy report");
        copy.setOnClickListener(view -> {
            ClipboardManager clipboard = getSystemService(ClipboardManager.class);
            clipboard.setPrimaryClip(ClipData.newPlainText("SELinux app UID probe", report));
        });
        ScrollView scroll = new ScrollView(this);
        scroll.addView(text);
        layout.addView(copy);
        layout.addView(scroll, new LinearLayout.LayoutParams(-1, 0, 1));
        setContentView(layout);
        new Thread(() -> {
            try {
                // JNI report fd is process-wide; serialize complete runs across activity recreation.
                synchronized (ProbeActivity.class) {
                    File output = new File(getFilesDir(), "selinux-probe-report.txt");
                    int code = runProbe(output.getAbsolutePath());
                    report = "package=" + getPackageName() + " native_exit=" + code + "\n" +
                        new String(Files.readAllBytes(output.toPath()), StandardCharsets.UTF_8);
                }
            } catch (Exception error) {
                report = "probe_exception=" + error;
            }
            for (String line : report.split("\n")) Log.i(TAG, line);
            runOnUiThread(() -> text.setText(report));
        }, "selinux-query-probe").start();
    }
}
