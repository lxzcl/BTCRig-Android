package com.btcrig.android;

import android.Manifest;
import android.app.Activity;
import android.content.Intent;
import android.content.pm.PackageManager;
import android.os.Build;
import android.os.Bundle;
import android.view.Gravity;
import android.widget.Button;
import android.widget.LinearLayout;
import android.widget.TextView;

public final class MainActivity extends Activity {
    private TextView status;

    @Override
    protected void onCreate(Bundle state) {
        super.onCreate(state);
        requestNotificationPermission();

        status = new TextView(this);
        status.setText("BTCRig Android shell\nEngine: not connected");
        status.setTextSize(18);

        Button start = new Button(this);
        start.setText("Start service");
        start.setOnClickListener(view -> startBtcrigService());

        Button stop = new Button(this);
        stop.setText("Stop service");
        stop.setOnClickListener(view -> stopBtcrigService());

        LinearLayout layout = new LinearLayout(this);
        layout.setOrientation(LinearLayout.VERTICAL);
        layout.setGravity(Gravity.CENTER);
        layout.setPadding(48, 48, 48, 48);
        layout.addView(status);
        layout.addView(start);
        layout.addView(stop);
        setContentView(layout);
    }

    private void startBtcrigService() {
        Intent intent = new Intent(this, BtcrigService.class);
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.O) {
            startForegroundService(intent);
        } else {
            startService(intent);
        }
        status.setText("BTCRig Android shell\nService: running\nEngine: not connected");
    }

    private void stopBtcrigService() {
        Intent intent = new Intent(this, BtcrigService.class);
        intent.setAction(BtcrigService.ACTION_STOP);
        startService(intent);
        status.setText("BTCRig Android shell\nService: stopped\nEngine: not connected");
    }

    private void requestNotificationPermission() {
        if (Build.VERSION.SDK_INT >= 33
                && checkSelfPermission(Manifest.permission.POST_NOTIFICATIONS) != PackageManager.PERMISSION_GRANTED) {
            requestPermissions(new String[] {Manifest.permission.POST_NOTIFICATIONS}, 1);
        }
    }
}
