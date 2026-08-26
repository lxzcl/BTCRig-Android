package com.btcrig.android;

import android.content.Context;

import java.io.File;
import java.io.FileOutputStream;
import java.io.IOException;
import java.io.InputStream;

final class BtcrigConfig {
    private static final String CONFIG_NAME = "config.json";

    private BtcrigConfig() {
    }

    static File ensure(Context context) throws IOException {
        File config = new File(context.getFilesDir(), CONFIG_NAME);
        if (config.exists()) {
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
}
