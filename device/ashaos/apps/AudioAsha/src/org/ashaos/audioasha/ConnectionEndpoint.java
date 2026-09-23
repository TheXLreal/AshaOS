/*
 * Copyright (C) 2026 The AshaOS Project
 * Licensed under the Apache License, Version 2.0.
 */

package org.ashaos.audioasha;

import java.net.Inet4Address;
import java.net.InetAddress;
import java.net.NetworkInterface;
import java.net.SocketException;
import java.util.Enumeration;

/** Resolves the exact phone interface selected in the Audio-Asha UI. */
final class ConnectionEndpoint {
    static final int MODE_WIFI_UDP = 0;
    static final int MODE_USB_TETHER_UDP = 1;
    static final int MODE_BLUETOOTH_PAN_UDP = 2;
    static final int MODE_DIRECT_USB_TCP = 3;
    static final int MODE_DIRECT_USB_UDP = 4;
    static final int MODE_DIRECT_WIFI_TCP = 5;
    static final int MODE_DIRECT_WIFI_UDP = 6;
    static final int MODE_DIRECT_BLUETOOTH_TCP = 7;
    static final int MODE_DIRECT_BLUETOOTH_UDP = 8;
    static final int MODE_COUNT = 9;
    static final int DIRECT_PORT = 48101;

    final int mode;
    final String interfaceName;
    final Inet4Address address;
    final int port;

    private ConnectionEndpoint(
            int mode, String interfaceName, Inet4Address address, int port) {
        this.mode = mode;
        this.interfaceName = interfaceName;
        this.address = address;
        this.port = port;
    }

    static boolean isValidMode(int mode) {
        return mode >= 0 && mode < MODE_COUNT;
    }

    static boolean isDirect(int mode) {
        return mode >= MODE_DIRECT_USB_TCP && mode < MODE_COUNT;
    }

    static boolean isDirectUdp(int mode) {
        return mode == MODE_DIRECT_USB_UDP
                || mode == MODE_DIRECT_WIFI_UDP
                || mode == MODE_DIRECT_BLUETOOTH_UDP;
    }

    static ConnectionEndpoint resolve(int mode, int udpPort) {
        if (!isValidMode(mode)) {
            return null;
        }
        int port = isDirect(mode) ? DIRECT_PORT : udpPort;
        try {
            Enumeration<NetworkInterface> interfaces =
                    NetworkInterface.getNetworkInterfaces();
            while (interfaces != null && interfaces.hasMoreElements()) {
                NetworkInterface networkInterface = interfaces.nextElement();
                if (!networkInterface.isUp()
                        || !matches(mode, networkInterface.getName())) {
                    continue;
                }
                Enumeration<InetAddress> addresses =
                        networkInterface.getInetAddresses();
                while (addresses.hasMoreElements()) {
                    InetAddress address = addresses.nextElement();
                    if (address instanceof Inet4Address
                            && !address.isAnyLocalAddress()
                            && !address.isLoopbackAddress()
                            && !address.isLinkLocalAddress()) {
                        return new ConnectionEndpoint(
                                mode, networkInterface.getName(),
                                (Inet4Address) address, port);
                    }
                }
            }
        } catch (SocketException ignored) {
            // The UI reports the selected interface as unavailable.
        }
        return null;
    }

    String target() {
        return address.getHostAddress() + ":" + port;
    }

    private static boolean matches(int mode, String name) {
        if (name == null) {
            return false;
        }
        switch (mode) {
            case MODE_WIFI_UDP:
            case MODE_DIRECT_WIFI_TCP:
            case MODE_DIRECT_WIFI_UDP:
                return name.startsWith("wlan");
            case MODE_USB_TETHER_UDP:
            case MODE_DIRECT_USB_TCP:
            case MODE_DIRECT_USB_UDP:
                return name.startsWith("rndis")
                        || name.startsWith("ncm")
                        || name.startsWith("usb");
            case MODE_BLUETOOTH_PAN_UDP:
            case MODE_DIRECT_BLUETOOTH_TCP:
            case MODE_DIRECT_BLUETOOTH_UDP:
                return name.equals("bt-pan") || name.startsWith("bnep");
            default:
                return false;
        }
    }
}
