package com.btcrig.android;

import android.Manifest;
import android.app.Activity;
import android.app.AlertDialog;
import android.content.Intent;
import android.content.pm.PackageManager;
import android.os.Build;
import android.os.Bundle;
import android.text.InputType;
import android.view.Gravity;
import android.widget.Button;
import android.widget.EditText;
import android.widget.LinearLayout;
import android.widget.ScrollView;
import android.widget.TextView;
import android.widget.Toast;

import java.io.File;
import java.util.Locale;

public final class MainActivity extends Activity {
    private TextView status;
    private Button benchmark;
    private boolean refreshScheduled;
    private String serviceState = "";

    @Override
    protected void onCreate(Bundle state) {
        super.onCreate(state);
        requestNotificationPermission();

        status = new TextView(this);
        status.setText(baseStatus());
        status.setTextSize(18);

        Button start = new Button(this);
        start.setText("Start service");
        start.setOnClickListener(view -> startBtcrigService());

        Button stop = new Button(this);
        stop.setText("Stop service");
        stop.setOnClickListener(view -> stopBtcrigService());

        Button editConfig = new Button(this);
        editConfig.setText("Edit config");
        editConfig.setOnClickListener(view -> showConfigEditor());

        benchmark = new Button(this);
        benchmark.setText("CPU benchmark");
        benchmark.setOnClickListener(view -> runCpuBenchmark());

        LinearLayout layout = new LinearLayout(this);
        layout.setOrientation(LinearLayout.VERTICAL);
        layout.setGravity(Gravity.CENTER);
        layout.setPadding(48, 48, 48, 48);
        layout.addView(status);
        layout.addView(start);
        layout.addView(stop);
        layout.addView(editConfig);
        layout.addView(benchmark);
        setContentView(layout);
    }

    private void startBtcrigService() {
        Intent intent = new Intent(this, BtcrigService.class);
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.O) {
            startForegroundService(intent);
        } else {
            startService(intent);
        }
        serviceState = "running";
        status.setText(baseStatus() + serviceLine());
        scheduleRefresh();
    }

    private void stopBtcrigService() {
        Intent intent = new Intent(this, BtcrigService.class);
        intent.setAction(BtcrigService.ACTION_STOP);
        startService(intent);
        serviceState = "stopped";
        status.setText(baseStatus() + serviceLine());
        scheduleRefresh();
    }

    private void requestNotificationPermission() {
        if (Build.VERSION.SDK_INT >= 33
                && checkSelfPermission(Manifest.permission.POST_NOTIFICATIONS) != PackageManager.PERMISSION_GRANTED) {
            requestPermissions(new String[] {Manifest.permission.POST_NOTIFICATIONS}, 1);
        }
    }

    private void showConfigEditor() {
        if (BtcrigNative.isRunning()) {
            Toast.makeText(this, "Stop service before editing config.", Toast.LENGTH_SHORT).show();
            return;
        }

        EditText editor = new EditText(this);
        editor.setGravity(Gravity.START | Gravity.TOP);
        editor.setInputType(InputType.TYPE_CLASS_TEXT
                | InputType.TYPE_TEXT_FLAG_MULTI_LINE
                | InputType.TYPE_TEXT_FLAG_NO_SUGGESTIONS);
        editor.setMinLines(14);
        editor.setTextSize(14);
        try {
            editor.setText(BtcrigConfig.read(this));
        } catch (Exception e) {
            Toast.makeText(this, "Config read failed: " + e.getMessage(), Toast.LENGTH_LONG).show();
            return;
        }

        ScrollView scroll = new ScrollView(this);
        scroll.setPadding(24, 0, 24, 0);
        scroll.addView(editor);

        AlertDialog dialog = new AlertDialog.Builder(this)
                .setTitle("Edit config.json")
                .setView(scroll)
                .setNegativeButton("Cancel", null)
                .setPositiveButton("Save", null)
                .create();
        dialog.setOnShowListener(view -> dialog.getButton(AlertDialog.BUTTON_POSITIVE).setOnClickListener(button -> {
            try {
                BtcrigConfig.write(this, editor.getText().toString());
                status.setText(baseStatus() + serviceLine());
                Toast.makeText(this, "Config saved.", Toast.LENGTH_SHORT).show();
                dialog.dismiss();
            } catch (Exception e) {
                Toast.makeText(this, "Save failed: " + e.getMessage(), Toast.LENGTH_LONG).show();
            }
        }));
        dialog.show();
    }

    private void runCpuBenchmark() {
        int threads = Math.max(1, Runtime.getRuntime().availableProcessors());
        benchmark.setEnabled(false);
        status.setText(baseStatus() + "\nBenchmark: running " + threads + " threads");

        new Thread(() -> {
            double hps = BtcrigNative.benchmarkCpu(2, threads);
            runOnUiThread(() -> {
                benchmark.setEnabled(true);
                status.setText(baseStatus()
                        + "\nBenchmark: " + formatHashrate(hps)
                        + "\nThreads: " + threads
                        + serviceLine());
            });
        }).start();
    }

    private void scheduleRefresh() {
        if (refreshScheduled) {
            return;
        }
        refreshScheduled = true;
        status.postDelayed(() -> {
            refreshScheduled = false;
            status.setText(baseStatus() + serviceLine());
            if (BtcrigNative.isRunning()) {
                scheduleRefresh();
            }
        }, 2000);
    }

    private String serviceLine() {
        return serviceState.isEmpty() ? "" : "\nService: " + serviceState;
    }

    private String baseStatus() {
        boolean running = BtcrigNative.isRunning();
        String text = "BTCRig Android shell"
                + "\nBackend: " + BtcrigNative.backendName()
                + "\nSelf-test: " + (BtcrigNative.selfTest() ? "ok" : "failed")
                + "\nCore: " + (running ? "running" : "stopped");
        if (running) {
            String pool = BtcrigNative.pool();
            if (pool.isEmpty()) {
                pool = "(not configured)";
            }
            text += "\nMiner: " + formatHashrate(BtcrigNative.hashrate())
                    + "\nWorkers: " + BtcrigNative.workerCount()
                    + "\nTotal: " + BtcrigNative.totalHashes()
                    + "\nPool: " + pool
                    + "\nStratum: " + BtcrigNative.stratumStatus()
                    + "\nConnected: " + (BtcrigNative.stratumConnected() ? "yes" : "no")
                    + "\nJobs: " + BtcrigNative.stratumJobs()
                    + "\nShares: " + BtcrigNative.stratumSubmits()
                    + " submit / " + BtcrigNative.stratumAccepts()
                    + " ok / " + BtcrigNative.stratumRejects() + " reject";
            String error = BtcrigNative.lastError();
            if (!error.isEmpty()) {
                text += "\nLast error: " + error;
            }
        }
        try {
            File config = BtcrigConfig.ensure(this);
            text += "\nConfig: " + config.getAbsolutePath();
        } catch (Exception ignored) {
            text += "\nConfig: unavailable";
        }
        return text;
    }

    private static String formatHashrate(double hps) {
        if (hps >= 1_000_000_000.0) {
            return String.format(Locale.US, "%.2f GH/s", hps / 1_000_000_000.0);
        }
        if (hps >= 1_000_000.0) {
            return String.format(Locale.US, "%.2f MH/s", hps / 1_000_000.0);
        }
        if (hps >= 1_000.0) {
            return String.format(Locale.US, "%.2f KH/s", hps / 1_000.0);
        }
        return String.format(Locale.US, "%.0f H/s", hps);
    }
}
