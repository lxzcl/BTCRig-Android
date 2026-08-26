package com.btcrig.android;

import android.content.Context;

import java.io.File;
import java.io.FileInputStream;
import java.io.FileOutputStream;
import java.io.IOException;
import java.io.InputStream;
import java.nio.charset.StandardCharsets;

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

    private static boolean hasPool(File config) throws IOException {
        byte[] data = new byte[(int) Math.min(config.length(), 8192)];
        try (FileInputStream input = new FileInputStream(config)) {
            int n = input.read(data);
            String text = new String(data, 0, n, StandardCharsets.UTF_8);
            return text.contains("\"pool\"") || text.contains("\"pools\"");
        }
    }
}
