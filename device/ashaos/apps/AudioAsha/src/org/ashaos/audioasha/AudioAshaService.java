/*
 * Copyright (C) 2026 The AshaOS Project
 * Licensed under the Apache License, Version 2.0.
 */

package org.ashaos.audioasha;

import android.app.Notification;
import android.app.NotificationChannel;
import android.app.NotificationManager;
import android.app.PendingIntent;
import android.app.Service;
import android.content.Intent;
import android.content.SharedPreferences;
import android.content.pm.ServiceInfo;
import android.media.AudioDeviceInfo;
import android.net.ConnectivityManager;
import android.net.Network;
import android.net.NetworkCapabilities;
import android.net.NetworkRequest;
import android.os.Binder;
import android.os.Handler;
import android.os.IBinder;
import android.os.Looper;
import android.os.SystemClock;

/**
 * Android 16 foreground owner for the UDP listener and AudioTrack.
 *
 * <p>The idle listener is special-use because Android 14 explicitly excludes a
 * mediaPlayback FGS from the BOOT_COMPLETED allowlist. Once valid PCM arrives,
 * the service reports both its listener and media-playback roles.</p>
 */
public final class AudioAshaService extends Service {
    static final String ACTION_START_LISTENING =
            "org.ashaos.audioasha.action.START_LISTENING";
    static final String ACTION_STOP = "org.ashaos.audioasha.action.STOP";
    static final String EXTRA_PORT = "port";
    static final String EXTRA_JITTER_MS = "jitter_ms";
    static final String EXTRA_DIRECT_SYSTEM_MODE = "direct_system_mode";
    static final String EXTRA_CONNECTION_MODE = "connection_mode";
    static final String PREFERENCES = "audio_asha";
    static final String PREF_START_AFTER_BOOT = "start_after_boot";
    static final String PREF_SHOULD_LISTEN = "should_listen";
    static final String PREF_PORT = "port";
    static final String PREF_JITTER_MS = "jitter_ms";
    static final String PREF_DIRECT_SYSTEM_MODE = "direct_system_mode";
    static final String PREF_CONNECTION_MODE = "connection_mode";
    static final int DEFAULT_PORT = 48100;
    static final int DEFAULT_JITTER_MS = 5;

    private static final String NOTIFICATION_CHANNEL = "audio_asha_streaming";
    private static final int NOTIFICATION_ID = 41;

    private final LocalBinder mBinder = new LocalBinder();
    private final Handler mHandler = new Handler(Looper.getMainLooper());
    private final Runnable mServiceMonitor = new Runnable() {
        @Override
        public void run() {
            if (!mListening) {
                return;
            }
            if (!mEngine.isRunning() && mConnectionEndpoint != null
                    && SystemClock.elapsedRealtime() >= mNextRestartMs) {
                mEngine.stop();
                mEngine.start(mPort, mJitterMs, null, mDirectSystemMode,
                        mConnectionEndpoint.address, mConnectionEndpoint.interfaceName);
                mNextRestartMs = SystemClock.elapsedRealtime() + 5000;
            }
            boolean audioActive = mEngine.hasRecentAudio();
            int foregroundTypes = ServiceInfo.FOREGROUND_SERVICE_TYPE_SPECIAL_USE;
            if (audioActive) {
                foregroundTypes |= ServiceInfo.FOREGROUND_SERVICE_TYPE_MEDIA_PLAYBACK;
            }
            if (foregroundTypes != mForegroundTypes || audioActive != mLastAudioActive) {
                enterForeground(foregroundTypes, audioActive);
            } else {
                NotificationManager manager = getSystemService(NotificationManager.class);
                if (manager != null) {
                    manager.notify(NOTIFICATION_ID, buildNotification(audioActive));
                }
            }
            mHandler.postDelayed(this, 1000);
        }
    };

    private AudioStreamEngine mEngine;
    private ConnectivityManager mConnectivityManager;
    private ConnectivityManager.NetworkCallback mWifiCallback;
    private boolean mNetworkCallbackRegistered;
    private volatile boolean mWifiAvailable;
    private boolean mListening;
    private boolean mLastAudioActive;
    private int mForegroundTypes;
    private int mPort = DEFAULT_PORT;
    private int mJitterMs = DEFAULT_JITTER_MS;
    private boolean mDirectSystemMode;
    private int mConnectionMode = ConnectionEndpoint.MODE_WIFI_UDP;
    private ConnectionEndpoint mConnectionEndpoint;
    private long mNextRestartMs;

    @Override
    public void onCreate() {
        super.onCreate();
        mEngine = new AudioStreamEngine(this);
        createNotificationChannel();
        registerWifiCallback();
    }

