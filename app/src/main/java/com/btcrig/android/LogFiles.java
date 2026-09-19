package com.btcrig.android;

import java.io.File;
import java.io.IOException;
import java.io.RandomAccessFile;

final class LogFiles {
    static final long MAX_BYTES = 1024 * 1024;
    static final int KEEP_BYTES = 256 * 1024;

    private LogFiles() {}

    static void trim(File file) {
        if (file == null || file.length() <= MAX_BYTES) return;
        try (RandomAccessFile log = new RandomAccessFile(file, "rw")) {
            long length = log.length();
            int keep = (int) Math.min(length, KEEP_BYTES);
            byte[] tail = new byte[keep];
            log.seek(length - keep);
            log.readFully(tail);
            log.setLength(0);
            log.write(tail);
        } catch (IOException ignored) {
        }
    }
}
