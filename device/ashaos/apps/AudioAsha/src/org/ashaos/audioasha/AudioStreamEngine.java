/*
 * Copyright (C) 2026 The AshaOS Project
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 */

package org.ashaos.audioasha;

import android.content.Context;
import android.media.AudioAttributes;
import android.media.AudioDeviceInfo;
import android.media.AudioFormat;
import android.media.AudioManager;
import android.media.AudioRouting;
import android.media.AudioTrack;
import android.os.Handler;
import android.os.Looper;
import android.os.Process;
import android.os.SystemClock;

import java.io.IOException;
import java.net.DatagramPacket;
import java.net.DatagramSocket;
import java.net.InetAddress;
import java.net.InetSocketAddress;
import java.net.SocketException;
import java.net.SocketTimeoutException;
import java.util.Arrays;
import java.util.Locale;

/**
 * Receives Audio-Asha protocol v1 UDP packets and feeds public AudioTrack APIs.
 *
 * <p>Wire integers are little-endian. The 24-byte header is: magic "ASHA"
 * (uint32), version (uint16), header bytes (uint16), sequence (uint32), sender
 * monotonic timestamp in nanoseconds (uint64), frame count (uint16), channels
 * (uint8), and reserved (uint8). It is followed by 240 stereo PCM16 frames.</p>
 */
final class AudioStreamEngine {
    static final int SAMPLE_RATE = 48_000;
    static final int CHANNELS = 2;
    static final int FRAMES_PER_PACKET = 240;
    static final int PACKET_DURATION_MS = 5;
    static final int PCM_BYTES = FRAMES_PER_PACKET * CHANNELS * 2;

    private static final int MAGIC = 0x41485341;
    private static final int PROTOCOL_VERSION = 1;
    private static final int HEADER_BYTES = 24;
    private static final int DATAGRAM_BYTES = HEADER_BYTES + PCM_BYTES;
    private static final int RECEIVE_BYTES = 1500;

    private final Context mContext;
    private final AudioManager mAudioManager;
    private final Handler mMainHandler = new Handler(Looper.getMainLooper());
    private final Object mLifecycleLock = new Object();
    private final Object mStatsLock = new Object();
    private final SequenceTracker mSequenceTracker = new SequenceTracker();

    private volatile boolean mRunning;
    private volatile String mState = "Stopped";
    private volatile String mLastError = "";
    private volatile DatagramSocket mSocket;
    private volatile AudioTrack mTrack;
    private volatile AudioDeviceInfo mPreferredDevice;
    private volatile AudioDeviceInfo mRoutedDevice;
    private volatile boolean mPreferredDeviceAccepted = true;
    private volatile long mLastPacketElapsedMs;
    private volatile boolean mDirectControlMode;
    private volatile InetAddress mBindAddress;
    private volatile String mBindInterface = "";

    private Thread mReceiverThread;
    private Thread mPlaybackThread;
    private JitterBuffer mJitterBuffer = new JitterBuffer(1);
    private int mPort;
    private int mJitterTargetMs = 5;
    private long mValidPacketCount;
    private long mReceivedPacketCount;
    private long mInvalidMagicCount;
    private long mUnsupportedVersionCount;
    private long mMalformedLengthCount;
    private long mImpossibleFrameCount;
    private long mStalePacketDrops;
    private String mSenderIp = "None";
    private boolean mHaveTransit;
    private long mPreviousTransitNanos;
    private double mEstimatedJitterNanos;

    AudioStreamEngine(Context context) {
        mContext = context.getApplicationContext();
        mAudioManager = mContext.getSystemService(AudioManager.class);
    }

