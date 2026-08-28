package com.btcrig.android;

import java.io.InputStream;
import java.io.OutputStream;
import java.net.InetSocketAddress;
import java.security.SecureRandom;
import java.security.cert.X509Certificate;
import java.util.ArrayDeque;
import java.util.Arrays;
import java.util.concurrent.ConcurrentHashMap;
import java.util.concurrent.atomic.AtomicInteger;

import javax.net.ssl.SSLContext;
import javax.net.ssl.SSLParameters;
import javax.net.ssl.SSLSocket;
import javax.net.ssl.SSLSocketFactory;
import javax.net.ssl.TrustManager;
import javax.net.ssl.X509TrustManager;

final class BtcrigTls {
    private static final int CONNECT_TIMEOUT_MS = 15000;
    private static final int MAX_BUFFERED_BYTES = 1024 * 1024;
    private static final AtomicInteger NEXT_ID = new AtomicInteger(1);
    private static final ConcurrentHashMap<Integer, Conn> CONNS = new ConcurrentHashMap<>();
    private static volatile String lastError = "";

    private BtcrigTls() {
    }

    static int open(String host, String port, boolean verify) {
        try {
            SSLSocketFactory factory = verify
                    ? (SSLSocketFactory) SSLSocketFactory.getDefault()
                    : insecureFactory();
            SSLSocket socket = (SSLSocket) factory.createSocket();
            socket.connect(new InetSocketAddress(host, Integer.parseInt(port)), CONNECT_TIMEOUT_MS);
            socket.setSoTimeout(CONNECT_TIMEOUT_MS);
            if (verify) {
                SSLParameters params = socket.getSSLParameters();
                params.setEndpointIdentificationAlgorithm("HTTPS");
                socket.setSSLParameters(params);
            }
            socket.startHandshake();
            socket.setSoTimeout(0);

            int id = NEXT_ID.getAndIncrement();
            CONNS.put(id, new Conn(socket));
            lastError = "";
            return id;
        } catch (Exception e) {
            lastError = e.getClass().getSimpleName() + ": " + e.getMessage();
            return -1;
        }
    }

    static int write(int id, byte[] data, int len) {
        Conn conn = CONNS.get(id);
        if (conn == null) {
            return -1;
        }
        try {
            conn.output.write(data, 0, len);
            conn.output.flush();
            return len;
        } catch (Exception e) {
            lastError = e.getClass().getSimpleName() + ": " + e.getMessage();
            return -1;
        }
    }

    static int read(int id, byte[] data, int len) {
        Conn conn = CONNS.get(id);
        if (conn == null) {
            return -1;
        }
        synchronized (conn.lock) {
            if (conn.pending == 0) {
                if (conn.error != null) {
                    lastError = conn.error;
                    return -1;
                }
                return conn.eof ? 0 : -3;
            }

            int copied = 0;
            while (copied < len && !conn.chunks.isEmpty()) {
                byte[] chunk = conn.chunks.peek();
                int n = Math.min(len - copied, chunk.length - conn.offset);
                System.arraycopy(chunk, conn.offset, data, copied, n);
                copied += n;
                conn.offset += n;
                conn.pending -= n;
                if (conn.offset == chunk.length) {
                    conn.chunks.remove();
                    conn.offset = 0;
                }
            }
            return copied;
        }
    }

    static int pending(int id) {
        Conn conn = CONNS.get(id);
        if (conn == null) {
            return 0;
        }
        synchronized (conn.lock) {
            if (conn.pending > 0) {
                return conn.pending;
            }
            if (conn.error != null) {
                lastError = conn.error;
                return -1;
            }
            return conn.eof ? -2 : 0;
        }
    }

    static void close(int id) {
        Conn conn = CONNS.remove(id);
        if (conn == null) {
            return;
        }
        conn.closed = true;
        try {
            conn.socket.close();
        } catch (Exception ignored) {
        }
    }

    static String cipher(int id) {
        Conn conn = CONNS.get(id);
        return conn == null ? "unknown" : conn.socket.getSession().getCipherSuite();
    }

    static String lastError() {
        return lastError == null ? "" : lastError;
    }

    private static SSLSocketFactory insecureFactory() throws Exception {
        TrustManager[] trustAll = new TrustManager[]{
                new X509TrustManager() {
                    @Override
                    public void checkClientTrusted(X509Certificate[] chain, String authType) {
                    }

                    @Override
                    public void checkServerTrusted(X509Certificate[] chain, String authType) {
                    }

                    @Override
                    public X509Certificate[] getAcceptedIssuers() {
                        return new X509Certificate[0];
                    }
                }
        };
        SSLContext context = SSLContext.getInstance("TLS");
        context.init(null, trustAll, new SecureRandom());
        return context.getSocketFactory();
    }

    private static final class Conn {
        final SSLSocket socket;
        final InputStream input;
        final OutputStream output;
        final Object lock = new Object();
        final ArrayDeque<byte[]> chunks = new ArrayDeque<>();
        int pending;
        int offset;
        volatile boolean closed;
        boolean eof;
        String error;

        Conn(SSLSocket socket) throws Exception {
            this.socket = socket;
            this.input = socket.getInputStream();
            this.output = socket.getOutputStream();
            Thread reader = new Thread(this::readLoop, "btcrig-tls-reader");
            reader.setDaemon(true);
            reader.start();
        }

        private void readLoop() {
            byte[] buffer = new byte[4096];
            try {
                for (;;) {
                    int n = input.read(buffer);
                    synchronized (lock) {
                        if (n < 0) {
                            eof = true;
                            return;
                        }
                        if (pending + n > MAX_BUFFERED_BYTES) {
                            error = "TLS read buffer overflow";
                            closed = true;
                            try {
                                socket.close();
                            } catch (Exception ignored) {
                            }
                            return;
                        }
                        chunks.add(Arrays.copyOf(buffer, n));
                        pending += n;
                    }
                }
            } catch (Exception e) {
                if (!closed) {
                    synchronized (lock) {
                        error = e.getClass().getSimpleName() + ": " + e.getMessage();
                        lastError = error;
                    }
                }
            }
        }
    }
}