    @Override
    public int onStartCommand(Intent intent, int flags, int startId) {
        if (intent != null && ACTION_STOP.equals(intent.getAction())) {
            stopListening();
            return START_NOT_STICKY;
        }

        SharedPreferences preferences =
                getSharedPreferences(PREFERENCES, MODE_PRIVATE);
        boolean explicitStart =
                intent != null && ACTION_START_LISTENING.equals(intent.getAction());
        boolean restart = intent == null && preferences.getBoolean(PREF_SHOULD_LISTEN, false);
        if (!explicitStart && !restart) {
            stopSelf();
            return START_NOT_STICKY;
        }

        int port = explicitStart
                ? intent.getIntExtra(EXTRA_PORT, DEFAULT_PORT)
                : preferences.getInt(PREF_PORT, DEFAULT_PORT);
        int jitterMs = explicitStart
                ? intent.getIntExtra(EXTRA_JITTER_MS, DEFAULT_JITTER_MS)
                : preferences.getInt(PREF_JITTER_MS, DEFAULT_JITTER_MS);
        int migratedMode = preferences.getBoolean(PREF_DIRECT_SYSTEM_MODE, false)
                ? ConnectionEndpoint.MODE_DIRECT_USB_TCP
                : ConnectionEndpoint.MODE_WIFI_UDP;
        int savedMode = preferences.getInt(PREF_CONNECTION_MODE, migratedMode);
        int connectionMode = explicitStart
                ? intent.getIntExtra(EXTRA_CONNECTION_MODE,
                        intent.getBooleanExtra(EXTRA_DIRECT_SYSTEM_MODE, false)
                                ? ConnectionEndpoint.MODE_DIRECT_USB_TCP : savedMode)
                : savedMode;
        startListening(port, jitterMs, connectionMode);
        return START_STICKY;
    }

    @Override
    public IBinder onBind(Intent intent) {
        return mBinder;
    }

    @Override
    public void onDestroy() {
        mHandler.removeCallbacks(mServiceMonitor);
        mListening = false;
        mEngine.stop();
        if (mNetworkCallbackRegistered && mConnectivityManager != null) {
            try {
                mConnectivityManager.unregisterNetworkCallback(mWifiCallback);
            } catch (IllegalArgumentException ignored) {
                // Callback may already have been removed during process teardown.
            }
        }
        mNetworkCallbackRegistered = false;
        super.onDestroy();
    }

    boolean isListening() {
        return mListening;
    }

    boolean isEngineRunning() {
        return mEngine.isRunning();
    }

    boolean isWifiAvailable() {
        return mWifiAvailable;
    }

    AudioStreamEngine.Diagnostics getDiagnostics() {
        return mEngine.getDiagnostics();
    }

    void setPreferredDevice(AudioDeviceInfo device) {
        mEngine.setPreferredDevice(device);
    }

    void stopListening() {
        mListening = false;
        mNextRestartMs = 0;
        mHandler.removeCallbacks(mServiceMonitor);
        mEngine.stop();
        getSharedPreferences(PREFERENCES, MODE_PRIVATE)
                .edit()
                .putBoolean(PREF_SHOULD_LISTEN, false)
                .apply();
        stopForeground(STOP_FOREGROUND_REMOVE);
        stopSelf();
    }

    private void startListening(int port, int jitterMs, int connectionMode) {
        if (port < 1 || port > 65535) {
            port = DEFAULT_PORT;
        }
        if (!isSupportedJitter(jitterMs)) {
            jitterMs = DEFAULT_JITTER_MS;
        }
        if (!ConnectionEndpoint.isValidMode(connectionMode)) {
            connectionMode = ConnectionEndpoint.MODE_WIFI_UDP;
        }
        boolean directSystemMode = ConnectionEndpoint.isDirect(connectionMode);
        int listeningPort = directSystemMode ? ConnectionEndpoint.DIRECT_PORT : port;
        if (mListening && mEngine.isRunning()
                && mPort == listeningPort && mJitterMs == jitterMs
                && mConnectionMode == connectionMode) {
            return;
        }
        if (mEngine.isRunning()) {
            mEngine.stop();
        }
        mPort = listeningPort;
        mJitterMs = jitterMs;
        mDirectSystemMode = directSystemMode;
        mConnectionMode = connectionMode;
        mConnectionEndpoint = null;

        // Must happen promptly after startForegroundService, before socket/audio setup.
        enterForeground(ServiceInfo.FOREGROUND_SERVICE_TYPE_SPECIAL_USE, false);
        ConnectionEndpoint endpoint = ConnectionEndpoint.resolve(connectionMode, port);
        if (endpoint == null) {
            mListening = false;
            getSharedPreferences(PREFERENCES, MODE_PRIVATE)
                    .edit()
                    .putBoolean(PREF_SHOULD_LISTEN, false)
                    .apply();
            stopForeground(STOP_FOREGROUND_REMOVE);
            stopSelf();
            return;
        }
        mConnectionEndpoint = endpoint;
        mListening = true;
        getSharedPreferences(PREFERENCES, MODE_PRIVATE)
                .edit()
                .putBoolean(PREF_SHOULD_LISTEN, true)
                .putInt(PREF_PORT, port)
                .putInt(PREF_JITTER_MS, jitterMs)
                .putInt(PREF_CONNECTION_MODE, connectionMode)
                .putBoolean(PREF_DIRECT_SYSTEM_MODE, directSystemMode)
                .apply();
        mEngine.start(listeningPort, jitterMs, null, directSystemMode,
                endpoint.address, endpoint.interfaceName);
        mNextRestartMs = SystemClock.elapsedRealtime() + 5000;
        mHandler.removeCallbacks(mServiceMonitor);
        mHandler.post(mServiceMonitor);
    }