    void start(int port, int jitterTargetMs, AudioDeviceInfo preferredDevice,
            boolean directControlMode, InetAddress bindAddress, String bindInterface) {
        synchronized (mLifecycleLock) {
            if (mRunning) {
                return;
            }
            resetStatistics();
            mPort = port;
            mJitterTargetMs = jitterTargetMs;
            if (preferredDevice != null) {
                mPreferredDevice = preferredDevice;
            }
            mDirectControlMode = directControlMode;
            mBindAddress = bindAddress;
            mBindInterface = bindInterface == null ? "" : bindInterface;
            mPreferredDeviceAccepted = true;
            mJitterBuffer = new JitterBuffer(jitterTargetMs / PACKET_DURATION_MS);
            mRunning = true;
            mState = directControlMode ? "Starting system direct ASHA control" : "Starting";
            mLastError = "";
            if (directControlMode) {
                mReceiverThread = null;
                mPlaybackThread = new Thread(this::playbackLoop, "AudioAshaDirectControl");
                mPlaybackThread.start();
            } else {
                mReceiverThread = new Thread(this::receiverLoop, "AudioAshaUdp");
                mPlaybackThread = null;
                mReceiverThread.start();
            }
        }
    }

    void stop() {
        Thread receiver;
        Thread playback;
        synchronized (mLifecycleLock) {
            if (!mRunning && mReceiverThread == null && mPlaybackThread == null) {
                mState = "Stopped";
                return;
            }
            mRunning = false;
            mState = "Stopping";
            DatagramSocket socket = mSocket;
            if (socket != null) {
                socket.close();
            }
            AudioTrack track = mTrack;
            if (track != null && track.getState() == AudioTrack.STATE_INITIALIZED) {
                try {
                    track.pause();
                    track.flush();
                } catch (IllegalStateException ignored) {
                    // A concurrent playback failure may already have stopped the track.
                }
            }
            receiver = mReceiverThread;
            playback = mPlaybackThread;
        }
        joinBriefly(receiver);
        joinBriefly(playback);
        synchronized (mLifecycleLock) {
            mReceiverThread = null;
            mPlaybackThread = null;
            mSocket = null;
            mTrack = null;
            mRoutedDevice = null;
            mJitterBuffer.clear();
            mState = mLastError.isEmpty() ? "Stopped" : "Error";
        }
    }

    void setPreferredDevice(AudioDeviceInfo device) {
        mPreferredDevice = device;
        AudioTrack track = mTrack;
        if (track != null) {
            try {
                mPreferredDeviceAccepted = track.setPreferredDevice(device);
                updateRoutedDevice(track);
            } catch (IllegalStateException e) {
                mPreferredDeviceAccepted = false;
            }
        }
    }

    boolean isRunning() {
        return mRunning;
    }

    boolean hasRecentAudio() {
        long lastPacket = mLastPacketElapsedMs;
        return mRunning && (mDirectControlMode
                || (lastPacket != 0
                && SystemClock.elapsedRealtime() - lastPacket <= 2000));
    }

    Diagnostics getDiagnostics() {
        long valid;
        long received;
        long invalidMagic;
        long unsupportedVersion;
        long malformedLength;
        long impossibleFrames;
        long staleDrops;
        String sender;
        double jitterMs;
        long lost;
        long duplicates;
        long outOfOrder;
        synchronized (mStatsLock) {
            valid = mValidPacketCount;
            received = mReceivedPacketCount;
            invalidMagic = mInvalidMagicCount;
            unsupportedVersion = mUnsupportedVersionCount;
            malformedLength = mMalformedLengthCount;
            impossibleFrames = mImpossibleFrameCount;
            staleDrops = mStalePacketDrops;
            sender = mSenderIp;
            jitterMs = mEstimatedJitterNanos / 1_000_000.0;
            lost = mSequenceTracker.lost;
            duplicates = mSequenceTracker.duplicates;
            outOfOrder = mSequenceTracker.outOfOrder;
        }

        AudioTrack track = mTrack;
        int bufferBytes = 0;
        int underruns = 0;
        int actualSampleRate = SAMPLE_RATE;
        AudioDeviceInfo routed = mRoutedDevice;
        if (track != null) {
            try {
                bufferBytes = track.getBufferSizeInFrames() * CHANNELS * 2;
                underruns = track.getUnderrunCount();
                actualSampleRate = track.getSampleRate();
                AudioDeviceInfo current = track.getRoutedDevice();
                if (current != null) {
                    routed = current;
                    mRoutedDevice = current;
                }
            } catch (IllegalStateException ignored) {
                // A stop may race this read; retain the last observed route.
            }
        }

        boolean hearingAidAvailable = false;
        if (mAudioManager != null) {
            for (AudioDeviceInfo device :
                    mAudioManager.getDevices(AudioManager.GET_DEVICES_OUTPUTS)) {
                if (device.getType() == AudioDeviceInfo.TYPE_HEARING_AID) {
                    hearingAidAvailable = true;
                    break;
                }
            }
        }
        return new Diagnostics(
                mRunning, mDirectControlMode, mState, mLastError, sender, mPort,
                valid, received, lost,
                duplicates, outOfOrder, invalidMagic, unsupportedVersion,
                malformedLength, impossibleFrames, jitterMs, mJitterTargetMs,
                mJitterBuffer.size(), mJitterBuffer.queuedMilliseconds(), staleDrops,
                bufferBytes, underruns, actualSampleRate, mPreferredDevice,
                mPreferredDeviceAccepted, routed, hearingAidAvailable);
    }

