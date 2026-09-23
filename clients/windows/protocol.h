/*
 * Copyright (C) 2026 The AshaOS Project
 * Licensed under the Apache License, Version 2.0.
 */
#ifndef ASHAOS_AUDIO_ASHA_PROTOCOL_H
#define ASHAOS_AUDIO_ASHA_PROTOCOL_H

#include <stdint.h>

#define AA_MAGIC 0x41485341u /* Little-endian bytes: A S H A */
#define AA_PROTOCOL_VERSION 1u
#define AA_SAMPLE_RATE 48000u
#define AA_CHANNELS 2u
#define AA_FRAMES_PER_PACKET 240u
#define AA_HEADER_BYTES 24u
#define AA_PCM_BYTES (AA_FRAMES_PER_PACKET * AA_CHANNELS * sizeof(int16_t))
#define AA_DATAGRAM_BYTES (AA_HEADER_BYTES + AA_PCM_BYTES)

#define AA_DIRECT_MAGIC 0x32444141u /* Little-endian bytes: A A D 2 */
#define AA_DIRECT_PROTOCOL_VERSION 2u
#define AA_DIRECT_SAMPLE_RATE 16000u
#define AA_DIRECT_PORT 48101u
#define AA_DIRECT_NONCE_BYTES 16u
#define AA_DIRECT_FRAMES_PER_RECORD 160u
#define AA_DIRECT_HEADER_BYTES 32u
#define AA_DIRECT_PCM_BYTES \
    (AA_DIRECT_FRAMES_PER_RECORD * AA_CHANNELS * sizeof(int16_t))
#define AA_DIRECT_RECORD_BYTES \
    (AA_DIRECT_HEADER_BYTES + AA_DIRECT_PCM_BYTES)
#define AA_DIRECT_RECORD_TYPE_PCM 0u
#define AA_DIRECT_RECORD_TYPE_CONFIG 1u
#define AA_DIRECT_MIN_BUFFER_MS 1u
#define AA_DIRECT_MAX_BUFFER_MS 300u
#define AA_DIRECT_BUFFER_STEP_MS 1u

#pragma pack(push, 1)
typedef struct aa_packet_header {
    uint32_t magic;
    uint16_t protocol_version;
    uint16_t header_bytes;
    uint32_t sequence;
    uint64_t sender_monotonic_timestamp_ns;
    uint16_t frame_count;
    uint8_t channels;
    uint8_t reserved;
} aa_packet_header;

typedef struct aa_direct_packet_header {
    uint32_t magic;
    uint16_t protocol_version;
    uint16_t header_bytes;
    uint32_t sequence;
    uint8_t session_nonce[AA_DIRECT_NONCE_BYTES];
    uint16_t frame_count;
    uint8_t channels;
    uint8_t reserved;
} aa_direct_packet_header;

typedef struct aa_direct_config {
    uint16_t buffer_ms;
    uint8_t adaptive_enabled;
    uint8_t reserved[5];
} aa_direct_config;
#pragma pack(pop)

_Static_assert(sizeof(aa_packet_header) == AA_HEADER_BYTES,
        "Audio-Asha header must be exactly 24 bytes");
_Static_assert(AA_DATAGRAM_BYTES == 984u,
        "Audio-Asha datagram must remain below normal Ethernet MTU");
_Static_assert(sizeof(aa_direct_packet_header) == AA_DIRECT_HEADER_BYTES,
        "AshaOS direct header must be exactly 32 bytes");
_Static_assert(AA_DIRECT_RECORD_BYTES == 672u,
        "AshaOS direct record must be exactly 672 bytes");
_Static_assert(sizeof(aa_direct_config) == 8u,
        "AshaOS direct configuration must be exactly 8 bytes");

#endif
