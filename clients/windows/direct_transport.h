/*
 * Copyright (C) 2026 The AshaOS Project
 * Licensed under the Apache License, Version 2.0.
 */
#ifndef ASHAOS_DIRECT_TRANSPORT_H
#define ASHAOS_DIRECT_TRANSPORT_H

#include <winsock2.h>
#include <windows.h>

#include <stddef.h>
#include <stdint.h>

#include "protocol.h"

typedef enum aa_adb_network_kind {
    AA_ADB_NETWORK_WIFI,
    AA_ADB_NETWORK_BLUETOOTH_PAN
} aa_adb_network_kind;

typedef struct aa_direct_connection {
    SOCKET socket_handle;
    BOOL datagram;
    WCHAR adb_path[MAX_PATH];
    BOOL forward_active;
    BOOL usb_tether_active;
    BOOL network_adb_active;
    uint8_t session_nonce[AA_DIRECT_NONCE_BYTES];
} aa_direct_connection;

BOOL aa_direct_open(
        aa_direct_connection *connection,
        const WCHAR *target_ip,
        USHORT target_port,
        BOOL use_udp,
        WCHAR *error,
        size_t error_chars);

BOOL aa_direct_send_pcm(
        aa_direct_connection *connection,
        uint32_t sequence,
        const int16_t *pcm,
        WCHAR *error,
        size_t error_chars);

BOOL aa_direct_send_config(
        aa_direct_connection *connection,
        uint16_t buffer_ms,
        BOOL adaptive_enabled,
        WCHAR *error,
        size_t error_chars);

void aa_direct_close(aa_direct_connection *connection);

BOOL aa_adb_connect_network(
        aa_adb_network_kind kind,
        WCHAR *endpoint,
        size_t endpoint_chars,
        WCHAR *error,
        size_t error_chars);

#endif