    private void receiverLoop() {
        Process.setThreadPriority(Process.THREAD_PRIORITY_AUDIO);
        byte[] receiveBuffer = new byte[RECEIVE_BYTES];
        DatagramPacket datagram = new DatagramPacket(receiveBuffer, receiveBuffer.length);
        try {
            DatagramSocket socket = new DatagramSocket(null);
            socket.setReuseAddress(true);
            socket.setReceiveBufferSize(64 * 1024);
            socket.setSoTimeout(500);
            InetAddress bindAddress = mBindAddress;
            if (bindAddress == null) {
                throw new SocketException("Selected phone network is unavailable");
            }
            socket.bind(new InetSocketAddress(bindAddress, mPort));
            mSocket = socket;
            if (mRunning) {
                mState = "Listening on " + mBindInterface + "@"
                        + bindAddress.getHostAddress() + ":" + mPort;
            }

            while (mRunning) {
                datagram.setLength(receiveBuffer.length);
                try {
                    socket.receive(datagram);
                } catch (SocketTimeoutException ignored) {
                    continue;
                }
                int length = datagram.getLength();
                int offset = datagram.getOffset();
                if (!validateAndCount(receiveBuffer, offset, length)) {
                    continue;
                }

                int sequence = readLeInt(receiveBuffer, offset + 8);
                long senderTimestamp = readLeLong(receiveBuffer, offset + 12);
                long arrivalTimestamp = SystemClock.elapsedRealtimeNanos();
                boolean duplicate;
                synchronized (mStatsLock) {
                    mValidPacketCount++;
                    duplicate = mSequenceTracker.accept(sequence);
                    if (!duplicate) {
                        mReceivedPacketCount++;
                        updateJitterLocked(senderTimestamp, arrivalTimestamp);
                    }
                    mSenderIp = datagram.getAddress().getHostAddress();
                }
                mLastPacketElapsedMs = SystemClock.elapsedRealtime();
                mState = "Streaming";
                if (duplicate) {
                    continue;
                }
                int stale = mJitterBuffer.offer(receiveBuffer, offset + HEADER_BYTES, sequence);
                if (stale != 0) {
                    synchronized (mStatsLock) {
                        mStalePacketDrops += stale;
                    }
                }
                ensurePlaybackThread();
            }
        } catch (SocketException e) {
            if (mRunning) {
                fail("UDP socket: " + e.getMessage());
            }
        } catch (IOException e) {
            if (mRunning) {
                fail("UDP receive: " + e.getMessage());
            }
        } finally {
            DatagramSocket socket = mSocket;
            if (socket != null) {
                socket.close();
            }
            mSocket = null;
            synchronized (mLifecycleLock) {
                if (Thread.currentThread() == mReceiverThread) {
                    mReceiverThread = null;
                }
            }
        }
    }

    private void ensurePlaybackThread() {
        synchronized (mLifecycleLock) {
            if (!mRunning || (mPlaybackThread != null && mPlaybackThread.isAlive())) {
                return;
            }
            mPlaybackThread = new Thread(this::playbackLoop, "AudioAshaPlayback");
            mPlaybackThread.start();
        }
    }

