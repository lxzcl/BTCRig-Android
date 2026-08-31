package com.btcrig.android;

import android.app.Notification;
import android.app.NotificationChannel;
import android.app.NotificationManager;
import android.app.PendingIntent;
import android.app.Service;
import android.content.Intent;
import android.os.Build;
import android.os.IBinder;
import android.os.PowerManager;

import java.io.File;
import java.io.IOException;
import java.util.Locale;

public final class BtcrigService extends Service {
    static final String ACTION_STOP = "com.btcrig.android.STOP";
    private static final String CHANNEL_ID = "btcrig";
    private static final int NOTIFICATION_ID = 1;
    private PowerManager.WakeLock wakeLock;
    private volatile boolean stopping;
    private volatile boolean notificationLoopRunning;
    private Thread notificationThread;

    @Override
    public int onStartCommand(Intent intent, int flags, int startId) {
        if (intent != null && ACTION_STOP.equals(intent.getAction())) {
            setDesiredRunning(false);
            stopNotificationLoop();
            stopCoreAsync(true);
            return START_NOT_STICKY;
        }
        if (stopping) {
            return START_NOT_STICKY;
        }

        createNotificationChannel();
        startForeground(NOTIFICATION_ID, buildNotification());
        File config;
        try {
            config = BtcrigConfig.ensure(this);
        } catch (IOException e) {
            setDesiredRunning(false);
            stopSelf();
            return START_NOT_STICKY;
        }
        if (!BtcrigNative.start(config.getAbsolutePath())) {
            setDesiredRunning(false);
            releaseWakeLock();
            stopSelf();
            return START_NOT_STICKY;
        }

        setDesiredRunning(true);
        acquireWakeLock();
        startNotificationLoop();
        updateNotification();
        return START_STICKY;
    }

    @Override
    public IBinder onBind(Intent intent) {
        return null;
    }

    @Override
    public void onDestroy() {
        stopNotificationLoop();
        stopCoreAsync(false);
        releaseWakeLock();
        super.onDestroy();
    }

    private synchronized boolean markStopping() {
        if (stopping) {
            return false;
        }
        stopping = true;
        return true;
    }

    private void stopCoreAsync(boolean stopService) {
        if (!markStopping()) {
            return;
        }
        new Thread(() -> {
            safeStopCore();
            releaseWakeLock();
            updateNotification();
            if (stopService) {
                stopSelf();
            }
        }, "BTCRig-stop").start();
    }

    private void safeStopCore() {
        try {
            BtcrigNative.stop();
        } catch (Throwable ignored) {
        }
    }

    private void acquireWakeLock() {
        try {
            if (!BtcrigConfig.readBasic(this).wakeLock || wakeLock != null && wakeLock.isHeld()) {
                return;
            }
            PowerManager manager = (PowerManager) getSystemService(POWER_SERVICE);
            if (manager == null) {
                return;
            }
            wakeLock = manager.newWakeLock(PowerManager.PARTIAL_WAKE_LOCK, "BTCRig:Miner");
            wakeLock.setReferenceCounted(false);
            wakeLock.acquire();
        } catch (Exception ignored) {
        }
    }

    private void releaseWakeLock() {
        if (wakeLock != null && wakeLock.isHeld()) {
            wakeLock.release();
        }
        wakeLock = null;
    }

    private void setDesiredRunning(boolean running) {
        getSharedPreferences("service", MODE_PRIVATE)
                .edit()
                .putBoolean("desired_running", running)
                .apply();
    }

    private void startNotificationLoop() {
        notificationLoopRunning = true;
        if (notificationThread != null && notificationThread.isAlive()) {
            return;
        }
        notificationThread = new Thread(() -> {
            while (notificationLoopRunning && BtcrigNative.isRunning()) {
                updateNotification();
                try {
                    Thread.sleep(5000);
                } catch (InterruptedException e) {
                    return;
                }
            }
            updateNotification();
        }, "BTCRig-notify");
        notificationThread.start();
    }

    private void stopNotificationLoop() {
        notificationLoopRunning = false;
        if (notificationThread != null) {
            notificationThread.interrupt();
            notificationThread = null;
        }
    }

    @SuppressWarnings("deprecation")
    private Notification buildNotification() {
        Intent openIntent = getPackageManager().getLaunchIntentForPackage(getPackageName());
        if (openIntent == null) {
            openIntent = new Intent(this, MainActivity.class);
        }
        PendingIntent open = PendingIntent.getActivity(
                this,
                0,
                openIntent,
                PendingIntent.FLAG_IMMUTABLE | PendingIntent.FLAG_UPDATE_CURRENT);

        Notification.Builder builder = Build.VERSION.SDK_INT >= Build.VERSION_CODES.O
                ? new Notification.Builder(this, CHANNEL_ID)
                : new Notification.Builder(this);

        return builder
                .setContentTitle(BtcrigNative.isRunning()
                        ? getString(R.string.notification_title_running)
                        : getString(R.string.notification_title_idle))
                .setContentText(BtcrigNative.isRunning()
                        ? getString(R.string.notification_mining, formatHashrate(BtcrigNative.hashrate()), BtcrigNative.stratumStatus())
                        : getString(R.string.notification_service_idle))
                .setSmallIcon(android.R.drawable.stat_sys_upload)
                .setContentIntent(open)
                .setOnlyAlertOnce(true)
                .setOngoing(true)
                .build();
    }

    private static String formatHashrate(double hps) {
        if (hps >= 1000000.0) {
            return String.format(Locale.US, "%.2f MH/s", hps / 1000000.0);
        }
        if (hps >= 1000.0) {
            return String.format(Locale.US, "%.2f KH/s", hps / 1000.0);
        }
        return String.format(Locale.US, "%.0f H/s", hps);
    }

    private void updateNotification() {
        NotificationManager manager = (NotificationManager) getSystemService(NOTIFICATION_SERVICE);
        if (manager != null) {
            manager.notify(NOTIFICATION_ID, buildNotification());
        }
    }

    private void createNotificationChannel() {
        if (Build.VERSION.SDK_INT < Build.VERSION_CODES.O) {
            return;
        }

        NotificationChannel channel = new NotificationChannel(
                CHANNEL_ID,
                getString(R.string.app_name),
                NotificationManager.IMPORTANCE_LOW);
        NotificationManager manager = getSystemService(NotificationManager.class);
        if (manager != null) {
            manager.createNotificationChannel(channel);
        }
    }
}
