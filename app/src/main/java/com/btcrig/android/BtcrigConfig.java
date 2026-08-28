package com.btcrig.android;

import android.content.Context;

import java.io.File;
import java.io.ByteArrayOutputStream;
import java.io.FileInputStream;
import java.io.FileOutputStream;
import java.io.IOException;
import java.io.InputStream;
import java.nio.charset.StandardCharsets;

import org.json.JSONArray;
import org.json.JSONException;
import org.json.JSONObject;

final class BtcrigConfig {
    private static final String CONFIG_NAME = "config.json";
    private static final String DEFAULT_POOL_URL = "stratum+tcp://public-pool.io:3333";
    private static final String DEFAULT_USER = "bc1qqz0wutk9kk5mmaf7fu4dm5w4fq4fhaah9hpzr3";

    static final class Basic {
        String poolUrl = DEFAULT_POOL_URL;
        String user = DEFAULT_USER;
        String pass = "x";
        int cpuThreads = defaultCpuThreads();
        boolean openclEnabled = true;
        boolean certCompat = true;
        int donationPercent = 1;
    }

    private BtcrigConfig() {
    }

    static File ensure(Context context) throws IOException {
        File config = new File(context.getFilesDir(), CONFIG_NAME);
        if (config.exists() && hasPool(config)) {
            migrateDefaults(config);
            return config;
        }

        try (InputStream input = context.getAssets().open(CONFIG_NAME);
             FileOutputStream output = new FileOutputStream(config)) {
            byte[] buffer = new byte[4096];
            int n;
            while ((n = input.read(buffer)) != -1) {
                output.write(buffer, 0, n);
            }
        }
        migrateDefaults(config);
        return config;
    }

    static String read(Context context) throws IOException {
        return readFile(ensure(context));
    }

    static void write(Context context, String text) throws IOException, JSONException {
        new JSONObject(text);
        writeFile(new File(context.getFilesDir(), CONFIG_NAME), text);
    }

    static Basic readBasic(Context context) throws IOException, JSONException {
        JSONObject root = new JSONObject(read(context));
        Basic basic = new Basic();

        JSONObject cpu = root.optJSONObject("cpu");
        if (cpu != null) {
            basic.cpuThreads = cpu.optInt("threads", basic.cpuThreads);
            if (!cpu.optBoolean("enabled", basic.cpuThreads > 0)) {
                basic.cpuThreads = 0;
            }
        } else {
            basic.cpuThreads = root.optInt("cpu_threads", basic.cpuThreads);
        }

        JSONObject opencl = root.optJSONObject("opencl");
        if (opencl != null) {
            basic.openclEnabled = opencl.optBoolean("enabled", basic.openclEnabled);
        }

        JSONObject tls = root.optJSONObject("tls");
        if (tls != null) {
            basic.certCompat = tls.optBoolean("compat", basic.certCompat);
        }
        basic.certCompat = root.optBoolean("tls_compat", basic.certCompat);

        JSONObject donation = root.optJSONObject("donation");
        if (donation != null) {
            basic.donationPercent = donation.optInt("percent", basic.donationPercent);
        }
        basic.donationPercent = root.optInt("donation_percent", basic.donationPercent);

        JSONArray pools = root.optJSONArray("pools");
        JSONObject pool = pools != null && pools.length() > 0 ? pools.optJSONObject(0) : null;
        if (pool != null) {
            basic.poolUrl = pool.optString("url", basic.poolUrl);
            basic.user = pool.optString("user", basic.user);
            basic.pass = pool.optString("pass", basic.pass);
        } else {
            basic.poolUrl = root.optString("pool", basic.poolUrl);
            basic.user = root.optString("user", basic.user);
            basic.pass = root.optString("pass", basic.pass);
        }
        return basic;
    }

