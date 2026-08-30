package com.btcrig.android;

import android.Manifest;
import android.app.Activity;
import android.app.AlertDialog;
import android.content.Intent;
import android.content.pm.PackageManager;
import android.graphics.Color;
import android.graphics.Typeface;
import android.graphics.drawable.Drawable;
import android.graphics.drawable.GradientDrawable;
import android.os.Build;
import android.os.Bundle;
import android.text.InputType;
import android.view.Gravity;
import android.view.View;
import android.widget.Button;
import android.widget.CheckBox;
import android.widget.EditText;
import android.widget.LinearLayout;
import android.widget.ScrollView;
import android.widget.TextView;
import android.widget.Toast;

import java.io.File;
import java.io.FileInputStream;
import java.util.Locale;

public final class MainActivity extends Activity {
    private static final int BG = Color.rgb(245, 247, 251);
    private static final int CARD = Color.WHITE;
    private static final int TEXT = Color.rgb(24, 32, 46);
    private static final int MUTED = Color.rgb(96, 106, 122);
    private static final int ACCENT = Color.rgb(76, 111, 255);
    private static final int SOFT = Color.rgb(236, 240, 255);
    private static final int BORDER = Color.rgb(226, 231, 241);
    private static final int PAGE_HOME = 0;
    private static final int PAGE_SETTINGS = 1;
    private static final int PAGE_INFO = 2;

    private LinearLayout content;
    private LinearLayout homePage;
    private LinearLayout settingsPage;
    private LinearLayout infoPage;
    private Button homeTab;
    private Button settingsTab;
    private Button infoTab;
    private TextView backendStatus;
    private TextView selfTestStatus;
    private TextView coreStatus;
    private TextView serviceStatus;
    private TextView hashrateStatus;
    private TextView workersStatus;
    private TextView totalStatus;
    private TextView openclStatus;
    private TextView poolStatus;
    private TextView stratumStatus;
    private TextView sharesStatus;
    private TextView errorStatus;
    private TextView configSummary;
    private TextView configStatus;
    private TextView infoSummary;
    private TextView infoFiles;
    private TextView benchmarkStatus;
    private Button startButton;
    private Button stopButton;
    private Button benchmarkButton;
    private boolean refreshScheduled;
    private String serviceState = "";