    private void playbackLoop() {
        Process.setThreadPriority(Process.THREAD_PRIORITY_AUDIO);
        AudioRouting.OnRoutingChangedListener routingListener = routing -> {
            if (routing instanceof AudioTrack) {
                updateRoutedDevice((AudioTrack) routing);
            }
        };
        byte[] silence = new byte[PCM_BYTES];
        try {
            int minimumBuffer = AudioTrack.getMinBufferSize(
                    SAMPLE_RATE, AudioFormat.CHANNEL_OUT_STEREO,
                    AudioFormat.ENCODING_PCM_16BIT);
            if (minimumBuffer <= 0) {
                throw new IllegalStateException("No valid 48 kHz stereo AudioTrack buffer");
            }
            int requestedBuffer = Math.max(minimumBuffer, PCM_BYTES);
            AudioTrack track = new AudioTrack.Builder()
                    .setContext(mContext)
                    .setAudioAttributes(new AudioAttributes.Builder()
                            .setUsage(AudioAttributes.USAGE_MEDIA)
                            .setContentType(AudioAttributes.CONTENT_TYPE_MUSIC)
                            .build())
                    .setAudioFormat(new AudioFormat.Builder()
                            .setEncoding(AudioFormat.ENCODING_PCM_16BIT)
                            .setSampleRate(SAMPLE_RATE)
                            .setChannelMask(AudioFormat.CHANNEL_OUT_STEREO)
                            .build())
                    .setTransferMode(AudioTrack.MODE_STREAM)
                    .setBufferSizeInBytes(requestedBuffer)
                    .setPerformanceMode(AudioTrack.PERFORMANCE_MODE_LOW_LATENCY)
                    .build();
            if (track.getState() != AudioTrack.STATE_INITIALIZED) {
                track.release();
                throw new IllegalStateException("AudioTrack failed to initialize");
            }
            mTrack = track;
            mPreferredDeviceAccepted = track.setPreferredDevice(mPreferredDevice);
            track.addOnRoutingChangedListener(routingListener, mMainHandler);
            track.play();
            updateRoutedDevice(track);
            if (mDirectControlMode) {
                mState = "System direct ASHA control active";
            }

            while (mRunning) {
                PacketSlot slot = mDirectControlMode ? null : mJitterBuffer.poll();
                if (!mDirectControlMode && slot == null && mLastPacketElapsedMs != 0
                        && SystemClock.elapsedRealtime() - mLastPacketElapsedMs > 2000) {
                    mState = "Listening (sender idle)";
                    break;
                }
                byte[] audio = mDirectControlMode || slot == null ? silence : slot.pcm;
                int written = 0;
                while (mRunning && written < PCM_BYTES) {
                    int result = track.write(
                            audio, written, PCM_BYTES - written, AudioTrack.WRITE_BLOCKING);
                    if (result < 0) {
                        throw new IllegalStateException("AudioTrack write failed: " + result);
                    }
                    written += result;
                }
                if (slot != null) {
                    mJitterBuffer.release(slot);
                }
                if (!mDirectControlMode && mRunning && mLastPacketElapsedMs != 0
                        && SystemClock.elapsedRealtime() - mLastPacketElapsedMs > 2000) {
                    mState = "Listening (sender idle)";
                }
            }
            track.removeOnRoutingChangedListener(routingListener);
            try {
                track.stop();
            } catch (IllegalStateException ignored) {
                // Track may have been paused and flushed by stop().
            }
            track.release();
        } catch (RuntimeException e) {
            if (mRunning) {
                fail("Audio playback: " + e.getMessage());
            }
        } finally {
            mTrack = null;
            mRoutedDevice = null;
            synchronized (mLifecycleLock) {
                if (Thread.currentThread() == mPlaybackThread) {
                    mPlaybackThread = null;
                }
            }
        }
    }

