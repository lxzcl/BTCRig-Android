package com.btcrig.android;

import static org.junit.Assert.assertEquals;

import java.io.File;
import java.io.FileOutputStream;
import java.nio.file.Files;
import org.junit.Test;

public final class LogFilesTest {
    @Test
    public void trimKeepsNewestBytes() throws Exception {
        File file = File.createTempFile("btcrig-log", ".txt");
        byte[] content = new byte[(int) LogFiles.MAX_BYTES + 1];
        for (int i = 0; i < content.length; i++) content[i] = (byte) i;
        try (FileOutputStream output = new FileOutputStream(file)) {
            output.write(content);
        }

        LogFiles.trim(file);

        byte[] trimmed = Files.readAllBytes(file.toPath());
        assertEquals(LogFiles.KEEP_BYTES, trimmed.length);
        for (int i = 0; i < trimmed.length; i++) {
            assertEquals(content[content.length - trimmed.length + i], trimmed[i]);
        }
        file.delete();
    }
}