    private void enterForeground(int foregroundTypes, boolean audioActive) {
        startForeground(NOTIFICATION_ID, buildNotification(audioActive), foregroundTypes);
        mForegroundTypes = foregroundTypes;
        mLastAudioActive = audioActive;
    }

    private Notification buildNotification(boolean audioActive) {
        Intent stopIntent = new Intent(this, AudioAshaService.class)
                .setAction(ACTION_STOP);
        PendingIntent stopPendingIntent = PendingIntent.getService(
                this, 1, stopIntent,
                PendingIntent.FLAG_UPDATE_CURRENT | PendingIntent.FLAG_IMMUTABLE);
        Intent contentIntent = new Intent(this, MainActivity.class)
                .addFlags(Intent.FLAG_ACTIVITY_SINGLE_TOP);
        PendingIntent contentPendingIntent = PendingIntent.getActivity(
                this, 2, contentIntent,
                PendingIntent.FLAG_UPDATE_CURRENT | PendingIntent.FLAG_IMMUTABLE);
        String status = mDirectSystemMode
                ? getString(R.string.notification_direct_system)
                : getString(audioActive ? R.string.notification_streaming
                        : R.string.notification_listening, mPort);
        if (mConnectionEndpoint != null) {
            status += " - " + mConnectionEndpoint.target();
        }
        if (mConnectionMode == ConnectionEndpoint.MODE_WIFI_UDP && !mWifiAvailable) {
            status += " - Wi-Fi unavailable";
        }
        return new Notification.Builder(this, NOTIFICATION_CHANNEL)
                .setSmallIcon(R.drawable.ic_audio_asha)
                .setContentTitle(getString(R.string.notification_title))
                .setContentText(status)
                .setContentIntent(contentPendingIntent)
                .setCategory(Notification.CATEGORY_SERVICE)
                .setOngoing(true)
                .setOnlyAlertOnce(true)
                .addAction(new Notification.Action.Builder(
                        null, getString(R.string.notification_stop), stopPendingIntent).build())
                .build();
    }

    private void createNotificationChannel() {
        NotificationManager manager = getSystemService(NotificationManager.class);
        if (manager == null) {
            return;
        }
        NotificationChannel channel = new NotificationChannel(
                NOTIFICATION_CHANNEL,
                getString(R.string.notification_channel_name),
                NotificationManager.IMPORTANCE_LOW);
        channel.setDescription(getString(R.string.notification_channel_name));
        manager.createNotificationChannel(channel);
    }

    private void registerWifiCallback() {
        mConnectivityManager = getSystemService(ConnectivityManager.class);
        if (mConnectivityManager == null) {
            return;
        }
        mWifiCallback = new ConnectivityManager.NetworkCallback() {
            @Override
            public void onAvailable(Network network) {
                mWifiAvailable = true;
            }

            @Override
            public void onLost(Network network) {
                mWifiAvailable = false;
            }

            @Override
            public void onCapabilitiesChanged(
                    Network network, NetworkCapabilities capabilities) {
                mWifiAvailable =
                        capabilities.hasTransport(NetworkCapabilities.TRANSPORT_WIFI);
            }
        };
        NetworkRequest request = new NetworkRequest.Builder()
                .addTransportType(NetworkCapabilities.TRANSPORT_WIFI)
                .build();
        try {
            mConnectivityManager.registerNetworkCallback(request, mWifiCallback);
            mNetworkCallbackRegistered = true;
        } catch (RuntimeException ignored) {
            // Listening remains functional; the UDP socket itself handles reconnects.
            mNetworkCallbackRegistered = false;
        }
    }

    private static boolean isSupportedJitter(int jitterMs) {
        return jitterMs == 0 || jitterMs == 5 || jitterMs == 10 || jitterMs == 15
                || jitterMs == 20 || jitterMs == 30 || jitterMs == 50;
    }

    final class LocalBinder extends Binder {
        AudioAshaService getService() {
            return AudioAshaService.this;
        }
    }
}
