/*
 * Copyright (C) 2026 The AshaOS Project
 * Licensed under the Apache License, Version 2.0.
 */

package org.ashaos.audioasha;

import android.Manifest;
import android.app.Activity;
import android.app.AlertDialog;
import android.app.LocaleManager;
import android.content.ActivityNotFoundException;
import android.content.ClipData;
import android.content.ClipboardManager;
import android.content.ComponentName;
import android.content.Context;
import android.content.Intent;
import android.content.ServiceConnection;
import android.content.SharedPreferences;
import android.content.pm.PackageInfo;
import android.content.pm.PackageManager;
import android.media.AudioDeviceInfo;
import android.media.AudioManager;
import android.os.Build;
import android.os.Bundle;
import android.os.Handler;
import android.os.IBinder;
import android.os.Looper;
import android.os.LocaleList;
import android.provider.Settings;
import android.net.Uri;
import android.view.View;
import android.widget.ArrayAdapter;
import android.widget.Button;
import android.widget.EditText;
import android.widget.Spinner;
import android.widget.Switch;
import android.widget.TextView;
import android.widget.Toast;

import java.util.ArrayList;
import java.util.List;

/** Audio-Asha foreground-service control, connection selection, and diagnostics UI. */
public final class MainActivity extends Activity {
    private static final int[] JITTER_VALUES_MS = {0, 5, 10, 15, 20, 30, 50};
    private static final int NOTIFICATION_PERMISSION_REQUEST = 42;

    private final Handler mHandler = new Handler(Looper.getMainLooper());
    private final List<AudioDeviceInfo> mOutputDevices = new ArrayList<>();
    private final Runnable mDiagnosticsRefresh = new Runnable() {
        @Override
        public void run() {
            updateDiagnostics();
            mHandler.postDelayed(this, 1000);
        }
    };
    private final ServiceConnection mServiceConnection = new ServiceConnection() {
        @Override
        public void onServiceConnected(ComponentName name, IBinder binder) {
            AudioAshaService.LocalBinder localBinder =
                    (AudioAshaService.LocalBinder) binder;
            mService = localBinder.getService();
            mBound = true;
            mService.setPreferredDevice(
                    getSelectedOutput(mOutputSpinner.getSelectedItemPosition()));
            updateDiagnostics();
        }

        @Override
        public void onServiceDisconnected(ComponentName name) {
            mBound = false;
            mService = null;
            updateDiagnostics();
        }
    };

    private AudioAshaService mService;
    private boolean mBound;
    private boolean mPendingStart;
    private EditText mPortText;
    private Spinner mConnectionSpinner;
    private Spinner mJitterSpinner;
    private Spinner mOutputSpinner;
    private Switch mBootSwitch;
    private TextView mConnectionEndpointText;
    private TextView mDiagnosticsText;
    private Button mCopyEndpointButton;
    private Button mStartButton;
    private Button mStopButton;
    private ConnectionEndpoint mDisplayedEndpoint;

    @Override
    protected void attachBaseContext(Context base) {
        super.attachBaseContext(UiAppearance.wrap(base));
    }

