package com.btcrig.android;

import android.content.Context;
import android.os.Build;

import java.io.File;
import java.io.FileInputStream;
import java.io.FileOutputStream;
import java.io.IOException;
import java.nio.charset.StandardCharsets;
import java.util.ArrayList;
import java.util.List;
import java.util.Locale;
import java.util.regex.Matcher;
import java.util.regex.Pattern;

final class XmrigRunner {
    private static final Pattern SPEED = Pattern.compile(
            "speed\\s+10s/60s/15m\\s+(\\d+(?:\\.\\d+)?|n/a)\\s+(\\d+(?:\\.\\d+)?|n/a)\\s+(\\d+(?:\\.\\d+)?|n/a)\\s+([kMGT]?H/s)",
            Pattern.CASE_INSENSITIVE);
    private static Process process;
    private static File logFile;
    private static String pool = "";
    private static volatile String lastError = "";
    private static int threads;

    private XmrigRunner() {
    }

    static synchronized boolean start(Context context, BtcrigConfig.Basic basic) {
        if (isRunning()) {
            return true;
        }
        if (!isAvailable(context)) {
            lastError = "XMRig requires Android 7.0+ on arm64-v8a";
            return false;
        }

        File binary = new File(context.getApplicationInfo().nativeLibraryDir, "libxmrig.so");
        logFile = new File(context.getFilesDir(), "xmrig.log");
        pool = basic.xmrigPoolUrl.trim();
        threads = Math.max(1, basic.xmrigThreads);
        lastError = "";

        List<String> command = new ArrayList<>();
        command.add(binary.getAbsolutePath());
        command.add("--algo=rx/0");
        command.add("--url=" + pool);
        command.add("--user=" + basic.xmrigUser.trim());
        command.add("--pass=" + (basic.xmrigPass.isEmpty() ? "x" : basic.xmrigPass));
        command.add("--threads=" + threads);
        command.add("--donate-level=" + basic.donationPercent);
        command.add("--print-time=5");
        command.add("--no-color");
        command.add("--no-huge-pages");
        command.add("--log-file=" + logFile.getAbsolutePath());

        try {
            new FileOutputStream(logFile, false).close();
            process = new ProcessBuilder(command)
                    .directory(context.getFilesDir())
                    .redirectErrorStream(true)
                    .redirectOutput(new File("/dev/null"))
                    .start();
            Process started = process;
            new Thread(() -> watch(started), "XMRig-watch").start();
            return true;
        } catch (IOException e) {
            process = null;
            lastError = "XMRig start failed: " + e.getMessage();
            return false;
        }
    }

    static synchronized void stop() {
        Process current = process;
        process = null;
        if (current == null) {
            return;
        }
        current.destroy();
        try {
            current.waitFor();
        } catch (InterruptedException e) {
            Thread.currentThread().interrupt();
        }
    }

    static synchronized boolean isRunning() {
        if (process == null) {
            return false;
        }
        try {
            process.exitValue();
            process = null;
            return false;
        } catch (IllegalThreadStateException ignored) {
            return true;
        }
    }

    static boolean isAvailable(Context context) {
        if (Build.VERSION.SDK_INT < 24 || !"arm64-v8a".equals(Build.SUPPORTED_ABIS[0])) {
            return false;
        }
        File binary = new File(context.getApplicationInfo().nativeLibraryDir, "libxmrig.so");
        return binary.isFile() && binary.canExecute();
    }

    static double hashrate() {
        String text = readLog();
        Matcher matcher = SPEED.matcher(text);
        double value = 0.0;
        String unit = "H/s";
        while (matcher.find()) {
            String rate = !"n/a".equalsIgnoreCase(matcher.group(2)) ? matcher.group(2) : matcher.group(1);
            if (!"n/a".equalsIgnoreCase(rate)) {
                value = Double.parseDouble(rate);
                unit = matcher.group(4);
            }
        }
        return value * unitMultiplier(unit);
    }

    static int workerCount() {
        return threads;
    }

    static String pool() {
        return pool;
    }

    static String lastError() {
        return lastError;
    }

    static File logFile(Context context) {
        return logFile != null ? logFile : new File(context.getFilesDir(), "xmrig.log");
    }

    private static void watch(Process watched) {
        try {
            int code = watched.waitFor();
            synchronized (XmrigRunner.class) {
                if (process == watched) {
                    process = null;
                    if (code != 0) {
                        lastError = "XMRig exited with code " + code;
                    }
                }
            }
        } catch (InterruptedException e) {
            Thread.currentThread().interrupt();
        }
    }

    private static String readLog() {
        File file = logFile;
        if (file == null || !file.isFile()) {
            return "";
        }
        int length = (int) Math.min(file.length(), 64 * 1024);
        byte[] bytes = new byte[length];
        try (FileInputStream input = new FileInputStream(file)) {
            long skip = Math.max(0, file.length() - length);
            while (skip > 0) {
                long n = input.skip(skip);
                if (n <= 0) break;
                skip -= n;
            }
            int read = input.read(bytes);
            return read > 0 ? new String(bytes, 0, read, StandardCharsets.UTF_8) : "";
        } catch (IOException ignored) {
            return "";
        }
    }

    private static double unitMultiplier(String unit) {
        String normalized = unit.toUpperCase(Locale.US);
        if (normalized.startsWith("K")) return 1_000.0;
        if (normalized.startsWith("M")) return 1_000_000.0;
        if (normalized.startsWith("G")) return 1_000_000_000.0;
        if (normalized.startsWith("T")) return 1_000_000_000_000.0;
        return 1.0;
    }
}
