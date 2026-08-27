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

    static final class Basic {
        String poolUrl = "";
        String user = "";
        String pass = "x";
        int cpuThreads = 0;
        boolean openclEnabled = true;
    }

    private BtcrigConfig() {
    }

    static File ensure(Context context) throws IOException {
        File config = new File(context.getFilesDir(), CONFIG_NAME);
        if (config.exists() && hasPool(config)) {
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
        return config;
    }

    static String read(Context context) throws IOException {
        File config = ensure(context);
        ByteArrayOutputStream output = new ByteArrayOutputStream();
        try (FileInputStream input = new FileInputStream(config)) {
            byte[] buffer = new byte[4096];
            int n;
            while ((n = input.read(buffer)) != -1) {
                output.write(buffer, 0, n);
            }
        }
        return output.toString(StandardCharsets.UTF_8.name());
    }

    static void write(Context context, String text) throws IOException, JSONException {
        new JSONObject(text);
        File config = new File(context.getFilesDir(), CONFIG_NAME);
        try (FileOutputStream output = new FileOutputStream(config)) {
            output.write(text.getBytes(StandardCharsets.UTF_8));
            output.write('\n');
        }
    }

    static Basic readBasic(Context context) throws IOException, JSONException {
        JSONObject root = new JSONObject(read(context));
        Basic basic = new Basic();

        JSONObject cpu = root.optJSONObject("cpu");
        if (cpu != null) {
            basic.cpuThreads = cpu.optInt("threads", basic.cpuThreads);
        } else {
            basic.cpuThreads = root.optInt("cpu_threads", basic.cpuThreads);
        }

        JSONObject opencl = root.optJSONObject("opencl");
        if (opencl != null) {
            basic.openclEnabled = opencl.optBoolean("enabled", basic.openclEnabled);
        }

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
        if (poolUrl.isEmpty() || !poolUrl.startsWith("stratum+tcp://")) {
            throw new JSONException("Pool URL must start with stratum+tcp://");
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
        cpu.put("enabled", true);
        cpu.put("threads", Math.max(0, basic.cpuThreads));

        JSONObject opencl = root.optJSONObject("opencl");
        if (opencl == null) {
            opencl = new JSONObject()
                    .put("all-devices", true)
                    .put("backend", "auto")
                    .put("kernel", "auto");
            root.put("opencl", opencl);
        }
        opencl.put("enabled", basic.openclEnabled);

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
            return text.contains("\"pool\"") || text.contains("\"pools\"");
        }
    }
}