    @Override
    protected void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);
        UiAppearance.selectTheme(this);
        setContentView(R.layout.activity_main);
        UiAppearance.applyInsets(this, findViewById(R.id.main_scroll));

        mPortText = findViewById(R.id.udp_port);
        mConnectionSpinner = findViewById(R.id.connection_mode);
        mJitterSpinner = findViewById(R.id.jitter_buffer);
        mOutputSpinner = findViewById(R.id.output_device);
        mBootSwitch = findViewById(R.id.start_after_boot);
        mConnectionEndpointText = findViewById(R.id.connection_endpoint);
        mDiagnosticsText = findViewById(R.id.audio_devices);
        mCopyEndpointButton = findViewById(R.id.copy_connection_endpoint);
        mStartButton = findViewById(R.id.start_streaming);
        mStopButton = findViewById(R.id.stop_streaming);
        findViewById(R.id.open_appearance).setOnClickListener(
                view -> showAppearanceSettings());
        findViewById(R.id.help_connection).setOnClickListener(view ->
                new AlertDialog.Builder(this)
                        .setTitle(R.string.help_connection_title)
                        .setMessage(R.string.help_connection_body)
                        .setPositiveButton(android.R.string.ok, null).show());
        findViewById(R.id.help_jitter).setOnClickListener(view ->
                new AlertDialog.Builder(this)
                        .setTitle(R.string.help_jitter_title)
                        .setMessage(R.string.help_jitter_body)
                        .setPositiveButton(android.R.string.ok, null).show());
        findViewById(R.id.open_tether_settings).setOnClickListener(view ->
                openSettings("android.settings.TETHER_SETTINGS",
                        Settings.ACTION_WIRELESS_SETTINGS));
        findViewById(R.id.open_developer_settings).setOnClickListener(view ->
                openSettings("android.settings.APPLICATION_DEVELOPMENT_SETTINGS",
                        Settings.ACTION_SETTINGS));
        findViewById(R.id.open_hearing_settings).setOnClickListener(view ->
                openSettings(Settings.ACTION_ACCESSIBILITY_SETTINGS, Settings.ACTION_SETTINGS));
        findViewById(R.id.open_bluetooth_settings).setOnClickListener(view ->
                openSettings(Settings.ACTION_BLUETOOTH_SETTINGS, Settings.ACTION_SETTINGS));
        findViewById(R.id.open_wifi_settings).setOnClickListener(view ->
                openSettings(Settings.ACTION_WIFI_SETTINGS, Settings.ACTION_SETTINGS));
        findViewById(R.id.open_app_settings).setOnClickListener(view -> {
            Intent details = new Intent(Settings.ACTION_APPLICATION_DETAILS_SETTINGS,
                    Uri.parse("package:" + getPackageName()));
            try {
                startActivity(details);
            } catch (ActivityNotFoundException e) {
                openSettings(Settings.ACTION_SETTINGS, null);
            }
        });
        findViewById(R.id.toggle_diagnostics).setOnClickListener(view -> {
            View panel = findViewById(R.id.diagnostics_panel);
            boolean show = panel.getVisibility() != View.VISIBLE;
            panel.setVisibility(show ? View.VISIBLE : View.GONE);
            ((Button) view).setText(show
                    ? R.string.hide_diagnostics : R.string.show_diagnostics);
            if (show) {
                updateDiagnostics();
            }
        });

        ((TextView) findViewById(R.id.android_version)).setText(getString(
                R.string.android_version_format, Build.VERSION.RELEASE, Build.VERSION.SDK_INT));
        ((TextView) findViewById(R.id.device_product)).setText(getString(
                R.string.device_product_format, Build.MODEL, Build.PRODUCT));
        ((TextView) findViewById(R.id.application_version)).setText(getString(
                R.string.application_version_format, getApplicationVersion()));

        SharedPreferences preferences = getSharedPreferences(
                AudioAshaService.PREFERENCES, MODE_PRIVATE);
        mPortText.setText(String.valueOf(preferences.getInt(
                AudioAshaService.PREF_PORT, AudioAshaService.DEFAULT_PORT)));
        mJitterSpinner.setSelection(jitterPosition(preferences.getInt(
                AudioAshaService.PREF_JITTER_MS, AudioAshaService.DEFAULT_JITTER_MS)));
        mBootSwitch.setChecked(preferences.getBoolean(
                AudioAshaService.PREF_START_AFTER_BOOT, false));
        int oldDefaultMode = preferences.getBoolean(
                AudioAshaService.PREF_DIRECT_SYSTEM_MODE, false)
                ? ConnectionEndpoint.MODE_DIRECT_USB_TCP
                : ConnectionEndpoint.MODE_WIFI_UDP;
        int connectionMode = preferences.getInt(
                AudioAshaService.PREF_CONNECTION_MODE, oldDefaultMode);
        if (!ConnectionEndpoint.isValidMode(connectionMode)) {
            connectionMode = ConnectionEndpoint.MODE_WIFI_UDP;
        }
        mConnectionSpinner.setSelection(connectionMode);
        mConnectionSpinner.setOnItemSelectedListener(
                new SimpleItemSelectedListener(position -> {
                    if (!ConnectionEndpoint.isValidMode(position)) {
                        return;
                    }
                    getSharedPreferences(AudioAshaService.PREFERENCES, MODE_PRIVATE)
                            .edit()
                            .putInt(AudioAshaService.PREF_CONNECTION_MODE, position)
                            .putBoolean(AudioAshaService.PREF_DIRECT_SYSTEM_MODE,
                                    ConnectionEndpoint.isDirect(position))
                            .apply();
                    setConfigurationEnabled(mStartButton.isEnabled());
                    updateConnectionEndpoint();
                }));
        mBootSwitch.setOnCheckedChangeListener((button, enabled) ->
                getSharedPreferences(AudioAshaService.PREFERENCES, MODE_PRIVATE)
                        .edit()
                        .putBoolean(AudioAshaService.PREF_START_AFTER_BOOT, enabled)
                        .apply());

        refreshOutputDevices();
        findViewById(R.id.check_audio_devices).setOnClickListener(
                view -> refreshOutputDevices());
        findViewById(R.id.compatibility_diagnostics).setOnClickListener(view ->
                startActivity(new Intent(this, CompatibilityActivity.class)));
        mCopyEndpointButton.setOnClickListener(view -> copyConnectionEndpoint());
        mStartButton.setOnClickListener(view -> requestStartStreaming());
        mStopButton.setOnClickListener(view -> stopStreaming());
        mOutputSpinner.setOnItemSelectedListener(
                new SimpleItemSelectedListener(position -> {
                    if (mService != null) {
                        mService.setPreferredDevice(getSelectedOutput(position));
                    }
                }));
        updateConnectionEndpoint();
    }

    @Override
    protected void onStart() {
        super.onStart();
        bindService(new Intent(this, AudioAshaService.class),
                mServiceConnection, Context.BIND_AUTO_CREATE);
        mHandler.post(mDiagnosticsRefresh);
    }

    @Override
    protected void onStop() {
        mHandler.removeCallbacks(mDiagnosticsRefresh);
        if (mBound) {
            unbindService(mServiceConnection);
            mBound = false;
            mService = null;
        }
        super.onStop();
    }

    @Override
    public void onRequestPermissionsResult(
            int requestCode, String[] permissions, int[] grantResults) {
        super.onRequestPermissionsResult(requestCode, permissions, grantResults);
        if (requestCode != NOTIFICATION_PERMISSION_REQUEST || !mPendingStart) {
            return;
        }
        mPendingStart = false;
        if (grantResults.length == 0
                || grantResults[0] != PackageManager.PERMISSION_GRANTED) {
            Toast.makeText(this, R.string.notification_permission_denied,
                    Toast.LENGTH_LONG).show();
        }
        beginStartStreaming();
    }

    private void requestStartStreaming() {
        if (!isDirectMode() && !validatePort()) {
            return;
        }
        if (!updateConnectionEndpoint()) {
            Toast.makeText(this, getUnavailableGuidance(selectedConnectionMode()),
                    Toast.LENGTH_LONG).show();
            return;
        }
        if (checkSelfPermission(Manifest.permission.POST_NOTIFICATIONS)
                != PackageManager.PERMISSION_GRANTED) {
            mPendingStart = true;
            requestPermissions(new String[] {Manifest.permission.POST_NOTIFICATIONS},
                    NOTIFICATION_PERMISSION_REQUEST);
            return;
        }
        beginStartStreaming();
    }

    private void beginStartStreaming() {
        int connectionMode = selectedConnectionMode();
        int port = ConnectionEndpoint.isDirect(connectionMode)
                ? ConnectionEndpoint.DIRECT_PORT
                : Integer.parseInt(mPortText.getText().toString());
        int jitterMs = JITTER_VALUES_MS[mJitterSpinner.getSelectedItemPosition()];
        Intent serviceIntent = new Intent(this, AudioAshaService.class)
                .setAction(AudioAshaService.ACTION_START_LISTENING)
                .putExtra(AudioAshaService.EXTRA_PORT, port)
                .putExtra(AudioAshaService.EXTRA_JITTER_MS, jitterMs)
                .putExtra(AudioAshaService.EXTRA_CONNECTION_MODE, connectionMode)
                .putExtra(AudioAshaService.EXTRA_DIRECT_SYSTEM_MODE,
                        ConnectionEndpoint.isDirect(connectionMode));
        startForegroundService(serviceIntent);
        ((TextView) findViewById(R.id.session_status)).setText(R.string.session_starting);
        mStartButton.setEnabled(false);
        mStopButton.setEnabled(true);
        setConfigurationEnabled(false);
    }

    private void stopStreaming() {
        if (mService != null) {
            mService.stopListening();
        } else {
            startService(new Intent(this, AudioAshaService.class)
                    .setAction(AudioAshaService.ACTION_STOP));
        }
        mStartButton.setEnabled(true);
        mStopButton.setEnabled(false);
        setConfigurationEnabled(true);
    }

    private boolean validatePort() {
        int port;
        try {
            port = Integer.parseInt(mPortText.getText().toString());
        } catch (NumberFormatException e) {
            port = -1;
        }
        if (port < 1 || port > 65535) {
            Toast.makeText(this, R.string.invalid_port, Toast.LENGTH_SHORT).show();
            return false;
        }
        return true;
    }

    private AudioDeviceInfo getSelectedOutput(int position) {
        int deviceIndex = position - 1;
        return deviceIndex >= 0 && deviceIndex < mOutputDevices.size()
                ? mOutputDevices.get(deviceIndex) : null;
    }

    private void refreshOutputDevices() {
        AudioManager audioManager = getSystemService(AudioManager.class);
        List<String> labels = new ArrayList<>();
        labels.add(getString(R.string.system_default_output));
        mOutputDevices.clear();
        if (audioManager != null) {
            for (AudioDeviceInfo device :
                    audioManager.getDevices(AudioManager.GET_DEVICES_OUTPUTS)) {
                mOutputDevices.add(device);
                labels.add(AudioStreamEngine.describeDevice(device));
            }
        }
        ArrayAdapter<String> adapter = new ArrayAdapter<>(
                this, android.R.layout.simple_spinner_item, labels);
        adapter.setDropDownViewResource(android.R.layout.simple_spinner_dropdown_item);
        mOutputSpinner.setAdapter(adapter);
    }

    private void updateDiagnostics() {
        boolean listening = mService != null && mService.isListening();
        if (findViewById(R.id.diagnostics_panel).getVisibility() == View.VISIBLE) {
            String details = mService != null
                    ? mService.getDiagnostics().format()
                    : getString(R.string.service_disconnected);
            if (!details.contentEquals(mDiagnosticsText.getText())) {
                mDiagnosticsText.setText(details);
            }
        }
        TextView status = findViewById(R.id.session_status);
        status.setText(!listening ? R.string.session_stopped
                : mService.isEngineRunning()
                        ? R.string.session_listening : R.string.session_reconnecting);
        mStartButton.setEnabled(!listening);
        mStopButton.setEnabled(listening);
        setConfigurationEnabled(!listening);
        updateConnectionEndpoint();
    }

    private boolean updateConnectionEndpoint() {
        int mode = selectedConnectionMode();
        int port = ConnectionEndpoint.isDirect(mode)
                ? ConnectionEndpoint.DIRECT_PORT : readPortOrDefault();
        mDisplayedEndpoint = ConnectionEndpoint.resolve(mode, port);
        String modeName = String.valueOf(mConnectionSpinner.getSelectedItem());
        if (mDisplayedEndpoint == null) {
            mConnectionEndpointText.setText(getString(
                    R.string.connection_endpoint_unavailable_format,
                    modeName, getString(getUnavailableGuidance(mode))));
            mCopyEndpointButton.setEnabled(false);
            return false;
        }
        String pcSettings = getString(!ConnectionEndpoint.isDirect(mode)
                ? R.string.pc_settings_udp
                : (ConnectionEndpoint.isDirectUdp(mode)
                        ? R.string.pc_settings_direct_udp
                        : R.string.pc_settings_direct_tcp));
        mConnectionEndpointText.setText(getString(
                R.string.connection_endpoint_ready_format,
                mDisplayedEndpoint.target(), modeName,
                mDisplayedEndpoint.interfaceName, pcSettings));
        mCopyEndpointButton.setEnabled(true);
        return true;
    }

    private void copyConnectionEndpoint() {
        if (!updateConnectionEndpoint() || mDisplayedEndpoint == null) {
            return;
        }
        ClipboardManager clipboard = getSystemService(ClipboardManager.class);
        if (clipboard != null) {
            clipboard.setPrimaryClip(ClipData.newPlainText(
                    getString(R.string.connection_target_label),
                    mDisplayedEndpoint.target()));
            Toast.makeText(this, R.string.connection_target_copied,
                    Toast.LENGTH_SHORT).show();
        }
    }

    private int readPortOrDefault() {
        try {
            int port = Integer.parseInt(mPortText.getText().toString());
            return port >= 1 && port <= 65535
                    ? port : AudioAshaService.DEFAULT_PORT;
        } catch (NumberFormatException e) {
            return AudioAshaService.DEFAULT_PORT;
        }
    }

    private int selectedConnectionMode() {
        int mode = mConnectionSpinner.getSelectedItemPosition();
        return ConnectionEndpoint.isValidMode(mode)
                ? mode : ConnectionEndpoint.MODE_WIFI_UDP;
    }

    private boolean isDirectMode() {
        return ConnectionEndpoint.isDirect(selectedConnectionMode());
    }

    private int getUnavailableGuidance(int mode) {
        switch (mode) {
            case ConnectionEndpoint.MODE_WIFI_UDP:
            case ConnectionEndpoint.MODE_DIRECT_WIFI_TCP:
            case ConnectionEndpoint.MODE_DIRECT_WIFI_UDP:
                return R.string.enable_wifi_guidance;
            case ConnectionEndpoint.MODE_USB_TETHER_UDP:
            case ConnectionEndpoint.MODE_DIRECT_USB_TCP:
            case ConnectionEndpoint.MODE_DIRECT_USB_UDP:
                return R.string.enable_usb_tether_guidance;
            case ConnectionEndpoint.MODE_BLUETOOTH_PAN_UDP:
            case ConnectionEndpoint.MODE_DIRECT_BLUETOOTH_TCP:
            case ConnectionEndpoint.MODE_DIRECT_BLUETOOTH_UDP:
                return R.string.enable_bluetooth_pan_guidance;
            default:
                return R.string.connection_unavailable;
        }
    }

    private void setConfigurationEnabled(boolean enabled) {
        mConnectionSpinner.setEnabled(enabled);
        boolean packetConfigurationEnabled = enabled && !isDirectMode();
        mPortText.setEnabled(packetConfigurationEnabled);
        mJitterSpinner.setEnabled(packetConfigurationEnabled);
        findViewById(R.id.udp_controls).setVisibility(
                isDirectMode() ? View.GONE : View.VISIBLE);
    }

    private void showAppearanceSettings() {
        new AlertDialog.Builder(this)
                .setTitle(R.string.appearance_title)
                .setItems(new String[] {getString(R.string.theme_setting),
                        getString(R.string.language_setting)}, (dialog, which) -> {
                    SharedPreferences preferences = getSharedPreferences(
                            AudioAshaService.PREFERENCES, MODE_PRIVATE);
                    if (which == 0) {
                        new AlertDialog.Builder(this)
                                .setTitle(R.string.theme_setting)
                                .setSingleChoiceItems(new String[] {
                                        getString(R.string.theme_system),
                                        getString(R.string.theme_light),
                                        getString(R.string.theme_dark)},
                                        preferences.getInt(UiAppearance.PREF_UI_THEME, 0),
                                        (choiceDialog, value) -> {
                                            preferences.edit().putInt(
                                                    UiAppearance.PREF_UI_THEME, value).apply();
                                            choiceDialog.dismiss();
                                            recreate();
                                        }).show();
                    } else {
                        LocaleManager localeManager = getSystemService(LocaleManager.class);
                        LocaleList currentLocales = localeManager == null
                                ? LocaleList.getEmptyLocaleList()
                                : localeManager.getApplicationLocales();
                        int selectedLanguage = currentLocales.isEmpty() ? 0
                                : "ru".equals(currentLocales.get(0).getLanguage()) ? 2 : 1;
                        new AlertDialog.Builder(this)
                                .setTitle(R.string.language_setting)
                                .setSingleChoiceItems(new String[] {
                                        getString(R.string.language_system),
                                        getString(R.string.language_english),
                                        getString(R.string.language_russian)},
                                        selectedLanguage,
                                        (choiceDialog, value) -> {
                                            choiceDialog.dismiss();
                                            if (localeManager != null) {
                                                localeManager.setApplicationLocales(value == 0
                                                        ? LocaleList.getEmptyLocaleList()
                                                        : LocaleList.forLanguageTags(
                                                                value == 2 ? "ru" : "en"));
                                            }
                                        }).show();
                    }
                }).show();
    }

    private void openSettings(String action, String fallbackAction) {
        try {
            startActivity(new Intent(action));
        } catch (ActivityNotFoundException e) {
            if (fallbackAction != null) {
                openSettings(fallbackAction, null);
            } else {
                Toast.makeText(this, R.string.settings_unavailable, Toast.LENGTH_LONG).show();
            }
        }
    }

    private static int jitterPosition(int jitterMs) {
        for (int i = 0; i < JITTER_VALUES_MS.length; i++) {
            if (JITTER_VALUES_MS[i] == jitterMs) {
                return i;
            }
        }
        return 2;
    }

    @SuppressWarnings("deprecation")
    private String getApplicationVersion() {
        try {
            PackageInfo info = getPackageManager().getPackageInfo(getPackageName(), 0);
            return info.versionName == null
                    ? getString(R.string.value_unknown) : info.versionName;
        } catch (PackageManager.NameNotFoundException e) {
            return getString(R.string.value_unknown);
        }
    }
}
