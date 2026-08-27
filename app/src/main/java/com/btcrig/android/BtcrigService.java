package com.btcrig.android;

import android.app.Notification;
import android.app.NotificationChannel;
import android.app.NotificationManager;
import android.app.PendingIntent;
import android.app.Service;
import android.content.Intent;
import android.os.Build;
import android.os.IBinder;

import java.io.File;
import java.io.IOException;

public final class BtcrigService extends Service {
    static final String ACTION_STOP = "com.btcrig.android.STOP";
    private static final String CHANNEL_ID = "btcrig";
    private static final int NOTIFICATION_ID = 1;

    @Override
    public int onStartCommand(Intent intent, int flags, int startId) {
        if (intent != null && ACTION_STOP.equals(intent.getAction())) {
            BtcrigNative.stop();
            stopSelf();
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
            stopSelf();
            return START_NOT_STICKY;
        }

        startForeground(NOTIFICATION_ID, buildNotification());
        return START_STICKY;
    }

    @Override
    public IBinder onBind(Intent intent) {
        return null;
    }

    @Override
    public void onDestroy() {
        BtcrigNative.stop();
        super.onDestroy();
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
                .setContentTitle("BTCRig is ready")
                .setContentText(BtcrigNative.isRunning()
                        ? "Stratum: " + BtcrigNative.stratumStatus()
                        : "Miner core is stopped.")
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
                "BTCRig",
                NotificationManager.IMPORTANCE_LOW);
        NotificationManager manager = getSystemService(NotificationManager.class);
        if (manager != null) {
            manager.createNotificationChannel(channel);
        }
    }
}
