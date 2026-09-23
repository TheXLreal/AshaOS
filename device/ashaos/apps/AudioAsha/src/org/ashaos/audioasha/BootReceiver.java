/*
 * Copyright (C) 2026 The AshaOS Project
 * Licensed under the Apache License, Version 2.0.
 */

package org.ashaos.audioasha;

import android.content.BroadcastReceiver;
import android.content.Context;
import android.content.Intent;
import android.content.SharedPreferences;

/** Restores the selected listening mode when the opt-in boot preference is enabled. */
public final class BootReceiver extends BroadcastReceiver {
    @Override
    public void onReceive(Context context, Intent intent) {
        if (!Intent.ACTION_BOOT_COMPLETED.equals(intent.getAction())) {
            return;
        }
        SharedPreferences preferences = context.getSharedPreferences(
                AudioAshaService.PREFERENCES, Context.MODE_PRIVATE);
        if (!preferences.getBoolean(AudioAshaService.PREF_START_AFTER_BOOT, false)) {
            return;
        }
        int migratedMode = preferences.getBoolean(
                AudioAshaService.PREF_DIRECT_SYSTEM_MODE, false)
                ? ConnectionEndpoint.MODE_DIRECT_USB_TCP
                : ConnectionEndpoint.MODE_WIFI_UDP;
        int connectionMode = preferences.getInt(
                AudioAshaService.PREF_CONNECTION_MODE, migratedMode);
        Intent serviceIntent = new Intent(context, AudioAshaService.class)
                .setAction(AudioAshaService.ACTION_START_LISTENING)
                .putExtra(AudioAshaService.EXTRA_PORT,
                        preferences.getInt(AudioAshaService.PREF_PORT,
                                AudioAshaService.DEFAULT_PORT))
                .putExtra(AudioAshaService.EXTRA_JITTER_MS,
                        preferences.getInt(AudioAshaService.PREF_JITTER_MS,
                                AudioAshaService.DEFAULT_JITTER_MS))
                .putExtra(AudioAshaService.EXTRA_CONNECTION_MODE, connectionMode)
                .putExtra(AudioAshaService.EXTRA_DIRECT_SYSTEM_MODE,
                        ConnectionEndpoint.isDirect(connectionMode));
        context.startForegroundService(serviceIntent);
    }
}
