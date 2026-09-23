/*
 * Copyright (C) 2026 The AshaOS Project
 * Licensed under the Apache License, Version 2.0.
 */

package org.ashaos.audioasha;

import android.accounts.AccountManager;
import android.app.Activity;
import android.content.Context;
import android.app.NotificationManager;
import android.bluetooth.BluetoothManager;
import android.content.Intent;
import android.content.pm.ApplicationInfo;
import android.content.pm.PackageInfo;
import android.content.pm.PackageManager;
import android.content.pm.ResolveInfo;
import android.media.AudioDeviceInfo;
import android.media.AudioManager;
import android.net.Uri;
import android.os.Bundle;
import android.webkit.WebView;
import android.widget.TextView;

import java.util.List;
import java.util.Locale;

/** Public-API runtime checks relevant to an authorized Nucleus Smart install. */
public final class CompatibilityActivity extends Activity {
    private TextView mStatus;

    @Override
    protected void attachBaseContext(Context base) {
        super.attachBaseContext(UiAppearance.wrap(base));
    }

    @Override
    protected void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);
        UiAppearance.selectTheme(this);
        setContentView(R.layout.activity_compatibility);
        UiAppearance.applyInsets(this, findViewById(R.id.compatibility_scroll));
        mStatus = findViewById(R.id.compatibility_status);
        findViewById(R.id.refresh_compatibility).setOnClickListener(view -> refresh());
        refresh();
    }

    private void refresh() {
        PackageManager packageManager = getPackageManager();
        AudioManager audioManager = getSystemService(AudioManager.class);
        boolean hearingAidOutput = false;
        if (audioManager != null) {
            for (AudioDeviceInfo device :
                    audioManager.getDevices(AudioManager.GET_DEVICES_OUTPUTS)) {
                if (device.getType() == AudioDeviceInfo.TYPE_HEARING_AID) {
                    hearingAidOutput = true;
                    break;
                }
            }
        }

        boolean bluetoothLe = packageManager.hasSystemFeature(
                PackageManager.FEATURE_BLUETOOTH_LE);
        boolean bluetoothGattApi = getSystemService(BluetoothManager.class) != null;
        boolean playServices = isPackageInstalled(packageManager, "com.google.android.gms");
        boolean packageInstaller = isPackageInstalled(
                packageManager, "com.android.packageinstaller");
        PackageInfo webViewPackage = WebView.getCurrentWebViewPackage();
        ResolveInfo browser = packageManager.resolveActivity(
                new Intent(Intent.ACTION_VIEW, Uri.parse("https://example.invalid"))
                        .addCategory(Intent.CATEGORY_BROWSABLE),
                PackageManager.MATCH_DEFAULT_ONLY);
        String nucleusPackage = findNucleusSmartLauncherPackage(packageManager);
        NotificationManager notificationManager =
                getSystemService(NotificationManager.class);

        StringBuilder result = new StringBuilder();
        append(result, getString(R.string.compat_installed),
                getString(R.string.value_yes) + " (" + getPackageName() + ")");
        append(result, getString(R.string.compat_hearing_output), yesNo(hearingAidOutput));
        append(result, getString(R.string.compat_ble_feature), yesNo(bluetoothLe));
        append(result, getString(R.string.compat_gatt), yesNo(bluetoothGattApi));
        append(result, getString(R.string.compat_play_services), yesNo(playServices));
        append(result, getString(R.string.compat_installer), yesNo(packageInstaller));
        append(result, getString(R.string.compat_webview), webViewPackage == null
                ? getString(R.string.compat_unavailable) : webViewPackage.packageName);
        append(result, getString(R.string.compat_browser), browser == null
                ? getString(R.string.compat_unavailable) : browser.activityInfo.packageName);
        append(result, getString(R.string.compat_accounts),
                yesNo(AccountManager.get(this) != null));
        append(result, getString(R.string.compat_notifications), notificationManager == null
                ? getString(R.string.value_unknown)
                : yesNo(notificationManager.areNotificationsEnabled()));
        append(result, "Nucleus Smart", nucleusPackage == null
                ? getString(R.string.compat_nucleus_missing)
                : getString(R.string.compat_nucleus_detected, nucleusPackage));
        append(result, getString(R.string.compat_asha_path),
                getString(R.string.compat_asha_path_value));
        mStatus.setText(result.toString());
    }

    @SuppressWarnings("deprecation")
    private static boolean isPackageInstalled(PackageManager packageManager, String packageName) {
        try {
            ApplicationInfo info = packageManager.getApplicationInfo(packageName, 0);
            return info.enabled;
        } catch (PackageManager.NameNotFoundException e) {
            return false;
        }
    }

    @SuppressWarnings("deprecation")
    private static String findNucleusSmartLauncherPackage(PackageManager packageManager) {
        Intent launcherIntent = new Intent(Intent.ACTION_MAIN)
                .addCategory(Intent.CATEGORY_LAUNCHER);
        List<ResolveInfo> launchers = packageManager.queryIntentActivities(launcherIntent, 0);
        for (ResolveInfo resolveInfo : launchers) {
            if (resolveInfo.activityInfo == null
                    || resolveInfo.activityInfo.applicationInfo == null) {
                continue;
            }
            CharSequence activityLabel = resolveInfo.loadLabel(packageManager);
            CharSequence applicationLabel = packageManager.getApplicationLabel(
                    resolveInfo.activityInfo.applicationInfo);
            String searchable = ((activityLabel == null ? "" : activityLabel.toString())
                    + " " + (applicationLabel == null ? "" : applicationLabel.toString()))
                    .toLowerCase(Locale.ROOT);
            if (searchable.contains("nucleus") && searchable.contains("smart")) {
                return resolveInfo.activityInfo.packageName;
            }
        }
        return null;
    }

    private static void append(StringBuilder builder, String label, String value) {
        if (builder.length() != 0) {
            builder.append("\n");
        }
        builder.append(label).append(": ").append(value);
    }

    private String yesNo(boolean value) {
        return getString(value ? R.string.value_yes : R.string.value_no);
    }
}