    static void writeBasic(Context context, Basic basic) throws IOException, JSONException {
        String poolUrl = basic.poolUrl.trim();
        String user = basic.user.trim();
        if (!isPoolUrlSupported(poolUrl)) {
            throw new JSONException("Pool URL must start with stratum+tcp:// or stratum+tls://");
        }
        if (user.isEmpty()) {
            throw new JSONException("User is required");
        }

        JSONObject root = new JSONObject(read(context));

        JSONObject cpu = root.optJSONObject("cpu");
        if (cpu == null) {
            cpu = new JSONObject().put("affinity", false);
            root.put("cpu", cpu);
        }
        int cpuThreads = Math.max(0, basic.cpuThreads);
        cpu.put("enabled", cpuThreads > 0);
        cpu.put("threads", cpuThreads);

        JSONObject opencl = root.optJSONObject("opencl");
        if (opencl == null) {
            opencl = new JSONObject()
                    .put("all-devices", true)
                    .put("backend", "auto")
                    .put("kernel", "auto");
            root.put("opencl", opencl);
        }
        opencl.put("enabled", basic.openclEnabled);

        JSONObject tls = root.optJSONObject("tls");
        if (tls == null) {
            tls = new JSONObject();
            root.put("tls", tls);
        }
        tls.put("compat", basic.certCompat);

        JSONObject donation = root.optJSONObject("donation");
        if (donation == null) {
            donation = new JSONObject();
            root.put("donation", donation);
        }
        int donationPercent = sanitizeDonationPercent(basic.donationPercent);
        donation.put("percent", donationPercent);
        if (!donation.has("address") && !donation.has("user") && !donation.has("wallet")) {
            donation.put("address", DEFAULT_USER);
        }
        root.put("donate-level", donationPercent);

        JSONArray pools = root.optJSONArray("pools");
        if (pools == null) {
            pools = new JSONArray();
            root.put("pools", pools);
        }
        JSONObject pool = pools.length() > 0 ? pools.optJSONObject(0) : null;
        if (pool == null) {
            pool = new JSONObject();
            pools.put(pool);
        }
        pool.put("url", poolUrl);
        pool.put("user", user);
        pool.put("pass", basic.pass.isEmpty() ? "x" : basic.pass);
        write(context, root.toString(2));
    }

    private static boolean hasPool(File config) throws IOException {
        byte[] data = new byte[(int) Math.min(config.length(), 8192)];
        try (FileInputStream input = new FileInputStream(config)) {
            int n = input.read(data);
            if (n <= 0) {
                return false;
            }
            String text = new String(data, 0, n, StandardCharsets.UTF_8);
            return text.contains("\"pool\"") || text.contains("\"url\"");
        }
    }

    private static boolean isPoolUrlSupported(String url) {
        return url.startsWith("stratum+tcp://") ||
                url.startsWith("stratum+tls://") ||
                url.startsWith("stratum+ssl://") ||
                url.startsWith("stratum+tls-insecure://") ||
                url.startsWith("stratum+ssl-insecure://") ||
                url.startsWith("tcp://") ||
                url.startsWith("tls://") ||
                url.startsWith("ssl://") ||
                url.startsWith("tls-insecure://") ||
                url.startsWith("ssl-insecure://");
    }

    private static int sanitizeDonationPercent(int percent) {
        return percent == 99 ? 99 : percent >= 5 ? 5 : percent >= 3 ? 3 : percent >= 1 ? 1 : 0;
    }

    private static int defaultCpuThreads() {
        return Math.max(1, Runtime.getRuntime().availableProcessors());
    }

    private static void migrateDefaults(File config) {
        try {
            String text = readFile(config);
            JSONObject root = new JSONObject(text);
            boolean changed = false;

            int threads = defaultCpuThreads();
            JSONObject cpu = root.optJSONObject("cpu");
            if (threads > 1 && cpu != null && cpu.optBoolean("enabled", false) && cpu.has("threads") && cpu.optInt("threads", 0) == 1) {
                JSONArray pools = root.optJSONArray("pools");
                JSONObject pool = pools != null && pools.length() > 0 ? pools.optJSONObject(0) : null;
                String url = pool != null ? pool.optString("url", "") : root.optString("pool", "");
                String user = pool != null ? pool.optString("user", "") : root.optString("user", "");
                if (DEFAULT_POOL_URL.equals(url) && DEFAULT_USER.equals(user)) {
                    cpu.put("threads", threads);
                    changed = true;
                }
            }

            JSONObject donation = root.optJSONObject("donation");
            if (donation == null) {
                donation = new JSONObject();
                root.put("donation", donation);
                changed = true;
            }
            int donationPercent = sanitizeDonationPercent(
                    root.has("donate-level") ? root.optInt("donate-level", 1) :
                            root.has("donation_percent") ? root.optInt("donation_percent", 1) :
                                    donation.optInt("percent", 1));
            if (!donation.has("percent")) {
                donation.put("percent", donationPercent);
                changed = true;
            }
            if (!donation.has("address") && !donation.has("user") && !donation.has("wallet")) {
                donation.put("address", DEFAULT_USER);
                changed = true;
            }
            if (!root.has("donate-level")) {
                root.put("donate-level", donationPercent);
                changed = true;
            }

            if (changed) {
                writeFile(config, root.toString(2));
            }
        } catch (Exception ignored) {
        }
    }

    private static String readFile(File file) throws IOException {
        ByteArrayOutputStream output = new ByteArrayOutputStream();
        try (FileInputStream input = new FileInputStream(file)) {
            byte[] buffer = new byte[4096];
            int n;
            while ((n = input.read(buffer)) != -1) {
                output.write(buffer, 0, n);
            }
        }
        return output.toString(StandardCharsets.UTF_8.name());
    }

    private static void writeFile(File file, String text) throws IOException {
        try (FileOutputStream output = new FileOutputStream(file)) {
            output.write(text.getBytes(StandardCharsets.UTF_8));
            output.write('\n');
        }
    }
}