    private boolean validateAndCount(byte[] data, int offset, int length) {
        if (length < HEADER_BYTES || readLeInt(data, offset) != MAGIC) {
            synchronized (mStatsLock) {
                mInvalidMagicCount++;
            }
            return false;
        }
        if (readLeUnsignedShort(data, offset + 4) != PROTOCOL_VERSION) {
            synchronized (mStatsLock) {
                mUnsupportedVersionCount++;
            }
            return false;
        }
        int headerBytes = readLeUnsignedShort(data, offset + 6);
        int frames = readLeUnsignedShort(data, offset + 20);
        int channels = data[offset + 22] & 0xff;
        if (frames != FRAMES_PER_PACKET || channels != CHANNELS) {
            synchronized (mStatsLock) {
                mImpossibleFrameCount++;
            }
            return false;
        }
        if (headerBytes != HEADER_BYTES || length != DATAGRAM_BYTES
                || data[offset + 23] != 0) {
            synchronized (mStatsLock) {
                mMalformedLengthCount++;
            }
            return false;
        }
        return true;
    }

    private void updateJitterLocked(long senderTimestamp, long arrivalTimestamp) {
        long transit = arrivalTimestamp - senderTimestamp;
        if (mHaveTransit) {
            long difference = transit - mPreviousTransitNanos;
            double absoluteDifference = Math.abs((double) difference);
            mEstimatedJitterNanos +=
                    (absoluteDifference - mEstimatedJitterNanos) / 16.0;
        } else {
            mHaveTransit = true;
        }
        mPreviousTransitNanos = transit;
    }

    private void updateRoutedDevice(AudioTrack track) {
        try {
            mRoutedDevice = track.getRoutedDevice();
        } catch (IllegalStateException ignored) {
            // Route is only defined while playing.
        }
    }

    private void resetStatistics() {
        synchronized (mStatsLock) {
            mValidPacketCount = 0;
            mReceivedPacketCount = 0;
            mInvalidMagicCount = 0;
            mUnsupportedVersionCount = 0;
            mMalformedLengthCount = 0;
            mImpossibleFrameCount = 0;
            mStalePacketDrops = 0;
            mSenderIp = "None";
            mHaveTransit = false;
            mPreviousTransitNanos = 0;
            mEstimatedJitterNanos = 0;
            mSequenceTracker.reset();
        }
        mLastPacketElapsedMs = 0;
        mRoutedDevice = null;
    }

    private void fail(String error) {
        mLastError = error == null ? "Unknown error" : error;
        mState = "Error";
        mRunning = false;
        DatagramSocket socket = mSocket;
        if (socket != null) {
            socket.close();
        }
    }

    private static void joinBriefly(Thread thread) {
        if (thread == null || thread == Thread.currentThread()) {
            return;
        }
        try {
            thread.join(750);
        } catch (InterruptedException e) {
            Thread.currentThread().interrupt();
        }
    }

    private static int readLeUnsignedShort(byte[] data, int offset) {
        return (data[offset] & 0xff) | ((data[offset + 1] & 0xff) << 8);
    }

    private static int readLeInt(byte[] data, int offset) {
        return (data[offset] & 0xff)
                | ((data[offset + 1] & 0xff) << 8)
                | ((data[offset + 2] & 0xff) << 16)
                | ((data[offset + 3] & 0xff) << 24);
    }

    private static long readLeLong(byte[] data, int offset) {
        return ((long) data[offset] & 0xff)
                | (((long) data[offset + 1] & 0xff) << 8)
                | (((long) data[offset + 2] & 0xff) << 16)
                | (((long) data[offset + 3] & 0xff) << 24)
                | (((long) data[offset + 4] & 0xff) << 32)
                | (((long) data[offset + 5] & 0xff) << 40)
                | (((long) data[offset + 6] & 0xff) << 48)
                | (((long) data[offset + 7] & 0xff) << 56);
    }

    static String describeDevice(AudioDeviceInfo device) {
        if (device == null) {
            return "None";
        }
        CharSequence product = device.getProductName();
        String name = product == null || product.length() == 0 ? "Unknown" : product.toString();
        return name + " - " + getAudioDeviceTypeName(device.getType())
                + " (" + device.getType() + ")";
    }

