package com.btcrig.android;

import android.content.Context;
import android.os.Build;

import org.json.JSONArray;
import org.json.JSONObject;

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
    interface Progress { void accept(String algorithm); }
    private static final String BUILD_ID = "moneroocean-6.26.0-mo5";
    private static final String[] BENCHMARK_ALGOS = {
            "cn/r", "cn-lite/1", "cn-pico", "cn/ccx", "cn/gpu", "argon2/chukwav2",
            "ghostrider", "flex", "cn-heavy/xhv", "rx/0", "rx/graft", "rx/arq", "panthera"
    };
    private static final Pattern SPEED = Pattern.compile(
            "speed\\s+10s/60s/15m\\s+(\\d+(?:\\.\\d+)?|n/a)\\s+(\\d+(?:\\.\\d+)?|n/a)\\s+(\\d+(?:\\.\\d+)?|n/a)\\s+([kMGT]?H/s)",
            Pattern.CASE_INSENSITIVE);
    private static final Pattern CURRENT_ALGO = Pattern.compile("new job .*? algo ([^\\s]+)", Pattern.CASE_INSENSITIVE);
    private static final Pattern BENCH_RATE = Pattern.compile("Algo ([^\\s]+) hashrate: ([0-9.]+)", Pattern.CASE_INSENSITIVE);
    private static final Pattern BENCH_CURRENT = Pattern.compile("Algo ([^\\s]+) Starting test", Pattern.CASE_INSENSITIVE);
    private static final Pattern BENCH_SKIPPED = Pattern.compile("Algo ([^\\s]+) is skipped", Pattern.CASE_INSENSITIVE);
    private static Process process;
    private static File logFile;
    private static String pool = "";
    private static volatile String lastError = "";
    private static int threads;

    private XmrigRunner() {}

    static String[] supportedAlgorithms() {
        return BENCHMARK_ALGOS.clone();
    }

    static boolean isSupportedAlgorithm(String name) {
        if (name == null) return false;
        for (String algorithm : BENCHMARK_ALGOS) {
            if (algorithm.equals(name)) return true;
        }
        return false;
    }

    static synchronized boolean start(Context context, BtcrigConfig.Basic basic) {
        if (isRunning()) return true;
        if (!isAvailable(context)) {
            lastError = "XMRig requires Android 7.0+ on arm64-v8a";
            return false;
        }

        logFile = new File(context.getFilesDir(), "xmrig.log");
        pool = basic.xmrigPoolUrl.trim();
        threads = Math.max(1, basic.xmrigThreads);
        lastError = "";

        try {
            boolean calibrated = !needsBenchmark(context, basic);
            String fixed = basic.xmrigAlgo == null ? "" : basic.xmrigAlgo.trim();
            boolean fixedAlgorithm = isSupportedAlgorithm(fixed);
            String algo = fixedAlgorithm ? fixed : (calibrated ? "" : "rx/0");
            List<String> command = command(binary(context), prepareConfig(configFile(context), basic, calibrated, fixedAlgorithm, algo), basic, logFile);
            new FileOutputStream(logFile, false).close();
            process = new ProcessBuilder(command)
                    .directory(context.getFilesDir())
                    .redirectErrorStream(true)
                    .redirectOutput(new File("/dev/null"))
                    .start();
            Process started = process;
            new Thread(() -> watch(started), "XMRig-watch").start();
            return true;
        } catch (Exception e) {
            process = null;
            lastError = "XMRig start failed: " + e.getMessage();
            return false;
        }
    }

    static boolean benchmark(Context context, BtcrigConfig.Basic basic, Progress progress) {
        if (!isAvailable(context) || isRunning()) return false;
        File benchmarkLog = new File(context.getFilesDir(), "xmrig-benchmark.log");
        Process benchmark = null;
        try {
            File config = prepareConfig(benchmarkConfigFile(context), basic, false, false, "");
            List<String> command = command(binary(context), config, basic, benchmarkLog);
            command.add("--rebench-algo");
            command.add("--bench-algo-time=3");
            new FileOutputStream(benchmarkLog, false).close();
            benchmark = new ProcessBuilder(command)
                    .directory(context.getFilesDir())
                    .redirectErrorStream(true)
                    .redirectOutput(new File("/dev/null"))
                    .start();
            String currentName = "";
            long deadline = System.currentTimeMillis() + 10 * 60_000L;
            while (System.currentTimeMillis() < deadline) {
                String text = readFile(benchmarkLog, 512 * 1024);
                Matcher current = BENCH_CURRENT.matcher(text);
                while (current.find()) currentName = current.group(1);
                progress.accept(currentName);
                if (text.contains("ALGO PERFORMANCE CALIBRATION COMPLETE")) {
                    JSONObject perf = parseBenchmark(text);
                    if (perf.length() == 0 || perf.optDouble("rx/0", -1.0) <= 0.0) return false;
                    String fixed = basic.xmrigAlgo == null ? "" : basic.xmrigAlgo.trim();
                    boolean fixedAlgorithm = isSupportedAlgorithm(fixed);
                    savePerformance(prepareConfig(configFile(context), basic, true, fixedAlgorithm, fixedAlgorithm ? fixed : ""), perf);
                    markBenchmarkComplete(context, basic);
                    progress.accept("");
                    return true;
                }
                try {
                    benchmark.exitValue();
                    return false;
                } catch (IllegalThreadStateException ignored) {}
                Thread.sleep(500);
            }
            return false;
        } catch (Exception e) {
            lastError = "XMRig benchmark failed: " + e.getMessage();
            return false;
        } finally {
            if (benchmark != null) benchmark.destroy();
        }
    }

    static String benchmarkSummary(Context context) {
        JSONObject perf = performance(context);
        if (perf.length() == 0) return "";
        StringBuilder result = new StringBuilder();
        for (String name : BENCHMARK_ALGOS) {
            double value = perf.optDouble(name, 0.0);
            if (value > 0.0) {
                if (result.length() > 0) result.append('\n');
                result.append(name).append(": ").append(formatRate(value));
            }
        }
        return result.toString();
    }

    static boolean needsBenchmark(Context context, BtcrigConfig.Basic basic) {
        String expected = BUILD_ID + ":" + Math.max(1, basic.xmrigThreads);
        String actual = context.getSharedPreferences("xmrig", Context.MODE_PRIVATE).getString("benchmark", "");
        return !expected.equals(actual) || performance(context).optDouble("rx/0", -1.0) <= 0.0;
    }

    static String currentAlgorithm() {
        Matcher matcher = CURRENT_ALGO.matcher(readLog());
        String value = "";
        while (matcher.find()) value = matcher.group(1);
        return value;
    }

    static double currentMultiplier(Context context) {
        JSONObject perf = performance(context);
        String algorithm = currentAlgorithm();
        double current = perf.optDouble(algorithm, -1.0);
        double reference = perf.optDouble("rx/0", -1.0);
        return current > 0.0 && reference > 0.0 ? reference / current : -1.0;
    }

    static synchronized void stop() {
        Process current = process;
        process = null;
        if (current == null) return;
        current.destroy();
        try {
            current.waitFor();
        } catch (InterruptedException e) {
            Thread.currentThread().interrupt();
        }
    }

    static synchronized boolean isRunning() {
        if (process == null) return false;
        try {
            process.exitValue();
            process = null;
            return false;
        } catch (IllegalThreadStateException ignored) {
            return true;
        }
    }

    static boolean isAvailable(Context context) {
        if (Build.VERSION.SDK_INT < 24 || !"arm64-v8a".equals(Build.SUPPORTED_ABIS[0])) return false;
        File binary = binary(context);
        return binary.isFile() && binary.canExecute();
    }

    static double hashrate() {
        Matcher matcher = SPEED.matcher(readLog());
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

    static int workerCount() { return threads; }
    static String pool() { return pool; }
    static String lastError() { return lastError; }
    static File logFile(Context context) { return logFile != null ? logFile : new File(context.getFilesDir(), "xmrig.log"); }

    private static List<String> command(File binary, File config, BtcrigConfig.Basic basic, File outputLog) {
        List<String> command = new ArrayList<>();
        command.add(binary.getAbsolutePath());
        command.add("--config=" + config.getAbsolutePath());
        command.add("--threads=" + Math.max(1, basic.xmrigThreads));
        command.add("--donate-level=" + basic.donationPercent);
        command.add("--print-time=5");
        command.add("--no-color");
        command.add("--no-huge-pages");
        command.add("--log-file=" + outputLog.getAbsolutePath());
        return command;
    }

    private static File prepareConfig(File file, BtcrigConfig.Basic basic, boolean calibrated, boolean fixedAlgorithm, String algo) throws Exception {
        JSONObject root = readJson(file);
        root.put("autosave", false);
        root.put("btcrig-calibrated", calibrated);
        root.put("rebench-algo", false);
        root.put("bench-algo-time", 3);
        root.put("cpu", true);
        root.put("donate-level", basic.donationPercent);
        root.put("donate-over-proxy", 0);
        root.put("dns", new JSONObject().put("ip_version", 4));
        JSONObject pool = new JSONObject()
                .put("url", basic.xmrigPoolUrl.trim())
                .put("user", basic.xmrigUser.trim())
                .put("pass", basic.xmrigPass.isEmpty() ? "x" : basic.xmrigPass)
                .put("keepalive", true)
                .put("tls-compat", basic.certCompat);
        if (!algo.isEmpty()) {
            pool.put("algo", algo);
        }
        root.put("pools", new JSONArray().put(pool));
        if (fixedAlgorithm) {
            root.remove("algo-perf");
        } else if (!calibrated) {
            JSONObject placeholder = new JSONObject();
            for (String name : BENCHMARK_ALGOS) placeholder.put(name, 1.0);
            root.put("algo-perf", placeholder);
        }
        writeFile(file, root.toString(2));
        return file;
    }

    private static JSONObject parseBenchmark(String text) throws Exception {
        JSONObject perf = new JSONObject();
        Matcher rates = BENCH_RATE.matcher(text);
        while (rates.find()) perf.put(rates.group(1), Double.parseDouble(rates.group(2)));
        Matcher skipped = BENCH_SKIPPED.matcher(text);
        while (skipped.find()) perf.put(skipped.group(1), -1.0);
        return perf;
    }

    private static void savePerformance(File config, JSONObject perf) throws Exception {
        JSONObject root = readJson(config);
        root.put("algo-perf", perf);
        root.put("btcrig-calibrated", true);
        root.put("rebench-algo", false);
        writeFile(config, root.toString(2));
    }

    private static JSONObject performance(Context context) {
        JSONObject root = readJson(configFile(context));
        if (!root.optBoolean("btcrig-calibrated", false)) return new JSONObject();
        JSONObject perf = root.optJSONObject("algo-perf");
        return perf == null ? new JSONObject() : perf;
    }

    private static void markBenchmarkComplete(Context context, BtcrigConfig.Basic basic) {
        context.getSharedPreferences("xmrig", Context.MODE_PRIVATE).edit()
                .putString("benchmark", BUILD_ID + ":" + Math.max(1, basic.xmrigThreads)).apply();
    }

    private static File binary(Context context) {
        return new File(context.getApplicationInfo().nativeLibraryDir, "libxmrig.so");
    }

    private static File configFile(Context context) {
        return new File(context.getFilesDir(), "xmrig-config.json");
    }

    private static File benchmarkConfigFile(Context context) {
        return new File(context.getFilesDir(), "xmrig-benchmark-config.json");
    }

    private static JSONObject readJson(File file) {
        try {
            String text = readFile(file, 512 * 1024);
            return text.isEmpty() ? new JSONObject() : new JSONObject(text);
        } catch (Exception ignored) {
            return new JSONObject();
        }
    }

    private static void writeFile(File file, String text) throws IOException {
        try (FileOutputStream output = new FileOutputStream(file, false)) {
            output.write(text.getBytes(StandardCharsets.UTF_8));
        }
    }

    private static String readFile(File file, int maximum) {
        if (file == null || !file.isFile()) return "";
        int length = (int) Math.min(file.length(), maximum);
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

    private static String readLog() { return readFile(logFile, 64 * 1024); }

    private static String formatRate(double value) {
        if (value >= 1_000_000.0) return String.format(Locale.US, "%.2f MH/s", value / 1_000_000.0);
        if (value >= 1_000.0) return String.format(Locale.US, "%.2f KH/s", value / 1_000.0);
        return String.format(Locale.US, "%.0f H/s", value);
    }

    private static void watch(Process watched) {
        try {
            int code = watched.waitFor();
            synchronized (XmrigRunner.class) {
                if (process == watched) {
                    process = null;
                    if (code != 0) lastError = "XMRig exited with code " + code;
                }
            }
        } catch (InterruptedException e) {
            Thread.currentThread().interrupt();
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