    @Override
    protected void onCreate(Bundle state) {
        super.onCreate(state);
        requestNotificationPermission();
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.LOLLIPOP) {
            getWindow().setStatusBarColor(BG);
            getWindow().setNavigationBarColor(BG);
        }
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.M) {
            getWindow().getDecorView().setSystemUiVisibility(View.SYSTEM_UI_FLAG_LIGHT_STATUS_BAR);
        }

        TextView title = new TextView(this);
        title.setText("BTCRig");
        title.setTextColor(TEXT);
        title.setTextSize(34);
        title.setTypeface(Typeface.DEFAULT_BOLD);

        TextView subtitle = new TextView(this);
        subtitle.setText("Native CPU / OpenCL miner");
        subtitle.setTextColor(MUTED);
        subtitle.setTextSize(15);

        backendStatus = line();
        selfTestStatus = line();
        coreStatus = line();
        serviceStatus = line();
        hashrateStatus = bigMetric();
        workersStatus = line();
        totalStatus = line();
        openclStatus = line();
        poolStatus = line();
        stratumStatus = line();
        sharesStatus = line();
        errorStatus = line();
        configSummary = line();
        configStatus = line();
        infoSummary = line();
        infoFiles = line();
        benchmarkStatus = line();

        startButton = button("Start service");
        startButton.setOnClickListener(view -> startBtcrigService());

        stopButton = button("Stop service");
        stopButton.setOnClickListener(view -> stopBtcrigService());

        Button editConfig = button("Configure");
        editConfig.setOnClickListener(view -> showBasicConfigEditor());

        benchmarkButton = button("CPU benchmark");
        benchmarkButton.setOnClickListener(view -> runCpuBenchmark());

        Button logButton = button("View log");
        logButton.setOnClickListener(view -> showLogViewer());

        Button jsonButton = button("Advanced JSON");
        jsonButton.setOnClickListener(view -> showJsonConfigEditor());

        homeTab = button("Home");
        homeTab.setOnClickListener(view -> selectPage(PAGE_HOME));
        settingsTab = button("Settings");
        settingsTab.setOnClickListener(view -> selectPage(PAGE_SETTINGS));
        infoTab = button("Info");
        infoTab.setOnClickListener(view -> selectPage(PAGE_INFO));

        homePage = page();
        homePage.addView(buttonRow(startButton, stopButton));
        homePage.addView(benchmarkButton, wide());
        homePage.addView(card("Status", backendStatus, selfTestStatus, coreStatus, serviceStatus));
        homePage.addView(card("Hashrate", hashrateStatus, workersStatus, totalStatus, benchmarkStatus));
        homePage.addView(card("OpenCL", openclStatus));
        homePage.addView(card("Pool", poolStatus, stratumStatus, sharesStatus, errorStatus));

        settingsPage = page();
        settingsPage.addView(buttonRow(editConfig, jsonButton));
        settingsPage.addView(card("Config", configSummary, configStatus));

        infoPage = page();
        infoPage.addView(logButton, wide());
        infoPage.addView(card("Info", infoSummary, infoFiles));

        content = new LinearLayout(this);
        content.setOrientation(LinearLayout.VERTICAL);

        LinearLayout root = new LinearLayout(this);
        root.setOrientation(LinearLayout.VERTICAL);
        root.setPadding(dp(20), dp(28), dp(20), dp(28));
        root.addView(title);
        root.addView(subtitle);
        root.addView(spacer(16));
        root.addView(tabRow());
        root.addView(content);

        ScrollView scroll = new ScrollView(this);
        scroll.setBackgroundColor(BG);
        scroll.addView(root);
        setContentView(scroll);
        selectPage(PAGE_HOME);
        updateUi();
    }

    @Override
    protected void onResume() {
        super.onResume();
        updateUi();
        if (BtcrigNative.isRunning()) {
            scheduleRefresh();
        }
    }

    private void startBtcrigService() {
        try {
            BtcrigConfig.Basic basic = BtcrigConfig.readBasic(this);
            if (basic.poolUrl.trim().isEmpty() || basic.user.trim().isEmpty()) {
                Toast.makeText(this, "Configure pool and user first.", Toast.LENGTH_LONG).show();
                showBasicConfigEditor();
                return;
            }
            if (basic.cpuThreads <= 0 && !basic.openclEnabled) {
                Toast.makeText(this, "Enable CPU threads or OpenCL first.", Toast.LENGTH_LONG).show();
                showBasicConfigEditor();
                return;
            }
            BtcrigConfig.writeBasic(this, basic);
        } catch (Exception e) {
            Toast.makeText(this, "Config read failed: " + e.getMessage(), Toast.LENGTH_LONG).show();
            return;
        }

        Intent intent = new Intent(this, BtcrigService.class);
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.O) {
            startForegroundService(intent);
        } else {
            startService(intent);
        }
        serviceState = "running";
        updateUi();
        scheduleRefresh();
    }

    private void stopBtcrigService() {
        Intent intent = new Intent(this, BtcrigService.class);
        intent.setAction(BtcrigService.ACTION_STOP);
        try {
            startService(intent);
        } catch (Exception e) {
            new Thread(() -> {
                try {
                    BtcrigNative.stop();
                } catch (Throwable ignored) {
                }
                stopService(new Intent(this, BtcrigService.class));
            }, "BTCRig-stop").start();
            Toast.makeText(this, getString(R.string.stop_failed, e.getMessage()), Toast.LENGTH_LONG).show();
        }
        serviceState = "stopped";
        updateUi();
        scheduleRefresh();
    }

    private void requestNotificationPermission() {
        if (Build.VERSION.SDK_INT >= 33
                && checkSelfPermission(Manifest.permission.POST_NOTIFICATIONS) != PackageManager.PERMISSION_GRANTED) {
            requestPermissions(new String[] {Manifest.permission.POST_NOTIFICATIONS}, 1);
        }
    }

    private void showBasicConfigEditor() {
        if (BtcrigNative.isRunning()) {
            Toast.makeText(this, "Stop service before editing config.", Toast.LENGTH_SHORT).show();
            return;
        }

        BtcrigConfig.Basic basic;
        try {
            basic = BtcrigConfig.readBasic(this);
        } catch (Exception e) {
            Toast.makeText(this, "Config read failed: " + e.getMessage(), Toast.LENGTH_LONG).show();
            return;
        }

        EditText poolUrl = oneLine("stratum+tcp://host:port", basic.poolUrl);
        EditText user = oneLine("wallet.worker", basic.user);
        EditText pass = oneLine("x", basic.pass);
        EditText cpuThreads = oneLine("0 = disabled", String.valueOf(basic.cpuThreads));
        cpuThreads.setInputType(InputType.TYPE_CLASS_NUMBER);
        CheckBox opencl = new CheckBox(this);
        opencl.setText("Enable OpenCL when available");
        opencl.setChecked(basic.openclEnabled);

        LinearLayout form = new LinearLayout(this);
        form.setOrientation(LinearLayout.VERTICAL);
        form.setPadding(dp(24), 0, dp(24), 0);
        addLabeled(form, "Pool URL", poolUrl);
        addLabeled(form, "User / worker", user);
        addLabeled(form, "Password", pass);
        addLabeled(form, "CPU threads", cpuThreads);
        form.addView(opencl);

        AlertDialog dialog = new AlertDialog.Builder(this)
                .setTitle("Configure")
                .setView(form)
                .setNegativeButton("Cancel", null)
                .setNeutralButton("Advanced JSON", null)
                .setPositiveButton("Save", null)
                .create();
        dialog.setOnShowListener(view -> {
            dialog.getButton(AlertDialog.BUTTON_NEUTRAL).setOnClickListener(button -> {
                dialog.dismiss();
                showJsonConfigEditor();
            });
            dialog.getButton(AlertDialog.BUTTON_POSITIVE).setOnClickListener(button -> {
                try {
                    BtcrigConfig.Basic next = new BtcrigConfig.Basic();
                    next.poolUrl = poolUrl.getText().toString();
                    next.user = user.getText().toString();
                    next.pass = pass.getText().toString();
                    next.cpuThreads = parseThreads(cpuThreads.getText().toString());
                    next.openclEnabled = opencl.isChecked();
                    BtcrigConfig.writeBasic(this, next);
                    updateUi();
                    Toast.makeText(this, "Config saved.", Toast.LENGTH_SHORT).show();
                    dialog.dismiss();
                } catch (Exception e) {
                    Toast.makeText(this, "Save failed: " + e.getMessage(), Toast.LENGTH_LONG).show();
                }
            });
        });
        dialog.show();
    }

    private EditText oneLine(String hint, String text) {
        EditText input = new EditText(this);
        input.setSingleLine(true);
        input.setHint(hint);
        input.setText(text);
        input.setSelectAllOnFocus(false);
        return input;
    }

    private void addLabeled(LinearLayout form, String label, EditText input) {
        TextView text = new TextView(this);
        text.setText(label);
        text.setTextColor(MUTED);
        text.setTextSize(14);
        form.addView(text);
        form.addView(input);
    }

    private static int parseThreads(String text) {
        try {
            return Math.max(0, Integer.parseInt(text.trim()));
        } catch (Exception ignored) {
            return 0;
        }
    }

    private void showJsonConfigEditor() {
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
        scroll.setPadding(dp(12), 0, dp(12), 0);
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
                updateUi();
                Toast.makeText(this, "Config saved.", Toast.LENGTH_SHORT).show();
                dialog.dismiss();
            } catch (Exception e) {
                Toast.makeText(this, "Save failed: " + e.getMessage(), Toast.LENGTH_LONG).show();
            }
        }));
        dialog.show();
    }

    private void showLogViewer() {
        TextView log = new TextView(this);
        log.setText(readTail(new File(getFilesDir(), "btcrig.log"), 64 * 1024));
        log.setTextIsSelectable(true);
        log.setTextSize(12);
        log.setTypeface(Typeface.MONOSPACE);

        ScrollView scroll = new ScrollView(this);
        scroll.setPadding(dp(12), 0, dp(12), 0);
        scroll.addView(log);

        new AlertDialog.Builder(this)
                .setTitle("btcrig.log")
                .setView(scroll)
                .setPositiveButton("Close", null)
                .show();
    }

    private static String readTail(File file, int maxBytes) {
        if (!file.exists()) {
            return "(log not found)";
        }
        try (FileInputStream input = new FileInputStream(file)) {
            long skip = Math.max(0, file.length() - maxBytes);
            while (skip > 0) {
                long skipped = input.skip(skip);
                if (skipped <= 0) {
                    break;
                }
                skip -= skipped;
            }
            byte[] buffer = new byte[(int) Math.min(maxBytes, file.length())];
            int n = input.read(buffer);
            return n > 0 ? new String(buffer, 0, n) : "(empty log)";
        } catch (Exception e) {
            return "Log read failed: " + e.getMessage();
        }
    }

    private void runCpuBenchmark() {
        int threads = Math.max(1, Runtime.getRuntime().availableProcessors());
        benchmarkButton.setEnabled(false);
        benchmarkStatus.setText("Benchmark: running " + threads + " threads");

        new Thread(() -> {
            double hps = BtcrigNative.benchmarkCpu(3, threads);
            runOnUiThread(() -> {
                benchmarkButton.setEnabled(true);
                benchmarkStatus.setText("Benchmark: " + formatHashrate(hps) + " / " + threads + " threads");
            });
        }).start();
    }

    private void updateUi() {
        boolean running = BtcrigNative.isRunning();
        backendStatus.setText("Backend: " + BtcrigNative.backendName());
        selfTestStatus.setText("Self-test: " + (BtcrigNative.selfTest() ? "ok" : "failed"));
        coreStatus.setText("Core: " + (running ? "running" : "stopped"));
        serviceStatus.setText("Service: " + (serviceState.isEmpty() ? (running ? "running" : "stopped") : serviceState));
        startButton.setEnabled(!running);
        stopButton.setEnabled(running);

        if (running) {
            String pool = BtcrigNative.pool();
            hashrateStatus.setText(formatHashrate(BtcrigNative.hashrate()));
            workersStatus.setText("Workers: " + BtcrigNative.workerCount());
            totalStatus.setText("Total: " + BtcrigNative.totalHashes());
            poolStatus.setText("Pool: " + (pool.isEmpty() ? "(not configured)" : pool));
            stratumStatus.setText("Stratum: " + BtcrigNative.stratumStatus()
                    + " / connected: " + (BtcrigNative.stratumConnected() ? "yes" : "no")
                    + " / jobs: " + BtcrigNative.stratumJobs());
            sharesStatus.setText("Shares: " + BtcrigNative.stratumSubmits()
                    + " submit / " + BtcrigNative.stratumAccepts()
                    + " ok / " + BtcrigNative.stratumRejects() + " reject");
        } else {
            hashrateStatus.setText("-- H/s");
            workersStatus.setText("Workers: --");
            totalStatus.setText("Total: --");
            poolStatus.setText("Pool: " + configuredPool());
            stratumStatus.setText("Stratum: stopped");
            sharesStatus.setText("Shares: --");
        }

        String error = BtcrigNative.lastError();
        errorStatus.setText(error.isEmpty() ? "Last error: none" : "Last error: " + error);
        openclStatus.setText(openclStatusText());
        configSummary.setText(configSummary());
        try {
            String configPath = BtcrigConfig.ensure(this).getAbsolutePath();
            configStatus.setText("Path: " + configPath);
            infoFiles.setText("Config: " + configPath + "\nLog: " + new File(getFilesDir(), "btcrig.log").getAbsolutePath());
        } catch (Exception ignored) {
            configStatus.setText("Config unavailable");
            infoFiles.setText("Files unavailable");
        }
        infoSummary.setText("Backend: " + BtcrigNative.backendName()
                + "\nSelf-test: " + (BtcrigNative.selfTest() ? "ok" : "failed")
                + "\nOpenCL:\n" + openclStatus.getText());
    }

    private String configuredPool() {
        try {
            String pool = BtcrigConfig.readBasic(this).poolUrl;
            return pool.trim().isEmpty() ? "not configured" : pool;
        } catch (Exception ignored) {
            return "unavailable";
        }
    }

    private String configSummary() {
        try {
            BtcrigConfig.Basic basic = BtcrigConfig.readBasic(this);
            return "CPU: " + (basic.cpuThreads > 0 ? basic.cpuThreads + " threads" : "disabled")
                    + " / OpenCL: " + (basic.openclEnabled ? "enabled" : "disabled");
        } catch (Exception ignored) {
            return "Config summary unavailable";
        }
    }

    private String openclStatusText() {
        try {
            return BtcrigNative.openclStatus(BtcrigConfig.ensure(this).getAbsolutePath());
        } catch (Exception e) {
            return "Config: unavailable\nRuntime: not probed\nMode: CPU only\nReason: " + e.getMessage();
        }
    }

    private void scheduleRefresh() {
        if (refreshScheduled) {
            return;
        }
        refreshScheduled = true;
        coreStatus.postDelayed(() -> {
            refreshScheduled = false;
            updateUi();
            if (BtcrigNative.isRunning()) {
                scheduleRefresh();
            }
        }, 2000);
    }

    private void selectPage(int page) {
        content.removeAllViews();
        content.addView(page == PAGE_SETTINGS ? settingsPage : page == PAGE_INFO ? infoPage : homePage);
        styleTab(homeTab, page == PAGE_HOME);
        styleTab(settingsTab, page == PAGE_SETTINGS);
        styleTab(infoTab, page == PAGE_INFO);
    }

    private void styleTab(Button tab, boolean selected) {
        tab.setTextColor(selected ? Color.WHITE : MUTED);
        setBackground(tab, roundRect(selected ? ACCENT : SOFT, dp(18), selected ? ACCENT : SOFT, 1));
    }

    private LinearLayout page() {
        LinearLayout page = new LinearLayout(this);
        page.setOrientation(LinearLayout.VERTICAL);
        return page;
    }

    private LinearLayout card(String title, View... views) {
        TextView heading = new TextView(this);
        heading.setText(title);
        heading.setTextColor(TEXT);
        heading.setTextSize(17);
        heading.setTypeface(Typeface.DEFAULT_BOLD);

        LinearLayout card = new LinearLayout(this);
        card.setOrientation(LinearLayout.VERTICAL);
        card.setPadding(dp(18), dp(16), dp(18), dp(16));
        setBackground(card, roundRect(CARD, dp(20), BORDER, 1));
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.LOLLIPOP) {
            card.setElevation(dp(2));
        }
        card.addView(heading);
        card.addView(spacer(8));
        for (View view : views) {
            card.addView(view);
        }

        LinearLayout.LayoutParams params = wide();
        params.setMargins(0, dp(12), 0, 0);
        card.setLayoutParams(params);
        return card;
    }

    private LinearLayout buttonRow(Button left, Button right) {
        LinearLayout row = new LinearLayout(this);
        row.setOrientation(LinearLayout.HORIZONTAL);
        row.setGravity(Gravity.CENTER);
        row.addView(left, weighted());
        row.addView(right, weighted());
        return row;
    }

    private LinearLayout tabRow() {
        LinearLayout row = new LinearLayout(this);
        row.setOrientation(LinearLayout.HORIZONTAL);
        row.setGravity(Gravity.CENTER);
        row.setPadding(dp(4), dp(4), dp(4), dp(4));
        setBackground(row, roundRect(SOFT, dp(22), BORDER, 1));
        row.addView(homeTab, weighted());
        row.addView(settingsTab, weighted());
        row.addView(infoTab, weighted());
        return row;
    }

    private Button button(String text) {
        Button button = new Button(this);
        button.setText(text);
        button.setTextColor(TEXT);
        button.setTextSize(14);
        button.setPadding(dp(12), dp(8), dp(12), dp(8));
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.LOLLIPOP) {
            button.setAllCaps(false);
            button.setElevation(0);
        }
        setBackground(button, roundRect(CARD, dp(16), BORDER, 1));
        return button;
    }

    private TextView line() {
        TextView view = new TextView(this);
        view.setTextColor(MUTED);
        view.setTextSize(14);
        view.setPadding(0, dp(2), 0, dp(2));
        return view;
    }

    private TextView bigMetric() {
        TextView view = line();
        view.setTextColor(TEXT);
        view.setTextSize(30);
        view.setTypeface(Typeface.DEFAULT_BOLD);
        return view;
    }

    private View spacer(int dp) {
        View view = new View(this);
        view.setLayoutParams(new LinearLayout.LayoutParams(1, dp(dp)));
        return view;
    }

    private LinearLayout.LayoutParams wide() {
        return new LinearLayout.LayoutParams(
                LinearLayout.LayoutParams.MATCH_PARENT,
                LinearLayout.LayoutParams.WRAP_CONTENT);
    }

    private LinearLayout.LayoutParams weighted() {
        LinearLayout.LayoutParams params = new LinearLayout.LayoutParams(0, LinearLayout.LayoutParams.WRAP_CONTENT, 1);
        params.setMargins(dp(2), 0, dp(2), 0);
        return params;
    }

    private GradientDrawable roundRect(int color, int radius) {
        return roundRect(color, radius, 0, 0);
    }

    private GradientDrawable roundRect(int color, int radius, int strokeColor, int strokeWidth) {
        GradientDrawable drawable = new GradientDrawable();
        drawable.setColor(color);
        drawable.setCornerRadius(radius);
        if (strokeWidth > 0) {
            drawable.setStroke(dp(strokeWidth), strokeColor);
        }
        return drawable;
    }

    @SuppressWarnings("deprecation")
    private void setBackground(View view, Drawable drawable) {
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.JELLY_BEAN) {
            view.setBackground(drawable);
        } else {
            view.setBackgroundDrawable(drawable);
        }
    }

    private int dp(int value) {
        return (int) (value * getResources().getDisplayMetrics().density + 0.5f);
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