    private static String getAudioDeviceTypeName(int type) {
        switch (type) {
            case AudioDeviceInfo.TYPE_BUILTIN_EARPIECE: return "TYPE_BUILTIN_EARPIECE";
            case AudioDeviceInfo.TYPE_BUILTIN_SPEAKER: return "TYPE_BUILTIN_SPEAKER";
            case AudioDeviceInfo.TYPE_WIRED_HEADSET: return "TYPE_WIRED_HEADSET";
            case AudioDeviceInfo.TYPE_WIRED_HEADPHONES: return "TYPE_WIRED_HEADPHONES";
            case AudioDeviceInfo.TYPE_BLUETOOTH_SCO: return "TYPE_BLUETOOTH_SCO";
            case AudioDeviceInfo.TYPE_BLUETOOTH_A2DP: return "TYPE_BLUETOOTH_A2DP";
            case AudioDeviceInfo.TYPE_HDMI: return "TYPE_HDMI";
            case AudioDeviceInfo.TYPE_USB_DEVICE: return "TYPE_USB_DEVICE";
            case AudioDeviceInfo.TYPE_USB_ACCESSORY: return "TYPE_USB_ACCESSORY";
            case AudioDeviceInfo.TYPE_DOCK: return "TYPE_DOCK";
            case AudioDeviceInfo.TYPE_AUX_LINE: return "TYPE_AUX_LINE";
            case AudioDeviceInfo.TYPE_USB_HEADSET: return "TYPE_USB_HEADSET";
            case AudioDeviceInfo.TYPE_HEARING_AID: return "TYPE_HEARING_AID";
            case AudioDeviceInfo.TYPE_BUILTIN_SPEAKER_SAFE: return "TYPE_BUILTIN_SPEAKER_SAFE";
            case AudioDeviceInfo.TYPE_REMOTE_SUBMIX: return "TYPE_REMOTE_SUBMIX";
            case AudioDeviceInfo.TYPE_BLE_HEADSET: return "TYPE_BLE_HEADSET";
            case AudioDeviceInfo.TYPE_BLE_SPEAKER: return "TYPE_BLE_SPEAKER";
            case AudioDeviceInfo.TYPE_BLE_BROADCAST: return "TYPE_BLE_BROADCAST";
            case AudioDeviceInfo.TYPE_UNKNOWN:
            default: return "TYPE_UNKNOWN";
        }
    }

    static final class Diagnostics {
        final boolean running;
        private final String state;
        private final boolean directControlMode;
        private final String error;
        private final String senderIp;
        private final int port;
        private final long validPackets;
        private final long receivedPackets;
        private final long lostPackets;
        private final long duplicatePackets;
        private final long outOfOrderPackets;
        private final long invalidMagic;
        private final long unsupportedVersion;
        private final long malformedLength;
        private final long impossibleFrames;
        private final double jitterMs;
        private final int targetJitterMs;
        private final int queuedPackets;
        private final int queuedMs;
        private final long staleDrops;
        private final int audioTrackBufferBytes;
        private final int underruns;
        private final int sampleRate;
        private final AudioDeviceInfo preferred;
        private final boolean preferredAccepted;
        private final AudioDeviceInfo routed;
        private final boolean hearingAidAvailable;

        Diagnostics(boolean running, boolean directControlMode, String state, String error,
                String senderIp, int port,
                long validPackets, long receivedPackets, long lostPackets,
                long duplicatePackets, long outOfOrderPackets, long invalidMagic,
                long unsupportedVersion, long malformedLength, long impossibleFrames,
                double jitterMs, int targetJitterMs, int queuedPackets, int queuedMs,
                long staleDrops, int audioTrackBufferBytes, int underruns, int sampleRate,
                AudioDeviceInfo preferred, boolean preferredAccepted, AudioDeviceInfo routed,
                boolean hearingAidAvailable) {
            this.running = running;
            this.state = state;
            this.error = error;
            this.directControlMode = directControlMode;
            this.senderIp = senderIp;
            this.port = port;
            this.validPackets = validPackets;
            this.receivedPackets = receivedPackets;
            this.lostPackets = lostPackets;
            this.duplicatePackets = duplicatePackets;
            this.outOfOrderPackets = outOfOrderPackets;
            this.invalidMagic = invalidMagic;
            this.unsupportedVersion = unsupportedVersion;
            this.malformedLength = malformedLength;
            this.impossibleFrames = impossibleFrames;
            this.jitterMs = jitterMs;
            this.targetJitterMs = targetJitterMs;
            this.queuedPackets = queuedPackets;
            this.queuedMs = queuedMs;
            this.staleDrops = staleDrops;
            this.audioTrackBufferBytes = audioTrackBufferBytes;
            this.underruns = underruns;
            this.sampleRate = sampleRate;
            this.preferred = preferred;
            this.preferredAccepted = preferredAccepted;
            this.routed = routed;
            this.hearingAidAvailable = hearingAidAvailable;
        }

