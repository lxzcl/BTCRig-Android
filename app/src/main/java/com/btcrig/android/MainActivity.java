package com.btcrig.android;

import android.Manifest;
import android.app.Activity;
import android.content.Intent;
import android.content.pm.PackageManager;
import android.os.Build;
import android.os.Bundle;
import android.view.Gravity;
import android.widget.Button;
import android.widget.LinearLayout;
import android.widget.TextView;

import java.util.Locale;

public final class MainActivity extends Activity {
    private TextView status;
    private Button benchmark;

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
        status.setText(baseStatus() + "\nService: running");
    }

    private void stopBtcrigService() {
        Intent intent = new Intent(this, BtcrigService.class);
        intent.setAction(BtcrigService.ACTION_STOP);
        startService(intent);
        status.setText(baseStatus() + "\nService: stopped");
    }

    private void requestNotificationPermission() {
        if (Build.VERSION.SDK_INT >= 33
                && checkSelfPermission(Manifest.permission.POST_NOTIFICATIONS) != PackageManager.PERMISSION_GRANTED) {
            requestPermissions(new String[] {Manifest.permission.POST_NOTIFICATIONS}, 1);
        }
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
                        + "\nThreads: " + threads);
            });
        }).start();
    }

    private String baseStatus() {
        return "BTCRig Android shell"
                + "\nBackend: " + BtcrigNative.backendName()
                + "\nSelf-test: " + (BtcrigNative.selfTest() ? "ok" : "failed");
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
