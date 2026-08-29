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

public final class BtcrigService extends Service {
    static final String ACTION_STOP = "com.btcrig.android.STOP";
    private static final String CHANNEL_ID = "btcrig";
    private static final int NOTIFICATION_ID = 1;
    private PowerManager.WakeLock wakeLock;
    private boolean stopping;

    @Override
    public int onStartCommand(Intent intent, int flags, int startId) {
        if (intent != null && ACTION_STOP.equals(intent.getAction())) {
            stopCoreAsync();
            return START_NOT_STICKY;
        }

        createNotificationChannel();
        File config;
        try {
            config = BtcrigConfig.ensure(this);
        } catch (IOException e) {
            stopSelf();
            return START_NOT_STICKY;
        }
        if (!BtcrigNative.start(config.getAbsolutePath())) {
            releaseWakeLock();
            stopSelf();
            return START_NOT_STICKY;
        }

        acquireWakeLock();
        startForeground(NOTIFICATION_ID, buildNotification());
        return START_STICKY;
    }

    @Override
    public IBinder onBind(Intent intent) {
        return null;
    }

    @Override
    public void onDestroy() {
        stopCoreAsync();
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

    private void stopCoreAsync() {
        if (!markStopping()) {
            return;
        }
        new Thread(() -> {
            safeStopCore();
            releaseWakeLock();
            stopSelf();
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
                .setContentTitle(getString(R.string.notification_title_ready))
                .setContentText(BtcrigNative.isRunning()
                        ? getString(R.string.notification_stratum, BtcrigNative.stratumStatus())
                        : getString(R.string.notification_core_stopped))
                .setSmallIcon(android.R.drawable.stat_sys_upload)
                .setContentIntent(open)
                .setOngoing(true)
                .build();
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