        String format() {
            boolean routedToHearingAid =
                    routed != null && routed.getType() == AudioDeviceInfo.TYPE_HEARING_AID;
            return String.format(Locale.US,
                    "Input mode: %s\n"
                    + "Streaming state: %s\n"
                    + "Last error: %s\n"
                    + "Sender IP: %s\nUDP port: %d\n"
                    + "Packet count: %d\nUnique received: %d\n"
                    + "Packet loss: %d\nDuplicates: %d\nOut of order: %d\n"
                    + "Invalid magic/version/length/frames: %d / %d / %d / %d\n"
                    + "Estimated jitter: %.3f ms\n"
                    + "Jitter buffer target: %d ms\n"
                    + "Current jitter buffer: %d packets\n"
                    + "Queued audio: %d ms\nLatency-control packets dropped: %d\n"
                    + "AudioTrack buffer size: %d bytes\n"
                    + "AudioTrack underruns: %d\nSample rate: %d Hz\n"
                    + "Preferred output: %s\nPreferred request accepted: %s\n"
                    + "Actual routed device: %s\n"
                    + "Routed product name: %s\nRouted type: %s (%d)\n"
                    + "TYPE_HEARING_AID available: %s\n"
                    + "Actually routed to TYPE_HEARING_AID: %s",
                    directControlMode ? "AshaOS system direct (ADB USB)" : "UDP via AudioTrack",
                    state, error.isEmpty() ? "None" : error, senderIp, port,
                    validPackets, receivedPackets, lostPackets, duplicatePackets,
                    outOfOrderPackets, invalidMagic, unsupportedVersion, malformedLength,
                    impossibleFrames, jitterMs, targetJitterMs, queuedPackets, queuedMs,
                    staleDrops, audioTrackBufferBytes, underruns, sampleRate,
                    preferred == null ? "System default" : describeDevice(preferred),
                    preferredAccepted ? "Yes" : "No",
                    routed == null ? "None (track not routed yet)" : describeDevice(routed),
                    routed == null || routed.getProductName() == null
                            ? "Unknown" : routed.getProductName().toString(),
                    routed == null ? "TYPE_UNKNOWN" : getAudioDeviceTypeName(routed.getType()),
                    routed == null ? AudioDeviceInfo.TYPE_UNKNOWN : routed.getType(),
                    hearingAidAvailable ? "Yes" : "No",
                    routedToHearingAid ? "Yes" : "No");
        }
    }

    /** Fixed-size, allocation-free-after-construction queue and packet pool. */
    private static final class JitterBuffer {
        private static final int POOL_PACKETS = 20;

        private final PacketSlot[] queue = new PacketSlot[POOL_PACKETS];
        private final PacketSlot[] free = new PacketSlot[POOL_PACKETS];
        private final int targetPackets;
        private final int maximumPackets;
        private int count;
        private int freeCount = POOL_PACKETS;
        private boolean primed;

        JitterBuffer(int targetPackets) {
            this.targetPackets = targetPackets;
            // WASAPI commonly delivers two 5 ms protocol packets in one 10 ms
            // capture burst. Keep enough overflow room for that burst plus
            // normal thread scheduling jitter. Priming still follows the user
            // target, so this capacity does not add startup latency by itself.
            maximumPackets = Math.min(
                    POOL_PACKETS, Math.max(8, targetPackets + 6));
            for (int i = 0; i < POOL_PACKETS; i++) {
                free[i] = new PacketSlot();
            }
        }

