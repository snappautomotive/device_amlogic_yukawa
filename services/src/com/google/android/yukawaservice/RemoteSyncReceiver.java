// Copyright 2019 Google Inc. All Rights Reserved.

package com.google.android.yukawaservice;

import android.app.ActivityManager;
import android.bluetooth.BluetoothAdapter;
import android.content.BroadcastReceiver;
import android.content.ComponentName;
import android.content.Context;
import android.content.Intent;
import android.content.pm.PackageManager;
import android.net.Uri;
import android.os.PowerManager;
import android.os.SystemClock;
import android.provider.Settings;
import android.util.Log;
import android.view.KeyEvent;

import java.util.List;

/**
 * Handles global keys.
 */
public class RemoteSyncReceiver extends BroadcastReceiver {
    private static final String TAG = "YukawaGlobalKey";
    private static final String ACTION_CONNECT_INPUT_NORMAL =
            "com.google.android.intent.action.CONNECT_INPUT";
    private static final String INTENT_EXTRA_NO_INPUT_MODE = "no_input_mode";

    private static void sendPairingIntent(Context context, KeyEvent event) {
        Intent intent = new Intent(ACTION_CONNECT_INPUT_NORMAL)
                .addFlags(Intent.FLAG_ACTIVITY_NEW_TASK);

        if (event != null) {
            intent.putExtra(INTENT_EXTRA_NO_INPUT_MODE, true)
                    .putExtra(Intent.EXTRA_KEY_EVENT, event);
        }
        context.startActivity(intent);
    }

    @Override
    public void onReceive(Context context, Intent intent) {
        if (Intent.ACTION_GLOBAL_BUTTON.equals(intent.getAction())) {
            KeyEvent event = intent.getParcelableExtra(Intent.EXTRA_KEY_EVENT);
            int keyCode = event.getKeyCode();
            int keyAction = event.getAction();
            switch (keyCode) {
                case KeyEvent.KEYCODE_PAIRING:
                    if (keyAction == KeyEvent.ACTION_UP) {
                        sendPairingIntent(context, event);
                    }
                    break;

                case KeyEvent.KEYCODE_BUTTON_2:
                    break;

                case KeyEvent.KEYCODE_BUTTON_3:
                    break;

                default:
                    Log.e(TAG, "Unhandled KeyEvent: " + keyCode);
            }
        }
    }

    private static void launchAppByPackageName(Context context, String packageName) {
        Intent launchIntent = context.getPackageManager().getLaunchIntentForPackage(packageName);
        context.startActivity(launchIntent);
    }
}
