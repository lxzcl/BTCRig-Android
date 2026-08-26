package com.btcrig.android;

import android.content.Context;

import java.io.File;
import java.io.ByteArrayOutputStream;
import java.io.FileInputStream;
import java.io.FileOutputStream;
import java.io.IOException;
import java.io.InputStream;
import java.nio.charset.StandardCharsets;

import org.json.JSONException;
import org.json.JSONObject;

final class BtcrigConfig {
    private static final String CONFIG_NAME = "config.json";

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