        synchronized int offer(byte[] source, int offset, int sequence) {
            int staleDrops = 0;
            if (count >= maximumPackets) {
                releaseOldestLocked();
                staleDrops = 1;
            }
            PacketSlot slot = free[--freeCount];
            System.arraycopy(source, offset, slot.pcm, 0, PCM_BYTES);
            slot.sequence = sequence;
            int insertion = count;
            while (insertion > 0
                    && sequence - queue[insertion - 1].sequence < 0) {
                queue[insertion] = queue[insertion - 1];
                insertion--;
            }
            queue[insertion] = slot;
            count++;
            notifyAll();
            return staleDrops;
        }

        synchronized PacketSlot poll() {
            int required = Math.max(1, targetPackets);
            if (!primed) {
                waitForPacketsLocked(required,
                        Math.max(PACKET_DURATION_MS,
                                required * PACKET_DURATION_MS));
                if (count < required) {
                    return null;
                }
                primed = true;
            }
            if (count == 0) {
                // Do not inject a silent packet merely because playback raced
                // the next UDP packet by a fraction of a scheduler period.
                // That old behavior repeatedly drained the queue between
                // 10 ms WASAPI bursts and caused roughly half the real audio
                // to be discarded as stale.
                waitForPacketsLocked(1, PACKET_DURATION_MS);
                if (count == 0) {
                    primed = false;
                    return null;
                }
            }
            PacketSlot result = queue[0];
            if (count > 1) {
                System.arraycopy(queue, 1, queue, 0, count - 1);
            }
            queue[--count] = null;
            return result;
        }

        private void waitForPacketsLocked(int required, int timeoutMs) {
            long deadline = SystemClock.elapsedRealtime() + timeoutMs;
            while (count < required) {
                long remaining = deadline - SystemClock.elapsedRealtime();
                if (remaining <= 0) {
                    return;
                }
                try {
                    wait(remaining);
                } catch (InterruptedException e) {
                    Thread.currentThread().interrupt();
                    return;
                }
            }
        }

        synchronized void release(PacketSlot slot) {
            free[freeCount++] = slot;
        }

        synchronized int size() {
            return count;
        }

        synchronized int queuedMilliseconds() {
            return count * PACKET_DURATION_MS;
        }

        synchronized void clear() {
            while (count > 0) {
                releaseOldestLocked();
            }
            primed = false;
            notifyAll();
        }

        private void releaseOldestLocked() {
            PacketSlot oldest = queue[0];
            if (count > 1) {
                System.arraycopy(queue, 1, queue, 0, count - 1);
            }
            queue[--count] = null;
            free[freeCount++] = oldest;
        }
    }

    private static final class PacketSlot {
        final byte[] pcm = new byte[PCM_BYTES];
        int sequence;
    }

    /** Sequence-window metrics with no per-packet object allocation. */
    private static final class SequenceTracker {
        private static final int HISTORY = 512;
        private final int[] recent = new int[HISTORY];
        private final boolean[] present = new boolean[HISTORY];
        private int nextHistory;
        private boolean initialized;
        private int highest;
        long lost;
        long duplicates;
        long outOfOrder;

        boolean accept(int sequence) {
            for (int i = 0; i < HISTORY; i++) {
                if (present[i] && recent[i] == sequence) {
                    duplicates++;
                    return true;
                }
            }
            recent[nextHistory] = sequence;
            present[nextHistory] = true;
            nextHistory = (nextHistory + 1) % HISTORY;
            if (!initialized) {
                initialized = true;
                highest = sequence;
                return false;
            }
            int delta = sequence - highest;
            if (delta > 0) {
                if (delta > 1) {
                    lost += delta - 1L;
                }
                highest = sequence;
            } else {
                outOfOrder++;
                if (lost > 0) {
                    lost--;
                }
            }
            return false;
        }

        void reset() {
            Arrays.fill(present, false);
            nextHistory = 0;
            initialized = false;
            highest = 0;
            lost = 0;
            duplicates = 0;
            outOfOrder = 0;
        }
    }
}
