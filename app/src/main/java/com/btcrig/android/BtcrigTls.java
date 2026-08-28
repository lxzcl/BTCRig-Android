package com.btcrig.android;

import java.io.InputStream;
import java.io.OutputStream;
import java.net.InetSocketAddress;
import java.net.SocketTimeoutException;
import java.security.SecureRandom;
import java.security.cert.X509Certificate;
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
    private static final int READ_TIMEOUT_MS = 1000;
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
            socket.setSoTimeout(READ_TIMEOUT_MS);

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
        try {
            int n = conn.input.read(data, 0, len);
            return n < 0 ? 0 : n;
        } catch (SocketTimeoutException e) {
            return -3;
        } catch (Exception e) {
            lastError = e.getClass().getSimpleName() + ": " + e.getMessage();
            return -1;
        }
    }

    static int pending(int id) {
        Conn conn = CONNS.get(id);
        if (conn == null) {
            return 0;
        }
        try {
            return conn.input.available();
        } catch (Exception ignored) {
            return 0;
        }
    }

    static void close(int id) {
        Conn conn = CONNS.remove(id);
        if (conn == null) {
            return;
        }
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

        Conn(SSLSocket socket) throws Exception {
            this.socket = socket;
            this.input = socket.getInputStream();
            this.output = socket.getOutputStream();
        }
    }
}
