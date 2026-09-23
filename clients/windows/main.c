/*
 * Copyright (C) 2026 The AshaOS Project
 * Licensed under the Apache License, Version 2.0.
 */

#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#include <avrt.h>
#include <initguid.h>
#include <audioclient.h>
#include <mmdeviceapi.h>
#include <functiondiscoverykeys_devpkey.h>
#include <propidl.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <stdarg.h>
#include <strsafe.h>
#include <math.h>
#include <dwmapi.h>
#include <uxtheme.h>
#include <commctrl.h>
#include <shellapi.h>

#include "protocol.h"
#include "direct_transport.h"
#include "resource.h"

#ifndef __MINGW32__
DEFINE_GUID(IID_IAudioClient,
        0x1cb9ad4c, 0xdbfa, 0x4c32, 0xb1, 0x78, 0xc2, 0xf5, 0x68, 0xa7, 0x03, 0xb2);
DEFINE_GUID(IID_IAudioCaptureClient,
        0xc8adbd64, 0xe71e, 0x48a0, 0xa4, 0xde, 0x18, 0x5c, 0x39, 0x5c, 0xd3, 0x17);
DEFINE_GUID(IID_IAudioClock,
        0xcd63314f, 0x3fba, 0x4a1b, 0x81, 0x2c, 0xef, 0x96, 0x35, 0x87, 0x28, 0xe7);
DEFINE_GUID(IID_IAudioRenderClient,
        0xf294acfc, 0x3146, 0x4483, 0xa7, 0xbf, 0xad, 0xdc, 0xa7, 0xc2, 0x60, 0xe2);
DEFINE_GUID(IID_IMMDeviceEnumerator,
        0xa95664d2, 0x9614, 0x4f35, 0xa7, 0x46, 0xde, 0x8d, 0xb6, 0x36, 0x17, 0xe6);
DEFINE_GUID(CLSID_MMDeviceEnumerator,
        0xbcde0395, 0xe52f, 0x467c, 0x8e, 0x3d, 0xc4, 0x57, 0x92, 0x91, 0x69, 0x2e);
#endif

#define ID_IP 1001
#define ID_PORT 1002
#define ID_START 1003
#define ID_STOP 1004
#define ID_STATUS 1005
#define ID_TIMER 1006
#define ID_TRAY_OPEN 1007
#define ID_DIRECT 1008
#define ID_BUFFER_MS 1009
#define ID_ADAPTIVE 1010
#define ID_USB_TETHER 1011
#define ID_KEEPALIVE 1012
#define ID_WIFI_ADB 1013
#define ID_BLUETOOTH_PAN_ADB 1014
#define ID_PREROLL_MS 1015
#define ID_MIN_DBFS 1016
#define ID_MAX_DBFS 1017
#define ID_DEVELOPERS 1018
#define ID_THEME 1019
#define ID_LANGUAGE 1020
#define ID_HELP_CONNECTION 1021
#define ID_HELP_BUFFER 1022
#define ID_HELP_KEEPALIVE 1023
#define ID_HELP_PREROLL 1024
#define ID_TRAY_EXIT 1025
#define ID_DIRECT_UDP 1026
#define WM_CAPTURE_STOPPED (WM_APP + 1)
#define WM_TRAY_ICON (WM_APP + 2)

#define CAPTURE_EVENT_INDEX 1
#define RING_CAPACITY_FRAMES 4800u
#define STATUS_CHARS 4096u
#define DIRECT_PERIOD_MS 10u
#define DIRECT_MIN_PREROLL_MS 0u
#define DIRECT_MAX_PREROLL_MS 100u
#define DIRECT_PREROLL_STEP_MS 10u
#define DIRECT_MIN_GATE_DBFS (-120)
#define DIRECT_MAX_GATE_DBFS (-20)
#define DIRECT_MIN_OUTPUT_DBFS (-30)
#define DIRECT_MAX_OUTPUT_DBFS 0
#define DIRECT_IDLE_HOLD_US 150000u
#define DIRECT_MAX_CATCHUP_RECORDS 30u
#define DIAGNOSTIC_EVENT_CAPACITY 8192u

#ifndef CREATE_WAITABLE_TIMER_HIGH_RESOLUTION
#define CREATE_WAITABLE_TIMER_HIGH_RESOLUTION 0x00000002
#endif

typedef struct sender_app {
    HWND window;
    HWND ip_edit;
    HWND port_edit;
    HWND start_button;
    HWND stop_button;
    HWND status_label;
    HWND log_label;
    HWND developers_button;
    HWND theme_combo;
    HWND language_combo;
    HWND heading_label;
    HWND subtitle_label;
    HWND target_label;
    HWND port_label;
    HWND appearance_label;
    HWND connection_group;
    HWND advanced_group;
    HWND buffer_label;
    HWND preroll_label;
    HWND gate_label;
    HWND output_label;
    HWND logs_label;
    HWND status_heading;
    HWND setup_hint;
    HWND direct_help_button;
    HWND buffer_help_button;
    HWND keepalive_help_button;
    HWND preroll_help_button;
    HWND tooltip_window;
    HWND active_help_button;
    HWND dev_controls[32];
    UINT dev_control_count;
    BOOL developers_visible;
    BOOL tray_added;
    BOOL exit_requested;
    int preferred_width;
    int preferred_height;
    int ui_theme;
    int ui_language;
    HFONT ui_font;
    HFONT heading_font;
    HFONT section_font;
    HFONT mono_font;
    HBRUSH background_brush;
    HBRUSH surface_brush;
    HBRUSH tooltip_brush;
    HWND direct_checkbox;
    HWND direct_udp_checkbox;
    HWND usb_tether_checkbox;
    HWND wifi_adb_button;
    HWND bluetooth_pan_adb_button;
    HWND keepalive_checkbox;
    HWND buffer_edit;
    HWND preroll_edit;
    HWND min_dbfs_edit;
    HWND max_dbfs_edit;
    HWND adaptive_checkbox;
    HANDLE capture_thread;
    HANDLE stop_event;
    WCHAR target_ip[64];
    USHORT target_port;
    WCHAR adb_network_endpoint[128];
    CRITICAL_SECTION status_lock;
    WCHAR endpoint_name[256];
    WCHAR capture_format[256];
    WCHAR last_error[512];
    volatile LONG running;
    volatile LONG queue_frames;
    volatile LONG64 packets_sent;
    volatile LONG64 bytes_sent;
    volatile LONG64 queue_overflow_frames;
    volatile LONG capture_discontinuities;
    volatile LONG mmcss_active;
    volatile LONG64 direct_max_send_gap_us;
    volatile LONG64 direct_gaps_over_25ms;
    volatile LONG64 direct_gaps_over_50ms;
    volatile LONG64 direct_gaps_over_100ms;
    volatile LONG64 capture_max_event_gap_us;
    volatile LONG64 capture_gaps_over_25ms;
    volatile LONG64 capture_gaps_over_50ms;
    volatile LONG64 capture_gaps_over_100ms;
    volatile LONG64 direct_silence_records;
    volatile LONG64 direct_preroll_silence_records;
    volatile LONG direct_preroll_ready;
    volatile LONG64 direct_catchup_records;
    volatile LONG direct_sender_mmcss_active;
    volatile LONG diagnostic_events;
    volatile LONG diagnostic_events_dropped;
    volatile LONG64 captured_signal_frames;
    volatile LONG64 captured_silent_frames;
    volatile LONG64 last_signal_capture_us;
    volatile LONG64 audio_clock_position;
    volatile LONG64 audio_clock_frequency;
    volatile LONG64 audio_clock_qpc_100ns;
    volatile LONG last_capture_flags;
    volatile LONG audio_clock_active;
    volatile LONG64 keepalive_refills;
    volatile LONG64 keepalive_frames;
    volatile LONG keepalive_thread_active;
    WCHAR diagnostic_path[MAX_PATH];
    LONG64 previous_bytes;
    BOOL direct_enabled;
    BOOL direct_udp_enabled;
    BOOL usb_tether_enabled;
    USHORT direct_buffer_ms;
    USHORT sender_preroll_ms;
    SHORT min_dbfs;
    SHORT max_dbfs;
    LONG idle_gate_abs_sample;
    LONG output_gain_q15;
    BOOL keepalive_enabled;
    BOOL direct_adaptive_enabled;
} sender_app;

static sender_app g_app;
static UINT g_taskbar_created;

typedef struct direct_sender_context {
    aa_direct_connection *connection;
    CRITICAL_SECTION *queue_lock;
    int16_t *ring;
    UINT32 *ring_frames;
} direct_sender_context;

typedef struct wasapi_keepalive_context {
    IAudioClient *audio_client;
    IAudioRenderClient *render_client;
    HANDLE render_event;
    UINT32 buffer_frames;
} wasapi_keepalive_context;

typedef enum diagnostic_event_type {
    DIAGNOSTIC_SESSION_START,
    DIAGNOSTIC_SESSION_STOP,
    DIAGNOSTIC_DIRECT_GAP,
    DIAGNOSTIC_CAPTURE_GAP,
    DIAGNOSTIC_TIMER_CATCHUP,
    DIAGNOSTIC_SOURCE_STARVATION_BEGIN,
    DIAGNOSTIC_SOURCE_STARVATION_END,
    DIAGNOSTIC_SOURCE_PREROLL_READY,
    DIAGNOSTIC_QUEUE_OVERFLOW,
    DIAGNOSTIC_WASAPI_DISCONTINUITY,
    DIAGNOSTIC_SEND_FAILURE,
    DIAGNOSTIC_KEEPALIVE_FAILURE
} diagnostic_event_type;

typedef struct diagnostic_event {
    uint64_t monotonic_us;
    uint32_t sequence;
    LONG queue_frames;
    LONG64 value;
    LONG64 aux_value;
    LONG64 audio_clock_position;
    LONG64 audio_clock_frequency;
    LONG64 audio_clock_qpc_100ns;
    LONG64 last_signal_age_us;
    LONG64 signal_frames;
    LONG64 silent_frames;
    LONG capture_flags;
    LONG keepalive_active;
    LONG64 keepalive_refills;
    diagnostic_event_type type;
} diagnostic_event;

static diagnostic_event g_diagnostic_events[DIAGNOSTIC_EVENT_CAPACITY];

static void set_error(const WCHAR *format, ...)
{
    va_list arguments;
    EnterCriticalSection(&g_app.status_lock);
    va_start(arguments, format);
    _vsnwprintf_s(g_app.last_error, ARRAYSIZE(g_app.last_error),
            _TRUNCATE, format, arguments);
    va_end(arguments);
    LeaveCriticalSection(&g_app.status_lock);
}

static uint64_t monotonic_units(uint64_t units_per_second)
{
    LARGE_INTEGER counter;
    LARGE_INTEGER frequency;
    uint64_t whole_seconds;
    uint64_t remaining_ticks;
    QueryPerformanceCounter(&counter);
    QueryPerformanceFrequency(&frequency);
    whole_seconds = (uint64_t)counter.QuadPart
            / (uint64_t)frequency.QuadPart;
    remaining_ticks = (uint64_t)counter.QuadPart
            % (uint64_t)frequency.QuadPart;
    return whole_seconds * units_per_second
            + remaining_ticks * units_per_second
                    / (uint64_t)frequency.QuadPart;
}

static uint64_t monotonic_nanoseconds(void)
{
    return monotonic_units(1000000000ull);
}

static uint64_t monotonic_microseconds(void)
{
    return monotonic_units(1000000ull);
}

static LONG dbfs_to_abs_sample(SHORT dbfs)
{
    double amplitude = pow(10.0, (double)dbfs / 20.0) * 32767.0;
    LONG sample = (LONG)(amplitude + 0.5);
    if (sample < 1) {
        sample = 1;
    }
    if (sample > 32767) {
        sample = 32767;
    }
    return sample;
}

static LONG dbfs_to_gain_q15(SHORT dbfs)
{
    double gain = pow(10.0, (double)dbfs / 20.0) * 32768.0;
    LONG q15 = (LONG)(gain + 0.5);
    if (q15 < 1) {
        q15 = 1;
    }
    if (q15 > 32768) {
        q15 = 32768;
    }
    return q15;
}

static UINT32 configured_preroll_frames(void)
{
    UINT32 frames = (UINT32)g_app.sender_preroll_ms * AA_DIRECT_SAMPLE_RATE / 1000u;
    return frames < AA_DIRECT_FRAMES_PER_RECORD ? AA_DIRECT_FRAMES_PER_RECORD : frames;
}

static uint64_t clock_units_to_microseconds(
        uint64_t units, uint64_t frequency)
{
    if (frequency == 0u) {
        return 0u;
    }
    return (units / frequency) * 1000000ull
            + (units % frequency) * 1000000ull / frequency;
}

static void update_maximum(volatile LONG64 *maximum, LONG64 value)
{
    LONG64 current = InterlockedCompareExchange64(maximum, 0, 0);
    while (value > current) {
        LONG64 observed = InterlockedCompareExchange64(
                maximum, value, current);
        if (observed == current) {
            break;
        }
        current = observed;
    }
}

static void record_diagnostic_extended(
        diagnostic_event_type type,
        uint32_t sequence,
        LONG queue_frames,
        LONG64 value,
        LONG64 aux_value)
{
    LONG index = InterlockedIncrement(&g_app.diagnostic_events) - 1;
    uint64_t now_us;
    LONG64 last_signal_us;
    diagnostic_event *event;
    if (index < 0 || index >= (LONG)DIAGNOSTIC_EVENT_CAPACITY) {
        InterlockedIncrement(&g_app.diagnostic_events_dropped);
        return;
    }
    now_us = monotonic_microseconds();
    last_signal_us = InterlockedCompareExchange64(
            &g_app.last_signal_capture_us, 0, 0);
    event = &g_diagnostic_events[index];
    event->monotonic_us = now_us;
    event->sequence = sequence;
    event->queue_frames = queue_frames;
    event->value = value;
    event->aux_value = aux_value;
    event->audio_clock_position = InterlockedCompareExchange64(
            &g_app.audio_clock_position, 0, 0);
    event->audio_clock_frequency = InterlockedCompareExchange64(
            &g_app.audio_clock_frequency, 0, 0);
    event->audio_clock_qpc_100ns = InterlockedCompareExchange64(
            &g_app.audio_clock_qpc_100ns, 0, 0);
    event->last_signal_age_us =
            last_signal_us > 0 && (uint64_t)last_signal_us <= now_us
                    ? (LONG64)(now_us - (uint64_t)last_signal_us) : -1;
    event->signal_frames = InterlockedCompareExchange64(
            &g_app.captured_signal_frames, 0, 0);
    event->silent_frames = InterlockedCompareExchange64(
            &g_app.captured_silent_frames, 0, 0);
    event->capture_flags = InterlockedCompareExchange(
            &g_app.last_capture_flags, 0, 0);
    event->keepalive_active = InterlockedCompareExchange(
            &g_app.keepalive_thread_active, 0, 0);
    event->keepalive_refills = InterlockedCompareExchange64(
            &g_app.keepalive_refills, 0, 0);
    event->type = type;
}

static void record_diagnostic(
        diagnostic_event_type type,
        uint32_t sequence,
        LONG queue_frames,
        LONG64 value)
{
    record_diagnostic_extended(type, sequence, queue_frames, value, 0);
}

static const char *diagnostic_event_name(diagnostic_event_type type)
{
    switch (type) {
        case DIAGNOSTIC_SESSION_START:
            return "session_start";
        case DIAGNOSTIC_SESSION_STOP:
            return "session_stop";
        case DIAGNOSTIC_DIRECT_GAP:
            return "direct_send_gap";
        case DIAGNOSTIC_CAPTURE_GAP:
            return "wasapi_event_gap";
        case DIAGNOSTIC_TIMER_CATCHUP:
            return "direct_timer_catchup";
        case DIAGNOSTIC_SOURCE_STARVATION_BEGIN:
            return "source_pause_begin";
        case DIAGNOSTIC_SOURCE_STARVATION_END:
            return "source_pause_end";
        case DIAGNOSTIC_SOURCE_PREROLL_READY:
            return "source_preroll_ready";
        case DIAGNOSTIC_QUEUE_OVERFLOW:
            return "queue_overflow";
        case DIAGNOSTIC_WASAPI_DISCONTINUITY:
            return "wasapi_discontinuity";
        case DIAGNOSTIC_SEND_FAILURE:
            return "direct_send_failure";
        case DIAGNOSTIC_KEEPALIVE_FAILURE:
            return "wasapi_keepalive_failure";
        default:
            return "unknown";
    }
}

static BOOL write_bytes(HANDLE file, const char *bytes, DWORD byte_count)
{
    DWORD written = 0;
    return WriteFile(file, bytes, byte_count, &written, NULL)
            && written == byte_count;
}

static void write_diagnostic_report(void)
{
    WCHAR path[MAX_PATH];
    WCHAR *file_name;
    HANDLE file;
    LONG count;
    LONG index;
    char row[640];
    const char *header =
            "# Audio-Asha Sender v1.7 anomaly log\r\n"
            "# value: gap_us, catchup_due_records, starvation_record_count, "
            "preroll_frames, overflow_frames, or Windows/socket error\r\n"
            "# audio_clock_advance_us is populated for wasapi_event_gap; "
            "-1 means the endpoint clock sample was unavailable\r\n"
            "event_index,monotonic_us,event,sequence,queue_frames,value,"
            "audio_clock_advance_us,audio_clock_position,audio_clock_frequency,"
            "audio_clock_qpc_100ns,last_signal_age_us,signal_frames,"
            "silent_frames,capture_flags,keepalive_active,"
            "keepalive_refills\r\n";

    if (GetModuleFileNameW(NULL, path, ARRAYSIZE(path)) == 0) {
        return;
    }
    file_name = wcsrchr(path, L'\\');
    if (file_name == NULL) {
        file_name = path;
    } else {
        file_name++;
    }
    if (FAILED(StringCchCopyW(file_name,
            ARRAYSIZE(path) - (size_t)(file_name - path),
            L"AudioAshaSender-v1.7-last.csv"))) {
        return;
    }
    file = CreateFileW(path, GENERIC_WRITE, FILE_SHARE_READ, NULL,
            CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if (file == INVALID_HANDLE_VALUE) {
        return;
    }
    if (!write_bytes(file, header, (DWORD)strlen(header))) {
        CloseHandle(file);
        return;
    }

    count = InterlockedCompareExchange(&g_app.diagnostic_events, 0, 0);
    if (count > (LONG)DIAGNOSTIC_EVENT_CAPACITY) {
        count = (LONG)DIAGNOSTIC_EVENT_CAPACITY;
    }
    for (index = 0; index < count; index++) {
        diagnostic_event *event = &g_diagnostic_events[index];
        int row_bytes = _snprintf_s(row, ARRAYSIZE(row), _TRUNCATE,
                "%ld,%llu,%s,%lu,%ld,%lld,%lld,%lld,%lld,%lld,%lld,"
                "%lld,%lld,%lu,%ld,%lld\r\n",
                index,
                (unsigned long long)event->monotonic_us,
                diagnostic_event_name(event->type),
                (unsigned long)event->sequence,
                event->queue_frames,
                (long long)event->value,
                (long long)event->aux_value,
                (long long)event->audio_clock_position,
                (long long)event->audio_clock_frequency,
                (long long)event->audio_clock_qpc_100ns,
                (long long)event->last_signal_age_us,
                (long long)event->signal_frames,
                (long long)event->silent_frames,
                (unsigned long)event->capture_flags,
                event->keepalive_active,
                (long long)event->keepalive_refills);
        if (row_bytes <= 0
                || !write_bytes(file, row, (DWORD)row_bytes)) {
            break;
        }
    }
    CloseHandle(file);
    EnterCriticalSection(&g_app.status_lock);
    StringCchCopyW(g_app.diagnostic_path,
            ARRAYSIZE(g_app.diagnostic_path), path);
    LeaveCriticalSection(&g_app.status_lock);
}

static void load_settings(void)
{
    HKEY key;
    DWORD type;
    DWORD bytes;
    DWORD port = 48100u;
    DWORD direct = 1u;
    DWORD direct_udp = 0u;
    DWORD usb_tether = 0u;
    DWORD keepalive = 1u;
    DWORD direct_buffer_ms = 20u;
    DWORD sender_preroll_ms = 40u;
    DWORD direct_adaptive = 0u;
    DWORD ui_theme = 0u;
    DWORD ui_language =
            PRIMARYLANGID(GetUserDefaultUILanguage()) == LANG_RUSSIAN ? 1u : 0u;
    LONG min_dbfs = -80;
    LONG max_dbfs = 0;

    StringCchCopyW(g_app.target_ip, ARRAYSIZE(g_app.target_ip), L"192.168.1.2");
    g_app.target_port = (USHORT)port;
    g_app.direct_enabled = TRUE;
    g_app.direct_udp_enabled = FALSE;
    g_app.usb_tether_enabled = FALSE;
    g_app.keepalive_enabled = TRUE;
    g_app.direct_buffer_ms = (USHORT)direct_buffer_ms;
    g_app.sender_preroll_ms = (USHORT)sender_preroll_ms;
    g_app.direct_adaptive_enabled = FALSE;
    g_app.min_dbfs = (SHORT)min_dbfs;
    g_app.max_dbfs = (SHORT)max_dbfs;
    g_app.idle_gate_abs_sample = dbfs_to_abs_sample(g_app.min_dbfs);
    g_app.output_gain_q15 = dbfs_to_gain_q15(g_app.max_dbfs);
    g_app.ui_theme = (int)ui_theme;
    g_app.ui_language = (int)ui_language;
    if (RegOpenKeyExW(HKEY_CURRENT_USER,
            L"Software\\AshaOS\\AudioAshaSender", 0, KEY_READ, &key) != ERROR_SUCCESS) {
        return;
    }
    bytes = sizeof(g_app.target_ip);
    type = 0;
    RegQueryValueExW(key, L"TargetIp", NULL, &type,
            (BYTE *)g_app.target_ip, &bytes);
    bytes = sizeof(port);
    type = 0;
    if (RegQueryValueExW(key, L"TargetPort", NULL, &type,
            (BYTE *)&port, &bytes) == ERROR_SUCCESS
            && type == REG_DWORD && port > 0u && port <= 65535u) {
        g_app.target_port = (USHORT)port;
    }
    bytes = sizeof(direct);
    type = 0;
    if (RegQueryValueExW(key, L"DirectEnabled", NULL, &type,
            (BYTE *)&direct, &bytes) == ERROR_SUCCESS
            && type == REG_DWORD) {
        g_app.direct_enabled = direct != 0u;
    }
    bytes = sizeof(direct_udp);
    type = 0;
    if (RegQueryValueExW(key, L"DirectUdpEnabled", NULL, &type,
            (BYTE *)&direct_udp, &bytes) == ERROR_SUCCESS
            && type == REG_DWORD) {
        g_app.direct_udp_enabled = direct_udp != 0u;
    }
    bytes = sizeof(usb_tether);
    type = 0;
    if (RegQueryValueExW(key, L"UsbTetherEnabled", NULL, &type,
            (BYTE *)&usb_tether, &bytes) == ERROR_SUCCESS
            && type == REG_DWORD) {
        g_app.usb_tether_enabled = usb_tether != 0u;
    }
    bytes = sizeof(keepalive);
    type = 0;
    if (RegQueryValueExW(key, L"KeepaliveEnabled", NULL, &type,
            (BYTE *)&keepalive, &bytes) == ERROR_SUCCESS
            && type == REG_DWORD) {
        g_app.keepalive_enabled = keepalive != 0u;
    }
    bytes = sizeof(direct_buffer_ms);
    type = 0;
    if (RegQueryValueExW(key, L"DirectBufferMs", NULL, &type,
            (BYTE *)&direct_buffer_ms, &bytes) == ERROR_SUCCESS
            && type == REG_DWORD
            && direct_buffer_ms >= AA_DIRECT_MIN_BUFFER_MS
            && direct_buffer_ms <= AA_DIRECT_MAX_BUFFER_MS
            && direct_buffer_ms % AA_DIRECT_BUFFER_STEP_MS == 0u) {
        g_app.direct_buffer_ms = (USHORT)direct_buffer_ms;
    }
    bytes = sizeof(sender_preroll_ms);
    type = 0;
    if (RegQueryValueExW(key, L"SenderPrerollMs", NULL, &type,
            (BYTE *)&sender_preroll_ms, &bytes) == ERROR_SUCCESS
            && type == REG_DWORD
            && sender_preroll_ms <= DIRECT_MAX_PREROLL_MS
            && sender_preroll_ms % DIRECT_PREROLL_STEP_MS == 0u) {
        g_app.sender_preroll_ms = (USHORT)sender_preroll_ms;
    }
    bytes = sizeof(min_dbfs);
    type = 0;
    if (RegQueryValueExW(key, L"MinDbfs", NULL, &type,
            (BYTE *)&min_dbfs, &bytes) == ERROR_SUCCESS
            && type == REG_DWORD
            && min_dbfs >= DIRECT_MIN_GATE_DBFS
            && min_dbfs <= DIRECT_MAX_GATE_DBFS) {
        g_app.min_dbfs = (SHORT)min_dbfs;
    }
    bytes = sizeof(max_dbfs);
    type = 0;
    if (RegQueryValueExW(key, L"MaxDbfs", NULL, &type,
            (BYTE *)&max_dbfs, &bytes) == ERROR_SUCCESS
            && type == REG_DWORD
            && max_dbfs >= DIRECT_MIN_OUTPUT_DBFS
            && max_dbfs <= DIRECT_MAX_OUTPUT_DBFS) {
        g_app.max_dbfs = (SHORT)max_dbfs;
    }
    bytes = sizeof(direct_adaptive);
    type = 0;
    if (RegQueryValueExW(key, L"DirectAdaptive", NULL, &type,
            (BYTE *)&direct_adaptive, &bytes) == ERROR_SUCCESS
            && type == REG_DWORD) {
        g_app.direct_adaptive_enabled = direct_adaptive != 0u;
    }
    bytes = sizeof(ui_theme);
    type = 0;
    if (RegQueryValueExW(key, L"UiTheme", NULL, &type,
            (BYTE *)&ui_theme, &bytes) == ERROR_SUCCESS
            && type == REG_DWORD && ui_theme <= 2u) {
        g_app.ui_theme = (int)ui_theme;
    }
    bytes = sizeof(ui_language);
    type = 0;
    if (RegQueryValueExW(key, L"UiLanguage", NULL, &type,
            (BYTE *)&ui_language, &bytes) == ERROR_SUCCESS
            && type == REG_DWORD && ui_language <= 1u) {
        g_app.ui_language = (int)ui_language;
    }
    // The old USB-tether checkbox was not actually shown in the UI.
    // Direct mode already uses the manually entered phone target.
    g_app.usb_tether_enabled = FALSE;
    g_app.idle_gate_abs_sample = dbfs_to_abs_sample(g_app.min_dbfs);
    g_app.output_gain_q15 = dbfs_to_gain_q15(g_app.max_dbfs);
    RegCloseKey(key);
}

static void save_settings(void)
{
    HKEY key;
    DWORD port = g_app.target_port;
    DWORD direct = g_app.direct_enabled ? 1u : 0u;
    DWORD direct_udp = g_app.direct_udp_enabled ? 1u : 0u;
    DWORD direct_buffer_ms = g_app.direct_buffer_ms;
    DWORD sender_preroll_ms = g_app.sender_preroll_ms;
    DWORD usb_tether = g_app.usb_tether_enabled ? 1u : 0u;
    DWORD keepalive = g_app.keepalive_enabled ? 1u : 0u;
    DWORD direct_adaptive = g_app.direct_adaptive_enabled ? 1u : 0u;
    DWORD min_dbfs = (DWORD)(LONG)g_app.min_dbfs;
    DWORD max_dbfs = (DWORD)(LONG)g_app.max_dbfs;
    DWORD ui_theme = (DWORD)g_app.ui_theme;
    DWORD ui_language = (DWORD)g_app.ui_language;
    if (RegCreateKeyExW(HKEY_CURRENT_USER,
            L"Software\\AshaOS\\AudioAshaSender", 0, NULL, 0,
            KEY_WRITE, NULL, &key, NULL) != ERROR_SUCCESS) {
        return;
    }
    RegSetValueExW(key, L"TargetIp", 0, REG_SZ,
            (const BYTE *)g_app.target_ip,
            (DWORD)((wcslen(g_app.target_ip) + 1u) * sizeof(WCHAR)));
    RegSetValueExW(key, L"TargetPort", 0, REG_DWORD,
            (const BYTE *)&port, sizeof(port));
    RegSetValueExW(key, L"DirectEnabled", 0, REG_DWORD,
            (const BYTE *)&direct, sizeof(direct));
    RegSetValueExW(key, L"DirectUdpEnabled", 0, REG_DWORD,
            (const BYTE *)&direct_udp, sizeof(direct_udp));
    RegSetValueExW(key, L"UsbTetherEnabled", 0, REG_DWORD,
            (const BYTE *)&usb_tether, sizeof(usb_tether));
    RegSetValueExW(key, L"KeepaliveEnabled", 0, REG_DWORD,
            (const BYTE *)&keepalive, sizeof(keepalive));
    RegSetValueExW(key, L"DirectBufferMs", 0, REG_DWORD,
            (const BYTE *)&direct_buffer_ms, sizeof(direct_buffer_ms));
    RegSetValueExW(key, L"SenderPrerollMs", 0, REG_DWORD,
            (const BYTE *)&sender_preroll_ms, sizeof(sender_preroll_ms));
    RegSetValueExW(key, L"DirectAdaptive", 0, REG_DWORD,
            (const BYTE *)&direct_adaptive, sizeof(direct_adaptive));
    RegSetValueExW(key, L"MinDbfs", 0, REG_DWORD,
            (const BYTE *)&min_dbfs, sizeof(min_dbfs));
    RegSetValueExW(key, L"MaxDbfs", 0, REG_DWORD,
            (const BYTE *)&max_dbfs, sizeof(max_dbfs));
    RegSetValueExW(key, L"UiTheme", 0, REG_DWORD,
            (const BYTE *)&ui_theme, sizeof(ui_theme));
    RegSetValueExW(key, L"UiLanguage", 0, REG_DWORD,
            (const BYTE *)&ui_language, sizeof(ui_language));
    RegCloseKey(key);
}

static void set_endpoint_name(IMMDevice *device)
{
    IPropertyStore *properties = NULL;
    PROPVARIANT value;
    PropVariantInit(&value);
    EnterCriticalSection(&g_app.status_lock);
    StringCchCopyW(g_app.endpoint_name, ARRAYSIZE(g_app.endpoint_name), L"Unknown");
    LeaveCriticalSection(&g_app.status_lock);

    if (SUCCEEDED(IMMDevice_OpenPropertyStore(device, STGM_READ, &properties))
            && SUCCEEDED(IPropertyStore_GetValue(
                    properties, &PKEY_Device_FriendlyName, &value))
            && value.vt == VT_LPWSTR && value.pwszVal != NULL) {
        EnterCriticalSection(&g_app.status_lock);
        StringCchCopyW(g_app.endpoint_name, ARRAYSIZE(g_app.endpoint_name),
                value.pwszVal);
        LeaveCriticalSection(&g_app.status_lock);
    }
    PropVariantClear(&value);
    if (properties != NULL) {
        IPropertyStore_Release(properties);
    }
}

static BOOL capture_packet_has_signal(
        const BYTE *source, UINT32 frames, DWORD capture_flags)
{
    const int16_t *samples;
    size_t sample_count;
    size_t index;

    if (source == NULL
            || (capture_flags & AUDCLNT_BUFFERFLAGS_SILENT) != 0u) {
        return FALSE;
    }
    samples = (const int16_t *)source;
    sample_count = (size_t)frames * AA_CHANNELS;
    for (index = 0; index < sample_count; index++) {
        if (samples[index] > g_app.idle_gate_abs_sample
                || samples[index] < -g_app.idle_gate_abs_sample) {
            return TRUE;
        }
    }
    return FALSE;
}

static BOOL update_capture_activity(
        const BYTE *source, UINT32 frames, DWORD capture_flags)
{
    BOOL has_signal =
            capture_packet_has_signal(source, frames, capture_flags);
    InterlockedExchange(&g_app.last_capture_flags, (LONG)capture_flags);
    if (has_signal) {
        InterlockedAdd64(&g_app.captured_signal_frames, (LONG64)frames);
        InterlockedExchange64(
                &g_app.last_signal_capture_us,
                (LONG64)monotonic_microseconds());
    } else {
        InterlockedAdd64(&g_app.captured_silent_frames, (LONG64)frames);
    }
    return has_signal;
}

static UINT32 append_capture_frames(
        int16_t *ring,
        UINT32 *ring_frames,
        const BYTE *source,
        UINT32 source_frames,
        DWORD capture_flags)
{
    UINT32 dropped = 0;
    UINT32 keep_frames = source_frames;
    const BYTE *keep_source = source;

    if (keep_frames > RING_CAPACITY_FRAMES) {
        dropped += keep_frames - RING_CAPACITY_FRAMES;
        if ((capture_flags & AUDCLNT_BUFFERFLAGS_SILENT) == 0u) {
            keep_source += (keep_frames - RING_CAPACITY_FRAMES)
                    * AA_CHANNELS * sizeof(int16_t);
        }
        keep_frames = RING_CAPACITY_FRAMES;
    }
    if (*ring_frames + keep_frames > RING_CAPACITY_FRAMES) {
        UINT32 discard = *ring_frames + keep_frames - RING_CAPACITY_FRAMES;
        memmove(ring, ring + discard * AA_CHANNELS,
                (*ring_frames - discard) * AA_CHANNELS * sizeof(int16_t));
        *ring_frames -= discard;
        dropped += discard;
    }

    if ((capture_flags & AUDCLNT_BUFFERFLAGS_SILENT) != 0u) {
        ZeroMemory(ring + *ring_frames * AA_CHANNELS,
                keep_frames * AA_CHANNELS * sizeof(int16_t));
    } else {
        int16_t *destination = ring + *ring_frames * AA_CHANNELS;
        const int16_t *samples = (const int16_t *)keep_source;
        size_t sample_count = keep_frames * AA_CHANNELS;
        LONG gain_q15 = g_app.output_gain_q15;
        if (gain_q15 != 32768) {
            size_t i;
            for (i = 0; i < sample_count; i++) {
                LONG64 scaled = (LONG64)samples[i] * gain_q15;
                destination[i] = (int16_t)(scaled / 32768);
            }
        } else {
            memcpy(destination, samples, sample_count * sizeof(int16_t));
        }
    }
    *ring_frames += keep_frames;
    return dropped;
}

static BOOL send_ready_packets(
        SOCKET socket_handle,
        const struct sockaddr_in *destination,
        int16_t *ring,
        UINT32 *ring_frames,
        uint32_t *sequence)
{
    BYTE datagram[AA_DATAGRAM_BYTES];
    aa_packet_header header;
    int sent;

    while (*ring_frames >= AA_FRAMES_PER_PACKET) {
        header.magic = AA_MAGIC;
        header.protocol_version = AA_PROTOCOL_VERSION;
        header.header_bytes = AA_HEADER_BYTES;
        header.sequence = (*sequence)++;
        header.sender_monotonic_timestamp_ns = monotonic_nanoseconds();
        header.frame_count = AA_FRAMES_PER_PACKET;
        header.channels = AA_CHANNELS;
        header.reserved = 0;
        memcpy(datagram, &header, sizeof(header));
        memcpy(datagram + AA_HEADER_BYTES, ring, AA_PCM_BYTES);

        sent = sendto(socket_handle, (const char *)datagram,
                (int)sizeof(datagram), 0,
                (const struct sockaddr *)destination, sizeof(*destination));
        if (sent == SOCKET_ERROR) {
            set_error(L"UDP send failed: Winsock error %d", WSAGetLastError());
            return FALSE;
        }
        InterlockedIncrement64(&g_app.packets_sent);
        InterlockedAdd64(&g_app.bytes_sent, sent);
        *ring_frames -= AA_FRAMES_PER_PACKET;
        if (*ring_frames != 0u) {
            memmove(ring, ring + AA_FRAMES_PER_PACKET * AA_CHANNELS,
                    *ring_frames * AA_CHANNELS * sizeof(int16_t));
        }
        InterlockedExchange(&g_app.queue_frames, (LONG)*ring_frames);
    }
    return TRUE;
}
static BOOL arm_direct_timer(HANDLE timer, uint64_t deadline_us)
{
    LARGE_INTEGER due_time;
    uint64_t now_us = monotonic_microseconds();
    uint64_t delay_us = deadline_us > now_us
            ? deadline_us - now_us : 1u;
    if (delay_us > (uint64_t)INT64_MAX / 10u) {
        delay_us = (uint64_t)INT64_MAX / 10u;
    }
    due_time.QuadPart = -(LONGLONG)(delay_us * 10u);
    return SetWaitableTimer(timer, &due_time, 0, NULL, NULL, FALSE);
}

static DWORD WINAPI wasapi_keepalive_thread_main(LPVOID parameter)
{
    wasapi_keepalive_context *context =
            (wasapi_keepalive_context *)parameter;
    HANDLE wait_handles[2];
    HANDLE mmcss_handle = NULL;
    DWORD mmcss_task_index = 0;
    HRESULT result = S_OK;
    HRESULT com_result;
    BOOL com_started = FALSE;
    BOOL failed = FALSE;

    com_result = CoInitializeEx(NULL, COINIT_MULTITHREADED);
    if (FAILED(com_result)) {
        result = com_result;
        failed = TRUE;
        goto cleanup;
    }
    com_started = TRUE;
    mmcss_handle = AvSetMmThreadCharacteristicsW(
            L"Pro Audio", &mmcss_task_index);
    if (mmcss_handle == NULL) {
        mmcss_handle = AvSetMmThreadCharacteristicsW(
                L"Audio", &mmcss_task_index);
    }
    if (mmcss_handle != NULL) {
        AvSetMmThreadPriority(mmcss_handle, AVRT_PRIORITY_NORMAL);
    }

    InterlockedExchange(&g_app.keepalive_thread_active, 1);
    wait_handles[0] = g_app.stop_event;
    wait_handles[1] = context->render_event;
    for (;;) {
        DWORD wait_result =
                WaitForMultipleObjects(2, wait_handles, FALSE, INFINITE);
        UINT32 padding = 0;
        UINT32 available_frames;
        BYTE *render_data = NULL;

        if (wait_result == WAIT_OBJECT_0) {
            break;
        }
        if (wait_result != WAIT_OBJECT_0 + 1) {
            result = HRESULT_FROM_WIN32(GetLastError());
            failed = TRUE;
            break;
        }
        result = IAudioClient_GetCurrentPadding(
                context->audio_client, &padding);
        if (FAILED(result) || padding > context->buffer_frames) {
            if (SUCCEEDED(result)) {
                result = E_UNEXPECTED;
            }
            failed = TRUE;
            break;
        }
        available_frames = context->buffer_frames - padding;
        if (available_frames == 0u) {
            continue;
        }
        result = IAudioRenderClient_GetBuffer(
                context->render_client, available_frames, &render_data);
        if (FAILED(result)) {
            failed = TRUE;
            break;
        }
        result = IAudioRenderClient_ReleaseBuffer(
                context->render_client, available_frames,
                AUDCLNT_BUFFERFLAGS_SILENT);
        if (FAILED(result)) {
            failed = TRUE;
            break;
        }
        InterlockedIncrement64(&g_app.keepalive_refills);
        InterlockedAdd64(&g_app.keepalive_frames, available_frames);
    }

cleanup:
    if (failed) {
        set_error(L"WASAPI keep-alive failed: 0x%08lx",
                (unsigned long)result);
        record_diagnostic(DIAGNOSTIC_KEEPALIVE_FAILURE, 0,
                InterlockedCompareExchange(&g_app.queue_frames, 0, 0),
                (LONG64)(LONG)result);
        SetEvent(g_app.stop_event);
    }
    InterlockedExchange(&g_app.keepalive_thread_active, 0);
    if (mmcss_handle != NULL) {
        AvRevertMmThreadCharacteristics(mmcss_handle);
    }
    if (com_started) {
        CoUninitialize();
    }
    return failed ? 1u : 0u;
}

static DWORD WINAPI direct_sender_thread_main(LPVOID parameter)
{
    direct_sender_context *context = (direct_sender_context *)parameter;
    HANDLE timer = NULL;
    HANDLE mmcss_handle = NULL;
    DWORD mmcss_task_index = 0;
    HANDLE wait_handles[2];
    int16_t record[AA_DIRECT_FRAMES_PER_RECORD * AA_CHANNELS];
    uint32_t sequence = 0;
    uint64_t last_send_us = 0;
    uint64_t next_deadline_us;
    LONG64 starvation_records = 0;
    BOOL source_preroll_ready = FALSE;
    WCHAR error[512];
    UINT32 required_preroll_frames = configured_preroll_frames();

    timer = CreateWaitableTimerExW(NULL, NULL,
            CREATE_WAITABLE_TIMER_HIGH_RESOLUTION, TIMER_ALL_ACCESS);
    if (timer == NULL) {
        timer = CreateWaitableTimerW(NULL, FALSE, NULL);
    }
    if (timer == NULL) {
        DWORD timer_error = GetLastError();
        set_error(L"Cannot create the 10 ms direct timer: Windows error %lu",
                timer_error);
        record_diagnostic(DIAGNOSTIC_SEND_FAILURE, sequence, 0, timer_error);
        SetEvent(g_app.stop_event);
        return 1;
    }

    next_deadline_us = monotonic_microseconds()
            + (uint64_t)DIRECT_PERIOD_MS * 1000u;
    if (!arm_direct_timer(timer, next_deadline_us)) {
        DWORD timer_error = GetLastError();
        set_error(L"Cannot start the 10 ms direct timer: Windows error %lu",
                timer_error);
        record_diagnostic(DIAGNOSTIC_SEND_FAILURE, sequence, 0, timer_error);
        CloseHandle(timer);
        SetEvent(g_app.stop_event);
        return 1;
    }

    mmcss_handle = AvSetMmThreadCharacteristicsW(
            L"Pro Audio", &mmcss_task_index);
    if (mmcss_handle == NULL) {
        mmcss_handle = AvSetMmThreadCharacteristicsW(
                L"Audio", &mmcss_task_index);
    }
    if (mmcss_handle != NULL) {
        AvSetMmThreadPriority(mmcss_handle, AVRT_PRIORITY_HIGH);
        InterlockedExchange(&g_app.direct_sender_mmcss_active, 1);
    }

    wait_handles[0] = g_app.stop_event;
    wait_handles[1] = timer;
    for (;;) {
        DWORD wait_result =
                WaitForMultipleObjects(2, wait_handles, FALSE, INFINITE);
        uint64_t wake_us;
        uint64_t overdue_us;
        uint64_t due_records;
        uint64_t record_index;

        if (wait_result == WAIT_OBJECT_0) {
            break;
        }
        if (wait_result != WAIT_OBJECT_0 + 1) {
            DWORD wait_error = GetLastError();
            set_error(L"Direct timer wait failed: Windows error %lu",
                    wait_error);
            record_diagnostic(
                    DIAGNOSTIC_SEND_FAILURE, sequence, 0, wait_error);
            SetEvent(g_app.stop_event);
            break;
        }

        wake_us = monotonic_microseconds();
        if (wake_us < next_deadline_us) {
            if (!arm_direct_timer(timer, next_deadline_us)) {
                DWORD timer_error = GetLastError();
                set_error(L"Cannot rearm direct timer: Windows error %lu",
                        timer_error);
                record_diagnostic(DIAGNOSTIC_SEND_FAILURE,
                        sequence, 0, timer_error);
                SetEvent(g_app.stop_event);
                break;
            }
            continue;
        }

        overdue_us = wake_us - next_deadline_us;
        due_records = 1u + overdue_us
                / ((uint64_t)DIRECT_PERIOD_MS * 1000u);
        if (due_records > DIRECT_MAX_CATCHUP_RECORDS) {
            due_records = DIRECT_MAX_CATCHUP_RECORDS;
            next_deadline_us = wake_us
                    - (due_records - 1u)
                            * (uint64_t)DIRECT_PERIOD_MS * 1000u;
        }
        if (due_records > 1u) {
            InterlockedAdd64(
                    &g_app.direct_catchup_records,
                    (LONG64)(due_records - 1u));
            record_diagnostic(DIAGNOSTIC_TIMER_CATCHUP, sequence,
                    InterlockedCompareExchange(
                            &g_app.queue_frames, 0, 0),
                    (LONG64)due_records);
        }

        for (record_index = 0; record_index < due_records; record_index++) {
            uint64_t send_us = monotonic_microseconds();
            LONG queued_before;
            BOOL have_real_record;
            BOOL preroll_completed = FALSE;

            EnterCriticalSection(context->queue_lock);
            queued_before = (LONG)*context->ring_frames;
            if (!source_preroll_ready
                    && *context->ring_frames >= required_preroll_frames) {
                source_preroll_ready = TRUE;
                preroll_completed = TRUE;
                InterlockedExchange(&g_app.direct_preroll_ready, 1);
            }
            have_real_record = source_preroll_ready
                    && *context->ring_frames >= AA_DIRECT_FRAMES_PER_RECORD;
            if (have_real_record) {
                memcpy(record, context->ring, sizeof(record));
                *context->ring_frames -= AA_DIRECT_FRAMES_PER_RECORD;
                if (*context->ring_frames != 0u) {
                    memmove(context->ring,
                            context->ring
                                    + AA_DIRECT_FRAMES_PER_RECORD
                                            * AA_CHANNELS,
                            *context->ring_frames * AA_CHANNELS
                                    * sizeof(int16_t));
                }
            }
            InterlockedExchange(
                    &g_app.queue_frames, (LONG)*context->ring_frames);
            LeaveCriticalSection(context->queue_lock);

            if (preroll_completed) {
                record_diagnostic(DIAGNOSTIC_SOURCE_PREROLL_READY,
                        sequence, queued_before, required_preroll_frames);
            }
            if (!have_real_record) {
                InterlockedIncrement64(&g_app.direct_silence_records);
                if (!source_preroll_ready) {
                    InterlockedIncrement64(
                            &g_app.direct_preroll_silence_records);
                }
                if (source_preroll_ready || starvation_records != 0) {
                    if (starvation_records == 0) {
                        record_diagnostic(DIAGNOSTIC_SOURCE_STARVATION_BEGIN,
                                sequence, queued_before,
                                AA_DIRECT_FRAMES_PER_RECORD - queued_before);
                    }
                    starvation_records++;
                }
                source_preroll_ready = FALSE;
                InterlockedExchange(&g_app.direct_preroll_ready, 0);
                // A silent source must not tear down the authenticated TCP
                // session.  Keep sending correctly timed PCM records so Start
                // remains active before the first Windows sound and after it.
                ZeroMemory(record, sizeof(record));
            }
            if (have_real_record && starvation_records != 0) {
                record_diagnostic(DIAGNOSTIC_SOURCE_STARVATION_END,
                        sequence, queued_before, starvation_records);
                starvation_records = 0;
            }

            if (last_send_us != 0u) {
                LONG64 gap_us = (LONG64)(send_us - last_send_us);
                update_maximum(&g_app.direct_max_send_gap_us, gap_us);
                if (gap_us > 25000) {
                    InterlockedIncrement64(&g_app.direct_gaps_over_25ms);
                    record_diagnostic(DIAGNOSTIC_DIRECT_GAP, sequence,
                            InterlockedCompareExchange(
                                    &g_app.queue_frames, 0, 0),
                            gap_us);
                }
                if (gap_us > 50000) {
                    InterlockedIncrement64(&g_app.direct_gaps_over_50ms);
                }
                if (gap_us > 100000) {
                    InterlockedIncrement64(&g_app.direct_gaps_over_100ms);
                }
            }
            last_send_us = send_us;

            if (!aa_direct_send_pcm(context->connection, sequence, record,
                    error, ARRAYSIZE(error))) {
                int socket_error = WSAGetLastError();
                set_error(L"%ls", error);
                record_diagnostic(DIAGNOSTIC_SEND_FAILURE, sequence,
                        queued_before, socket_error);
                SetEvent(g_app.stop_event);
                goto sender_cleanup;
            }
            sequence++;
            InterlockedIncrement64(&g_app.packets_sent);
            InterlockedAdd64(&g_app.bytes_sent, AA_DIRECT_RECORD_BYTES);
            next_deadline_us += (uint64_t)DIRECT_PERIOD_MS * 1000u;
        }

        if (!arm_direct_timer(timer, next_deadline_us)) {
            DWORD timer_error = GetLastError();
            set_error(L"Cannot rearm direct timer: Windows error %lu",
                    timer_error);
            record_diagnostic(
                    DIAGNOSTIC_SEND_FAILURE, sequence, 0, timer_error);
            SetEvent(g_app.stop_event);
            break;
        }
    }

sender_cleanup:
    if (starvation_records != 0) {
        record_diagnostic(DIAGNOSTIC_SOURCE_STARVATION_END,
                sequence, InterlockedCompareExchange(
                        &g_app.queue_frames, 0, 0),
                starvation_records);
    }
    CancelWaitableTimer(timer);
    CloseHandle(timer);
    if (mmcss_handle != NULL) {
        AvRevertMmThreadCharacteristics(mmcss_handle);
    }
    InterlockedExchange(&g_app.direct_sender_mmcss_active, 0);
    return 0;
}

static DWORD WINAPI capture_thread_main(LPVOID unused)
{
    HRESULT result;
    IMMDeviceEnumerator *enumerator = NULL;
    IMMDevice *device = NULL;
    IAudioClient *audio_client = NULL;
    IAudioCaptureClient *capture_client = NULL;
    IAudioClock *audio_clock = NULL;
    IAudioClient *keepalive_audio_client = NULL;
    IAudioRenderClient *keepalive_render_client = NULL;
    WAVEFORMATEX *keepalive_format = NULL;
    HANDLE audio_event = NULL;
    HANDLE keepalive_event = NULL;
    HANDLE mmcss_handle = NULL;
    DWORD mmcss_task_index = 0;
    HANDLE wait_handles[2];
    HANDLE direct_sender_thread = NULL;
    HANDLE keepalive_thread = NULL;
    SOCKET socket_handle = INVALID_SOCKET;
    aa_direct_connection direct_connection;
    direct_sender_context direct_sender;
    wasapi_keepalive_context keepalive_context;
    CRITICAL_SECTION queue_lock;
    WCHAR direct_error[512];
    struct sockaddr_in destination;
    WAVEFORMATEX requested_format;
    UINT32 capture_rate = g_app.direct_enabled
            ? AA_DIRECT_SAMPLE_RATE : AA_SAMPLE_RATE;
    int16_t *ring = NULL;
    UINT32 ring_frames = 0;
    UINT32 keepalive_buffer_frames = 0;
    uint32_t sequence = 0;
    uint64_t last_capture_event_us = 0;
    uint64_t audio_clock_frequency = 0;
    uint64_t last_audio_clock_position = 0;
    BOOL queue_lock_initialized = FALSE;
    BOOL audio_started = FALSE;
    BOOL keepalive_started = FALSE;
    BOOL have_audio_clock_position = FALSE;
    BOOL com_started = FALSE;
    BOOL keep_running = TRUE;
    BOOL have_capture_packet = FALSE;
    (void)unused;
    ZeroMemory(&direct_connection, sizeof(direct_connection));
    direct_connection.socket_handle = INVALID_SOCKET;
    ZeroMemory(&direct_sender, sizeof(direct_sender));
    ZeroMemory(&keepalive_context, sizeof(keepalive_context));

    result = CoInitializeEx(NULL, COINIT_MULTITHREADED);
    if (FAILED(result)) {
        set_error(L"COM initialization failed: 0x%08lx", (unsigned long)result);
        goto cleanup;
    }
    com_started = TRUE;

    mmcss_handle = AvSetMmThreadCharacteristicsW(
            L"Pro Audio", &mmcss_task_index);
    if (mmcss_handle == NULL) {
        mmcss_handle = AvSetMmThreadCharacteristicsW(
                L"Audio", &mmcss_task_index);
    }
    if (mmcss_handle != NULL) {
        AvSetMmThreadPriority(mmcss_handle, AVRT_PRIORITY_HIGH);
        InterlockedExchange(&g_app.mmcss_active, 1);
    }

    result = CoCreateInstance(&CLSID_MMDeviceEnumerator, NULL, CLSCTX_ALL,
            &IID_IMMDeviceEnumerator, (void **)&enumerator);
    if (FAILED(result)) {
        set_error(L"Cannot create audio endpoint enumerator: 0x%08lx",
                (unsigned long)result);
        goto cleanup;
    }
    result = IMMDeviceEnumerator_GetDefaultAudioEndpoint(
            enumerator, eRender, eConsole, &device);
    if (FAILED(result)) {
        set_error(L"No default Windows output endpoint: 0x%08lx",
                (unsigned long)result);
        goto cleanup;
    }
    set_endpoint_name(device);

    result = IMMDevice_Activate(device, &IID_IAudioClient, CLSCTX_ALL,
            NULL, (void **)&audio_client);
    if (FAILED(result)) {
        set_error(L"Cannot activate WASAPI: 0x%08lx", (unsigned long)result);
        goto cleanup;
    }

    ZeroMemory(&requested_format, sizeof(requested_format));
    requested_format.wFormatTag = WAVE_FORMAT_PCM;
    requested_format.nChannels = AA_CHANNELS;
    requested_format.nSamplesPerSec = capture_rate;
    requested_format.wBitsPerSample = 16;
    requested_format.nBlockAlign =
            requested_format.nChannels * requested_format.wBitsPerSample / 8;
    requested_format.nAvgBytesPerSec =
            requested_format.nSamplesPerSec * requested_format.nBlockAlign;

    result = IAudioClient_Initialize(audio_client, AUDCLNT_SHAREMODE_SHARED,
            AUDCLNT_STREAMFLAGS_LOOPBACK
            | AUDCLNT_STREAMFLAGS_EVENTCALLBACK
            | AUDCLNT_STREAMFLAGS_AUTOCONVERTPCM
            | AUDCLNT_STREAMFLAGS_SRC_DEFAULT_QUALITY,
            0, 0, &requested_format, NULL);
    if (FAILED(result)) {
        set_error(L"WASAPI cannot provide PCM16 stereo %lu Hz: 0x%08lx",
                (unsigned long)capture_rate, (unsigned long)result);
        goto cleanup;
    }

    audio_event = CreateEventW(NULL, FALSE, FALSE, NULL);
    if (audio_event == NULL) {
        set_error(L"Cannot create WASAPI event: Windows error %lu", GetLastError());
        goto cleanup;
    }
    result = IAudioClient_SetEventHandle(audio_client, audio_event);
    if (FAILED(result)) {
        set_error(L"Cannot configure WASAPI event: 0x%08lx",
                (unsigned long)result);
        goto cleanup;
    }
    result = IAudioClient_GetService(audio_client, &IID_IAudioCaptureClient,
            (void **)&capture_client);
    if (FAILED(result)) {
        set_error(L"Cannot open WASAPI loopback capture: 0x%08lx",
                (unsigned long)result);
        goto cleanup;
    }

    result = IAudioClient_GetService(
            audio_client, &IID_IAudioClock, (void **)&audio_clock);
    if (SUCCEEDED(result)) {
        result = IAudioClock_GetFrequency(audio_clock, &audio_clock_frequency);
        if (SUCCEEDED(result) && audio_clock_frequency != 0u) {
            InterlockedExchange64(&g_app.audio_clock_frequency,
                    (LONG64)audio_clock_frequency);
            InterlockedExchange(&g_app.audio_clock_active, 1);
        } else {
            IAudioClock_Release(audio_clock);
            audio_clock = NULL;
        }
    }

    if (g_app.direct_enabled && g_app.keepalive_enabled) {
        BYTE *render_data = NULL;

        result = IMMDevice_Activate(device, &IID_IAudioClient, CLSCTX_ALL,
                NULL, (void **)&keepalive_audio_client);
        if (FAILED(result)) {
            set_error(L"Cannot activate WASAPI keep-alive: 0x%08lx",
                    (unsigned long)result);
            goto cleanup;
        }
        result = IAudioClient_GetMixFormat(
                keepalive_audio_client, &keepalive_format);
        if (FAILED(result)) {
            set_error(L"Cannot read keep-alive mix format: 0x%08lx",
                    (unsigned long)result);
            goto cleanup;
        }
        result = IAudioClient_Initialize(
                keepalive_audio_client, AUDCLNT_SHAREMODE_SHARED,
                AUDCLNT_STREAMFLAGS_EVENTCALLBACK
                        | AUDCLNT_STREAMFLAGS_NOPERSIST,
                0, 0, keepalive_format, NULL);
        if (FAILED(result)) {
            set_error(L"Cannot initialize WASAPI keep-alive: 0x%08lx",
                    (unsigned long)result);
            goto cleanup;
        }
        keepalive_event = CreateEventW(NULL, FALSE, FALSE, NULL);
        if (keepalive_event == NULL) {
            set_error(L"Cannot create keep-alive event: Windows error %lu",
                    GetLastError());
            goto cleanup;
        }
        result = IAudioClient_SetEventHandle(
                keepalive_audio_client, keepalive_event);
        if (FAILED(result)) {
            set_error(L"Cannot configure keep-alive event: 0x%08lx",
                    (unsigned long)result);
            goto cleanup;
        }
        result = IAudioClient_GetBufferSize(
                keepalive_audio_client, &keepalive_buffer_frames);
        if (FAILED(result)) {
            set_error(L"Cannot read keep-alive buffer size: 0x%08lx",
                    (unsigned long)result);
            goto cleanup;
        }
        result = IAudioClient_GetService(keepalive_audio_client,
                &IID_IAudioRenderClient,
                (void **)&keepalive_render_client);
        if (FAILED(result)) {
            set_error(L"Cannot open WASAPI render keep-alive: 0x%08lx",
                    (unsigned long)result);
            goto cleanup;
        }
        result = IAudioRenderClient_GetBuffer(keepalive_render_client,
                keepalive_buffer_frames, &render_data);
        if (SUCCEEDED(result)) {
            result = IAudioRenderClient_ReleaseBuffer(
                    keepalive_render_client, keepalive_buffer_frames,
                    AUDCLNT_BUFFERFLAGS_SILENT);
        }
        if (FAILED(result)) {
            set_error(L"Cannot prime WASAPI keep-alive: 0x%08lx",
                    (unsigned long)result);
            goto cleanup;
        }
    }

    if (g_app.direct_enabled) {
        if (!aa_direct_open(&direct_connection,
                g_app.target_ip, g_app.target_port, g_app.direct_udp_enabled,
                direct_error,
                ARRAYSIZE(direct_error))) {
            set_error(L"%ls", direct_error);
            goto cleanup;
        }
        if (!aa_direct_send_config(&direct_connection,
                g_app.direct_buffer_ms, g_app.direct_adaptive_enabled,
                direct_error, ARRAYSIZE(direct_error))) {
            set_error(L"%ls", direct_error);
            goto cleanup;
        }
    } else {
        socket_handle = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
        if (socket_handle == INVALID_SOCKET) {
            set_error(L"Cannot create UDP socket: Winsock error %d",
                    WSAGetLastError());
            goto cleanup;
        }
        ZeroMemory(&destination, sizeof(destination));
        destination.sin_family = AF_INET;
        destination.sin_port = htons(g_app.target_port);
        if (InetPtonW(AF_INET, g_app.target_ip, &destination.sin_addr) != 1) {
            set_error(L"Target must be a valid IPv4 address");
            goto cleanup;
        }
    }

    ring = (int16_t *)HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY,
            RING_CAPACITY_FRAMES * AA_CHANNELS * sizeof(int16_t));
    if (ring == NULL) {
        set_error(L"Cannot allocate the bounded capture queue");
        goto cleanup;
    }
    if (g_app.direct_enabled) {
        InitializeCriticalSection(&queue_lock);
        queue_lock_initialized = TRUE;
        direct_sender.connection = &direct_connection;
        direct_sender.queue_lock = &queue_lock;
        direct_sender.ring = ring;
        direct_sender.ring_frames = &ring_frames;
    }

    EnterCriticalSection(&g_app.status_lock);
    StringCchPrintfW(g_app.capture_format, ARRAYSIZE(g_app.capture_format),
            L"PCM16 stereo %lu Hz; %s; keep-alive %s",
            (unsigned long)capture_rate,
            !g_app.direct_enabled ? L"UDP via AudioTrack"
                    : g_app.direct_udp_enabled
                            ? L"system direct UDP to selected phone interface"
                            : L"system direct TCP to selected phone interface",
            g_app.direct_enabled && g_app.keepalive_enabled
                    ? L"enabled" : L"disabled");
    g_app.last_error[0] = L'\0';
    LeaveCriticalSection(&g_app.status_lock);

    if (g_app.direct_enabled && g_app.keepalive_enabled) {
        keepalive_context.audio_client = keepalive_audio_client;
        keepalive_context.render_client = keepalive_render_client;
        keepalive_context.render_event = keepalive_event;
        keepalive_context.buffer_frames = keepalive_buffer_frames;
        result = IAudioClient_Start(keepalive_audio_client);
        if (FAILED(result)) {
            set_error(L"Cannot start WASAPI keep-alive: 0x%08lx",
                    (unsigned long)result);
            goto cleanup;
        }
        keepalive_started = TRUE;
        keepalive_thread = CreateThread(NULL, 0,
                wasapi_keepalive_thread_main,
                &keepalive_context, 0, NULL);
        if (keepalive_thread == NULL) {
            set_error(L"Cannot create keep-alive thread: Windows error %lu",
                    GetLastError());
            goto cleanup;
        }
    }

    result = IAudioClient_Start(audio_client);
    if (FAILED(result)) {
        set_error(L"Cannot start WASAPI loopback: 0x%08lx",
                (unsigned long)result);
        goto cleanup;
    }
    audio_started = TRUE;
    if (g_app.direct_enabled) {
        direct_sender_thread = CreateThread(NULL, 0,
                direct_sender_thread_main, &direct_sender, 0, NULL);
        if (direct_sender_thread == NULL) {
            set_error(L"Cannot create direct sender thread: Windows error %lu",
                    GetLastError());
            goto cleanup;
        }
        record_diagnostic(DIAGNOSTIC_SESSION_START, 0, 0, 0);
    }
    wait_handles[0] = g_app.stop_event;
    wait_handles[CAPTURE_EVENT_INDEX] = audio_event;

    while (keep_running) {
        DWORD wait_result = WaitForMultipleObjects(2, wait_handles, FALSE, INFINITE);
        if (wait_result == WAIT_OBJECT_0) {
            break;
        }
        if (wait_result != WAIT_OBJECT_0 + CAPTURE_EVENT_INDEX) {
            set_error(L"WASAPI wait failed: Windows error %lu", GetLastError());
            break;
        }

        {
            uint64_t capture_event_us = monotonic_microseconds();
            LONG64 audio_clock_advance_us = -1;
            if (audio_clock != NULL) {
                UINT64 position = 0;
                UINT64 qpc_position = 0;
                HRESULT clock_result = IAudioClock_GetPosition(
                        audio_clock, &position, &qpc_position);
                if (SUCCEEDED(clock_result)) {
                    if (have_audio_clock_position
                            && position >= last_audio_clock_position) {
                        audio_clock_advance_us = (LONG64)
                                clock_units_to_microseconds(
                                        position - last_audio_clock_position,
                                        audio_clock_frequency);
                    }
                    last_audio_clock_position = position;
                    have_audio_clock_position = TRUE;
                    InterlockedExchange64(&g_app.audio_clock_position,
                            (LONG64)position);
                    InterlockedExchange64(&g_app.audio_clock_qpc_100ns,
                            (LONG64)qpc_position);
                    InterlockedExchange(&g_app.audio_clock_active, 1);
                } else {
                    InterlockedExchange(&g_app.audio_clock_active, 0);
                }
            }
            if (last_capture_event_us != 0u) {
                LONG64 gap_us =
                        (LONG64)(capture_event_us - last_capture_event_us);
                update_maximum(&g_app.capture_max_event_gap_us, gap_us);
                if (gap_us > 25000) {
                    InterlockedIncrement64(&g_app.capture_gaps_over_25ms);
                    record_diagnostic_extended(DIAGNOSTIC_CAPTURE_GAP,
                            (uint32_t)InterlockedCompareExchange64(
                                    &g_app.packets_sent, 0, 0),
                            InterlockedCompareExchange(
                                    &g_app.queue_frames, 0, 0),
                            gap_us, audio_clock_advance_us);
                }
                if (gap_us > 50000) {
                    InterlockedIncrement64(&g_app.capture_gaps_over_50ms);
                }
                if (gap_us > 100000) {
                    InterlockedIncrement64(&g_app.capture_gaps_over_100ms);
                }
            }
            last_capture_event_us = capture_event_us;
        }

        for (;;) {
            UINT32 available_frames = 0;
            BYTE *capture_data = NULL;
            UINT32 capture_frames = 0;
            DWORD capture_flags = 0;
            UINT32 dropped_frames;
            BOOL capture_has_signal;
            BOOL sent = TRUE;
            result = IAudioCaptureClient_GetNextPacketSize(
                    capture_client, &available_frames);
            if (FAILED(result)) {
                set_error(L"WASAPI packet query failed: 0x%08lx",
                        (unsigned long)result);
                keep_running = FALSE;
                break;
            }
            if (available_frames == 0u) {
                break;
            }
            result = IAudioCaptureClient_GetBuffer(capture_client,
                    &capture_data, &capture_frames, &capture_flags, NULL, NULL);
            if (FAILED(result)) {
                set_error(L"WASAPI capture failed: 0x%08lx",
                        (unsigned long)result);
                keep_running = FALSE;
                break;
            }
            capture_has_signal = update_capture_activity(
                    capture_data, capture_frames, capture_flags);
            if (have_capture_packet
                    && (capture_flags
                            & AUDCLNT_BUFFERFLAGS_DATA_DISCONTINUITY) != 0u) {
                InterlockedIncrement(&g_app.capture_discontinuities);
                record_diagnostic(DIAGNOSTIC_WASAPI_DISCONTINUITY,
                        (uint32_t)InterlockedCompareExchange64(
                                &g_app.packets_sent, 0, 0),
                        InterlockedCompareExchange(
                                &g_app.queue_frames, 0, 0),
                        capture_flags);
            }
            have_capture_packet = TRUE;

            if (g_app.direct_enabled) {
                uint64_t now_us = monotonic_microseconds();
                LONG64 last_signal_us = InterlockedCompareExchange64(
                        &g_app.last_signal_capture_us, 0, 0);
                BOOL source_active = capture_has_signal
                        || (last_signal_us > 0
                                && (uint64_t)last_signal_us <= now_us
                                && now_us - (uint64_t)last_signal_us
                                        <= DIRECT_IDLE_HOLD_US);
                EnterCriticalSection(&queue_lock);
                if (source_active) {
                    dropped_frames = append_capture_frames(
                            ring, &ring_frames, capture_data,
                            capture_frames, capture_flags);
                } else {
                    ring_frames = 0;
                    dropped_frames = 0;
                    InterlockedExchange(&g_app.direct_preroll_ready, 0);
                }
                InterlockedExchange(
                        &g_app.queue_frames, (LONG)ring_frames);
                LeaveCriticalSection(&queue_lock);
            } else {
                dropped_frames = append_capture_frames(
                        ring, &ring_frames, capture_data,
                        capture_frames, capture_flags);
                InterlockedExchange(
                        &g_app.queue_frames, (LONG)ring_frames);
            }
            if (dropped_frames != 0u) {
                InterlockedAdd64(
                        &g_app.queue_overflow_frames, dropped_frames);
                record_diagnostic(DIAGNOSTIC_QUEUE_OVERFLOW,
                        (uint32_t)InterlockedCompareExchange64(
                                &g_app.packets_sent, 0, 0),
                        InterlockedCompareExchange(
                                &g_app.queue_frames, 0, 0),
                        dropped_frames);
            }

            result = IAudioCaptureClient_ReleaseBuffer(
                    capture_client, capture_frames);
            if (FAILED(result)) {
                set_error(L"WASAPI release failed: 0x%08lx",
                        (unsigned long)result);
                keep_running = FALSE;
                break;
            }
            if (!g_app.direct_enabled) {
                sent = send_ready_packets(socket_handle, &destination,
                        ring, &ring_frames, &sequence);
            }
            if (!sent) {
                keep_running = FALSE;
                break;
            }
        }
    }

cleanup:
    SetEvent(g_app.stop_event);
    if (audio_started) {
        IAudioClient_Stop(audio_client);
    }
    if (keepalive_started) {
        IAudioClient_Stop(keepalive_audio_client);
    }
    if (direct_sender_thread != NULL) {
        WaitForSingleObject(direct_sender_thread, INFINITE);
        CloseHandle(direct_sender_thread);
    }
    if (keepalive_thread != NULL) {
        WaitForSingleObject(keepalive_thread, INFINITE);
        CloseHandle(keepalive_thread);
    }
    if (g_app.direct_enabled) {
        record_diagnostic(DIAGNOSTIC_SESSION_STOP,
                (uint32_t)InterlockedCompareExchange64(
                        &g_app.packets_sent, 0, 0),
                InterlockedCompareExchange(&g_app.queue_frames, 0, 0),
                0);
    }
    if (socket_handle != INVALID_SOCKET) {
        closesocket(socket_handle);
    }
    if (g_app.direct_enabled) {
        aa_direct_close(&direct_connection);
    }
    if (audio_event != NULL) {
        CloseHandle(audio_event);
    }
    if (keepalive_event != NULL) {
        CloseHandle(keepalive_event);
    }
    if (queue_lock_initialized) {
        DeleteCriticalSection(&queue_lock);
    }
    if (ring != NULL) {
        HeapFree(GetProcessHeap(), 0, ring);
    }
    if (audio_clock != NULL) {
        IAudioClock_Release(audio_clock);
    }
    if (capture_client != NULL) {
        IAudioCaptureClient_Release(capture_client);
    }
    if (audio_client != NULL) {
        IAudioClient_Release(audio_client);
    }
    if (keepalive_render_client != NULL) {
        IAudioRenderClient_Release(keepalive_render_client);
    }
    if (keepalive_audio_client != NULL) {
        IAudioClient_Release(keepalive_audio_client);
    }
    if (keepalive_format != NULL) {
        CoTaskMemFree(keepalive_format);
    }
    if (device != NULL) {
        IMMDevice_Release(device);
    }
    if (enumerator != NULL) {
        IMMDeviceEnumerator_Release(enumerator);
    }
    if (mmcss_handle != NULL) {
        AvRevertMmThreadCharacteristics(mmcss_handle);
    }
    InterlockedExchange(&g_app.mmcss_active, 0);
    InterlockedExchange(&g_app.audio_clock_active, 0);
    InterlockedExchange(&g_app.keepalive_thread_active, 0);
    if (com_started) {
        CoUninitialize();
    }
    InterlockedExchange(&g_app.queue_frames, 0);
    if (g_app.direct_enabled) {
        write_diagnostic_report();
    }
    InterlockedExchange(&g_app.running, 0);
    PostMessageW(g_app.window, WM_CAPTURE_STOPPED, 0, 0);
    return 0;
}

static BOOL read_target_controls(void)
{
    WCHAR port_text[16];
    WCHAR buffer_text[16];
    WCHAR preroll_text[16];
    WCHAR min_dbfs_text[16];
    WCHAR max_dbfs_text[16];
    WCHAR *end = NULL;
    unsigned long port;
    struct in_addr address;

    GetWindowTextW(g_app.ip_edit, g_app.target_ip, ARRAYSIZE(g_app.target_ip));
    GetWindowTextW(g_app.port_edit, port_text, ARRAYSIZE(port_text));
    port = wcstoul(port_text, &end, 10);
    if (g_app.target_ip[0] == L'\0'
            || InetPtonW(AF_INET, g_app.target_ip, &address) != 1) {
        MessageBoxW(g_app.window,
                L"Enter the valid Android IPv4 address shown by the phone.",
                L"Audio-Asha Sender", MB_OK | MB_ICONWARNING);
        return FALSE;
    }
    if (end == port_text || *end != L'\0' || port == 0ul || port > 65535ul) {
        MessageBoxW(g_app.window,
                L"Enter the port shown by the phone (1 to 65535).",
                L"Audio-Asha Sender", MB_OK | MB_ICONWARNING);
        return FALSE;
    }
    g_app.target_port = (USHORT)port;

    if (g_app.direct_enabled) {
        unsigned long buffer_ms;
        unsigned long preroll_ms;
        long min_dbfs;
        long max_dbfs;

        GetWindowTextW(g_app.buffer_edit, buffer_text, ARRAYSIZE(buffer_text));
        end = NULL;
        buffer_ms = wcstoul(buffer_text, &end, 10);
        if (end == buffer_text || *end != L'\0'
                || buffer_ms < AA_DIRECT_MIN_BUFFER_MS
                || buffer_ms > AA_DIRECT_MAX_BUFFER_MS
                || buffer_ms % AA_DIRECT_BUFFER_STEP_MS != 0ul) {
            MessageBoxW(g_app.window,
                    L"Enter a direct buffer from 1 to 300 ms in 1 ms steps.",
                    L"Audio-Asha Sender", MB_OK | MB_ICONWARNING);
            return FALSE;
        }
        g_app.direct_buffer_ms = (USHORT)buffer_ms;

        GetWindowTextW(g_app.preroll_edit, preroll_text,
                ARRAYSIZE(preroll_text));
        end = NULL;
        preroll_ms = wcstoul(preroll_text, &end, 10);
        if (end == preroll_text || *end != L'\0'
                || preroll_ms > DIRECT_MAX_PREROLL_MS
                || preroll_ms % DIRECT_PREROLL_STEP_MS != 0ul) {
            MessageBoxW(g_app.window,
                    L"Enter sender preroll from 0 to 100 ms in 10 ms steps.",
                    L"Audio-Asha Sender", MB_OK | MB_ICONWARNING);
            return FALSE;
        }

        GetWindowTextW(g_app.min_dbfs_edit, min_dbfs_text,
                ARRAYSIZE(min_dbfs_text));
        end = NULL;
        min_dbfs = wcstol(min_dbfs_text, &end, 10);
        if (end == min_dbfs_text || *end != L'\0'
                || min_dbfs < DIRECT_MIN_GATE_DBFS
                || min_dbfs > DIRECT_MAX_GATE_DBFS) {
            MessageBoxW(g_app.window,
                    L"Enter Min dBFS from -120 to -20. Recommended: -80.",
                    L"Audio-Asha Sender", MB_OK | MB_ICONWARNING);
            return FALSE;
        }

        GetWindowTextW(g_app.max_dbfs_edit, max_dbfs_text,
                ARRAYSIZE(max_dbfs_text));
        end = NULL;
        max_dbfs = wcstol(max_dbfs_text, &end, 10);
        if (end == max_dbfs_text || *end != L'\0'
                || max_dbfs < DIRECT_MIN_OUTPUT_DBFS
                || max_dbfs > DIRECT_MAX_OUTPUT_DBFS) {
            MessageBoxW(g_app.window,
                    L"Enter Max dBFS from -30 to 0. This setting never boosts.",
                    L"Audio-Asha Sender", MB_OK | MB_ICONWARNING);
            return FALSE;
        }
        g_app.sender_preroll_ms = (USHORT)preroll_ms;
        g_app.min_dbfs = (SHORT)min_dbfs;
        g_app.max_dbfs = (SHORT)max_dbfs;
        g_app.idle_gate_abs_sample = dbfs_to_abs_sample(g_app.min_dbfs);
        g_app.output_gain_q15 = dbfs_to_gain_q15(g_app.max_dbfs);
        g_app.direct_adaptive_enabled = SendMessageW(
                g_app.adaptive_checkbox, BM_GETCHECK, 0, 0) == BST_CHECKED;
        g_app.keepalive_enabled = SendMessageW(
                g_app.keepalive_checkbox, BM_GETCHECK, 0, 0) == BST_CHECKED;
    }
    return TRUE;
}

static void update_status(void);
static const WCHAR *tr(const WCHAR *english, const WCHAR *russian);

static void update_network_adb_buttons(void)
{
    BOOL enabled = InterlockedCompareExchange(&g_app.running, 0, 0) == 0
            && g_app.direct_enabled && !g_app.usb_tether_enabled;
    EnableWindow(g_app.wifi_adb_button, enabled);
    EnableWindow(g_app.bluetooth_pan_adb_button, enabled);
}

static void connect_network_adb(aa_adb_network_kind kind)
{
    WCHAR endpoint[128];
    WCHAR error[512];
    WCHAR message[320];

    EnableWindow(g_app.wifi_adb_button, FALSE);
    EnableWindow(g_app.bluetooth_pan_adb_button, FALSE);
    g_app.adb_network_endpoint[0] = L'\0';
    if (!aa_adb_connect_network(kind, endpoint, ARRAYSIZE(endpoint),
            error, ARRAYSIZE(error))) {
        MessageBoxW(g_app.window, error, L"Network ADB",
                MB_OK | MB_ICONWARNING);
        update_network_adb_buttons();
        update_status();
        return;
    }
    StringCchCopyW(g_app.adb_network_endpoint,
            ARRAYSIZE(g_app.adb_network_endpoint), endpoint);
    StringCchPrintfW(message, ARRAYSIZE(message),
            L"Connected and selected:\r\n%s\r\n\r\n"
            L"You can now press Start. The USB cable may remain connected.",
            endpoint);
    MessageBoxW(g_app.window, message, L"Network ADB",
            MB_OK | MB_ICONINFORMATION);
    update_network_adb_buttons();
    update_status();
}

static void start_capture(void)
{
    if (InterlockedCompareExchange(&g_app.running, 1, 0) != 0) {
        return;
    }
    g_app.direct_enabled = SendMessageW(
            g_app.direct_checkbox, BM_GETCHECK, 0, 0) == BST_CHECKED;
    g_app.direct_udp_enabled = SendMessageW(
            g_app.direct_udp_checkbox, BM_GETCHECK, 0, 0) == BST_CHECKED;
    if (!read_target_controls()) {
        InterlockedExchange(&g_app.running, 0);
        return;
    }
    save_settings();
    ResetEvent(g_app.stop_event);
    InterlockedExchange64(&g_app.packets_sent, 0);
    InterlockedExchange64(&g_app.bytes_sent, 0);
    InterlockedExchange64(&g_app.queue_overflow_frames, 0);
    InterlockedExchange(&g_app.capture_discontinuities, 0);
    InterlockedExchange(&g_app.mmcss_active, 0);
    InterlockedExchange64(&g_app.direct_max_send_gap_us, 0);
    InterlockedExchange64(&g_app.direct_gaps_over_25ms, 0);
    InterlockedExchange64(&g_app.direct_gaps_over_50ms, 0);
    InterlockedExchange64(&g_app.direct_gaps_over_100ms, 0);
    InterlockedExchange64(&g_app.capture_max_event_gap_us, 0);
    InterlockedExchange64(&g_app.capture_gaps_over_25ms, 0);
    InterlockedExchange64(&g_app.capture_gaps_over_50ms, 0);
    InterlockedExchange64(&g_app.capture_gaps_over_100ms, 0);
    InterlockedExchange64(&g_app.direct_silence_records, 0);
    InterlockedExchange64(&g_app.direct_preroll_silence_records, 0);
    InterlockedExchange(&g_app.direct_preroll_ready, 0);
    InterlockedExchange64(&g_app.direct_catchup_records, 0);
    InterlockedExchange(&g_app.direct_sender_mmcss_active, 0);
    InterlockedExchange(&g_app.diagnostic_events, 0);
    InterlockedExchange(&g_app.diagnostic_events_dropped, 0);
    InterlockedExchange64(&g_app.captured_signal_frames, 0);
    InterlockedExchange64(&g_app.captured_silent_frames, 0);
    InterlockedExchange64(&g_app.last_signal_capture_us, 0);
    InterlockedExchange64(&g_app.audio_clock_position, 0);
    InterlockedExchange64(&g_app.audio_clock_frequency, 0);
    InterlockedExchange64(&g_app.audio_clock_qpc_100ns, 0);
    InterlockedExchange(&g_app.last_capture_flags, 0);
    InterlockedExchange(&g_app.audio_clock_active, 0);
    InterlockedExchange64(&g_app.keepalive_refills, 0);
    InterlockedExchange64(&g_app.keepalive_frames, 0);
    InterlockedExchange(&g_app.keepalive_thread_active, 0);
    InterlockedExchange(&g_app.queue_frames, 0);
    g_app.previous_bytes = 0;
    EnterCriticalSection(&g_app.status_lock);
    g_app.endpoint_name[0] = L'\0';
    g_app.capture_format[0] = L'\0';
    g_app.last_error[0] = L'\0';
    g_app.diagnostic_path[0] = L'\0';
    LeaveCriticalSection(&g_app.status_lock);

    g_app.capture_thread = CreateThread(NULL, 0,
            capture_thread_main, NULL, 0, NULL);
    if (g_app.capture_thread == NULL) {
        InterlockedExchange(&g_app.running, 0);
        set_error(L"Cannot create capture thread: Windows error %lu", GetLastError());
        return;
    }
    EnableWindow(g_app.start_button, FALSE);
    EnableWindow(g_app.stop_button, TRUE);
    EnableWindow(g_app.ip_edit, FALSE);
    EnableWindow(g_app.port_edit, FALSE);
    EnableWindow(g_app.direct_checkbox, FALSE);
    EnableWindow(g_app.direct_udp_checkbox, FALSE);
    EnableWindow(g_app.usb_tether_checkbox, FALSE);
    EnableWindow(g_app.wifi_adb_button, FALSE);
    EnableWindow(g_app.bluetooth_pan_adb_button, FALSE);
    EnableWindow(g_app.keepalive_checkbox, FALSE);
    EnableWindow(g_app.buffer_edit, FALSE);
    EnableWindow(g_app.preroll_edit, FALSE);
    EnableWindow(g_app.min_dbfs_edit, FALSE);
    EnableWindow(g_app.max_dbfs_edit, FALSE);
    EnableWindow(g_app.adaptive_checkbox, FALSE);
}

static void stop_capture(void)
{
    HANDLE thread = g_app.capture_thread;
    if (thread == NULL) {
        return;
    }
    SetEvent(g_app.stop_event);
    WaitForSingleObject(thread, 5000);
    CloseHandle(thread);
    g_app.capture_thread = NULL;
    InterlockedExchange(&g_app.running, 0);
    EnableWindow(g_app.start_button, TRUE);
    EnableWindow(g_app.stop_button, FALSE);
    EnableWindow(g_app.ip_edit, TRUE);
    EnableWindow(g_app.port_edit, TRUE);
    EnableWindow(g_app.direct_checkbox, TRUE);
    EnableWindow(g_app.direct_udp_checkbox, g_app.direct_enabled);
    EnableWindow(g_app.usb_tether_checkbox, g_app.direct_enabled);
    update_network_adb_buttons();
    EnableWindow(g_app.keepalive_checkbox, g_app.direct_enabled);
    EnableWindow(g_app.buffer_edit, g_app.direct_enabled);
    EnableWindow(g_app.preroll_edit, g_app.direct_enabled);
    EnableWindow(g_app.min_dbfs_edit, g_app.direct_enabled);
    EnableWindow(g_app.max_dbfs_edit, g_app.direct_enabled);
    EnableWindow(g_app.adaptive_checkbox, g_app.direct_enabled);
}

static void update_status(void)
{
    WCHAR endpoint[256];
    WCHAR format[256];
    WCHAR error[512];
    WCHAR status[STATUS_CHARS];
    WCHAR summary[1024];
    WCHAR previous_summary[1024];
    LONG64 packets = InterlockedCompareExchange64(&g_app.packets_sent, 0, 0);
    LONG64 bytes = InterlockedCompareExchange64(&g_app.bytes_sent, 0, 0);
    LONG64 overflow = InterlockedCompareExchange64(
            &g_app.queue_overflow_frames, 0, 0);
    LONG discontinuities = InterlockedCompareExchange(&g_app.capture_discontinuities, 0, 0);
    LONG mmcss_active = InterlockedCompareExchange(&g_app.mmcss_active, 0, 0);
    LONG64 max_direct_gap_us = InterlockedCompareExchange64(
            &g_app.direct_max_send_gap_us, 0, 0);
    LONG64 gaps_over_25ms = InterlockedCompareExchange64(
            &g_app.direct_gaps_over_25ms, 0, 0);
    LONG64 gaps_over_50ms = InterlockedCompareExchange64(
            &g_app.direct_gaps_over_50ms, 0, 0);
    LONG64 gaps_over_100ms = InterlockedCompareExchange64(
            &g_app.direct_gaps_over_100ms, 0, 0);
    LONG64 capture_max_gap_us = InterlockedCompareExchange64(
            &g_app.capture_max_event_gap_us, 0, 0);
    LONG64 capture_gaps_over_25ms = InterlockedCompareExchange64(
            &g_app.capture_gaps_over_25ms, 0, 0);
    LONG64 capture_gaps_over_50ms = InterlockedCompareExchange64(
            &g_app.capture_gaps_over_50ms, 0, 0);
    LONG64 capture_gaps_over_100ms = InterlockedCompareExchange64(
            &g_app.capture_gaps_over_100ms, 0, 0);
    LONG64 silence_records = InterlockedCompareExchange64(
            &g_app.direct_silence_records, 0, 0);
    LONG64 preroll_silence_records = InterlockedCompareExchange64(
            &g_app.direct_preroll_silence_records, 0, 0);
    LONG preroll_ready = InterlockedCompareExchange(
            &g_app.direct_preroll_ready, 0, 0);
    LONG64 catchup_records = InterlockedCompareExchange64(
            &g_app.direct_catchup_records, 0, 0);
    LONG sender_mmcss = InterlockedCompareExchange(
            &g_app.direct_sender_mmcss_active, 0, 0);
    LONG diagnostic_events = InterlockedCompareExchange(
            &g_app.diagnostic_events, 0, 0);
    LONG diagnostic_dropped = InterlockedCompareExchange(
            &g_app.diagnostic_events_dropped, 0, 0);
    LONG64 signal_frames = InterlockedCompareExchange64(
            &g_app.captured_signal_frames, 0, 0);
    LONG64 silent_frames = InterlockedCompareExchange64(
            &g_app.captured_silent_frames, 0, 0);
    LONG64 last_signal_us = InterlockedCompareExchange64(
            &g_app.last_signal_capture_us, 0, 0);
    LONG64 clock_position = InterlockedCompareExchange64(
            &g_app.audio_clock_position, 0, 0);
    LONG64 clock_frequency = InterlockedCompareExchange64(
            &g_app.audio_clock_frequency, 0, 0);
    LONG clock_active = InterlockedCompareExchange(
            &g_app.audio_clock_active, 0, 0);
    LONG64 keepalive_refills = InterlockedCompareExchange64(
            &g_app.keepalive_refills, 0, 0);
    LONG64 keepalive_frames = InterlockedCompareExchange64(
            &g_app.keepalive_frames, 0, 0);
    LONG keepalive_active = InterlockedCompareExchange(
            &g_app.keepalive_thread_active, 0, 0);
    LONG queue_frames = InterlockedCompareExchange(&g_app.queue_frames, 0, 0);
    UINT32 capture_rate = g_app.direct_enabled
            ? AA_DIRECT_SAMPLE_RATE : AA_SAMPLE_RATE;
    LONG64 bytes_per_second = (bytes - g_app.previous_bytes) * 2;
    uint64_t now_us = monotonic_microseconds();
    double last_signal_age_ms =
            last_signal_us > 0 && (uint64_t)last_signal_us <= now_us
                    ? (double)(now_us - (uint64_t)last_signal_us) / 1000.0
                    : -1.0;
    g_app.previous_bytes = bytes;

    EnterCriticalSection(&g_app.status_lock);
    StringCchCopyW(endpoint, ARRAYSIZE(endpoint),
            g_app.endpoint_name[0] == L'\0' ? L"Waiting..." : g_app.endpoint_name);
    StringCchCopyW(format, ARRAYSIZE(format),
            g_app.capture_format[0] == L'\0' ? L"Waiting..." : g_app.capture_format);
    StringCchCopyW(error, ARRAYSIZE(error),
            g_app.last_error[0] == L'\0' ? L"None" : g_app.last_error);
    LeaveCriticalSection(&g_app.status_lock);

    StringCchPrintfW(status, ARRAYSIZE(status),
            L"State: %s\r\n"
            L"Transport: %s\r\n"
            L"Target entered manually: %s:%u\r\n"
            L"ADB authorization: current authorized device\r\n"
            L"Direct buffer policy: %u ms, %s\r\n"
            L"Sender preroll: %u ms (10 ms record minimum), %s; "
            L"pre-roll idle ticks: %lld\r\n"
            L"PCM dBFS: min gate %d (abs > %ld), max output %d\r\n"
            L"Active Windows audio endpoint: %s\r\n"
            L"Capture format: %s\r\n"
            L"Captured frames above/below gate: %lld / %lld\r\n"
            L"Last above-gate PCM age: %.1f ms (-1 means none yet)\r\n"
            L"Endpoint AudioClock: %s; position/frequency: %lld / %lld\r\n"
            L"WASAPI keep-alive selected/thread: %s / %s; "
            L"refills/frames: %lld / %lld\r\n"
            L"Records sent: %lld\r\n"
            L"Bytes sent/sec: %lld\r\n"
            L"Sender queue: %ld frames (%.1f ms)\r\n"
            L"Queue overflow frames: %lld\r\n"
            L"Capture discontinuities: %ld\r\n"
            L"MMCSS capture/direct priority: %s / %s\r\n"
            L"Idle ticks skipped (no PCM packet, 10 ms): %lld\r\n"
            L"Timer catch-up records: %lld\r\n"
            L"Maximum WASAPI event gap: %.1f ms\r\n"
            L"WASAPI gaps >25/50/100 ms: %lld / %lld / %lld\r\n"
            L"Maximum direct send gap: %.1f ms\r\n"
            L"Direct gaps >25/50/100 ms: %lld / %lld / %lld\r\n"
            L"Buffered diagnostic events / dropped: %ld / %ld\r\n"
            L"Diagnostic CSV after Stop: AudioAshaSender-v1.7-last.csv\r\n"
            L"Last error: %s",
            InterlockedCompareExchange(&g_app.running, 0, 0) ? L"Running" : L"Stopped",
            !g_app.direct_enabled ? L"UDP via Android AudioTrack"
                    : g_app.direct_udp_enabled
                            ? L"AshaOS Direct UDP to selected interface"
                            : L"AshaOS Direct TCP to selected interface",
            g_app.target_ip, (unsigned int)g_app.target_port,
            g_app.direct_buffer_ms,
            g_app.direct_adaptive_enabled ? L"adaptive (+5 ms/underflow)"
                    : L"fixed",
            (unsigned int)g_app.sender_preroll_ms,
            preroll_ready ? L"ready" : L"waiting for real PCM",
            preroll_silence_records,
            (int)g_app.min_dbfs, g_app.idle_gate_abs_sample,
            (int)g_app.max_dbfs,
            endpoint, format,
            signal_frames, silent_frames, last_signal_age_ms,
            clock_active ? L"active" : L"unavailable",
            clock_position, clock_frequency,
            g_app.direct_enabled && g_app.keepalive_enabled
                    ? L"enabled" : L"disabled",
            keepalive_active ? L"active" : L"inactive",
            keepalive_refills, keepalive_frames,
            packets, bytes_per_second, queue_frames,
            (double)queue_frames * 1000.0 / (double)capture_rate,
            overflow, discontinuities,
            mmcss_active ? L"active" : L"inactive",
            sender_mmcss ? L"active" : L"inactive",
            silence_records, catchup_records,
            (double)capture_max_gap_us / 1000.0,
            capture_gaps_over_25ms, capture_gaps_over_50ms,
            capture_gaps_over_100ms,
            (double)max_direct_gap_us / 1000.0,
            gaps_over_25ms, gaps_over_50ms, gaps_over_100ms,
            diagnostic_events, diagnostic_dropped,
            error);
    StringCchPrintfW(status, ARRAYSIZE(status),
            tr(L"State: %s\r\nConnection: %s\r\nPhone: %s:%u\r\n"
               L"Windows output: %s\r\nRecords sent: %lld\r\n"
               L"Speed: %lld B/s\r\nQueue overflows: %lld\r\n"
               L"Last error: %s",
               L"Состояние: %s\r\nПодключение: %s\r\nТелефон: %s:%u\r\n"
               L"Выход Windows: %s\r\nОтправлено кадров: %lld\r\n"
               L"Скорость: %lld Б/с\r\nПереполнений буфера: %lld\r\n"
               L"Ошибка: %s"),
            InterlockedCompareExchange(&g_app.running, 0, 0)
                    ? tr(L"Running", L"Работает")
                    : tr(L"Stopped", L"Остановлено"),
            g_app.direct_enabled
                    ? (g_app.direct_udp_enabled ? L"Direct UDP" : L"Direct TCP")
                    : L"UDP via AudioTrack",
            g_app.target_ip, (unsigned int)g_app.target_port,
            wcscmp(endpoint, L"Waiting...") == 0
                    ? tr(L"Waiting for sound", L"Ожидание звука") : endpoint,
            packets, bytes_per_second, overflow,
            wcscmp(error, L"None") == 0 ? tr(L"None", L"Нет") : error);
    StringCchPrintfW(summary, ARRAYSIZE(summary),
            tr(L"%s  ·  %s\r\nPhone: %s:%u\r\n"
               L"Records sent: %lld  ·  Speed: %lld B/s\r\nLast error: %s",
               L"%s  ·  %s\r\nТелефон: %s:%u\r\n"
               L"Отправлено кадров: %lld  ·  Скорость: %lld Б/с\r\nОшибка: %s"),
            InterlockedCompareExchange(&g_app.running, 0, 0)
                    ? tr(L"Running", L"Работает")
                    : tr(L"Stopped", L"Остановлено"),
            g_app.direct_enabled
                    ? (g_app.direct_udp_enabled ? L"Direct UDP" : L"Direct TCP")
                    : L"UDP via AudioTrack",
            g_app.target_ip, (unsigned int)g_app.target_port,
            packets, bytes_per_second,
            wcscmp(error, L"None") == 0 ? tr(L"None", L"Нет") : error);
    GetWindowTextW(g_app.status_label, previous_summary,
            ARRAYSIZE(previous_summary));
    if (wcscmp(previous_summary, summary) != 0) {
        SetWindowTextW(g_app.status_label, summary);
    }
    if (g_app.developers_visible && GetFocus() != g_app.log_label) {
        WCHAR previous[STATUS_CHARS];
        GetWindowTextW(g_app.log_label, previous, ARRAYSIZE(previous));
        if (wcscmp(previous, status) != 0) {
            int first_line = (int)SendMessageW(
                    g_app.log_label, EM_GETFIRSTVISIBLELINE, 0, 0);
            SendMessageW(g_app.log_label, WM_SETREDRAW, FALSE, 0);
            SetWindowTextW(g_app.log_label, status);
            SendMessageW(g_app.log_label, EM_LINESCROLL, 0, first_line);
            SendMessageW(g_app.log_label, WM_SETREDRAW, TRUE, 0);
            InvalidateRect(g_app.log_label, NULL, FALSE);
        }
    }
}

static HWND make_control(
        DWORD extended_style,
        const WCHAR *class_name,
        const WCHAR *text,
        DWORD style,
        int x,
        int y,
        int width,
        int height,
        HWND parent,
        int id)
{
    return CreateWindowExW(extended_style, class_name, text, style,
            x, y, width, height, parent, (HMENU)(INT_PTR)id,
            GetModuleHandleW(NULL), NULL);
}

static const WCHAR *tr(const WCHAR *english, const WCHAR *russian)
{
    return g_app.ui_language == 1 ? russian : english;
}

static BOOL dark_theme_active(void)
{
    HKEY key;
    DWORD light = 1u;
    DWORD bytes = sizeof(light);
    if (g_app.ui_theme == 2) {
        return TRUE;
    }
    if (g_app.ui_theme == 1) {
        return FALSE;
    }
    if (RegOpenKeyExW(HKEY_CURRENT_USER,
            L"Software\\Microsoft\\Windows\\CurrentVersion\\Themes\\Personalize",
            0, KEY_READ, &key) == ERROR_SUCCESS) {
        RegQueryValueExW(key, L"AppsUseLightTheme", NULL, NULL,
                (BYTE *)&light, &bytes);
        RegCloseKey(key);
    }
    return light == 0u;
}

static void apply_theme(void)
{
    BOOL dark = dark_theme_active();
    BOOL dark_title = dark;
    HWND child = NULL;
    if (g_app.background_brush != NULL) {
        DeleteObject(g_app.background_brush);
    }
    if (g_app.surface_brush != NULL) {
        DeleteObject(g_app.surface_brush);
    }
    if (g_app.tooltip_brush != NULL) {
        DeleteObject(g_app.tooltip_brush);
    }
    g_app.background_brush = CreateSolidBrush(
            dark ? RGB(17, 25, 39) : RGB(242, 246, 252));
    g_app.surface_brush = CreateSolidBrush(
            dark ? RGB(29, 41, 59) : RGB(255, 255, 255));
    g_app.tooltip_brush = CreateSolidBrush(
            dark ? RGB(52, 68, 89) : RGB(255, 251, 226));
    DwmSetWindowAttribute(g_app.window, 20,
            &dark_title, sizeof(dark_title));
    while ((child = FindWindowExW(g_app.window, child, NULL, NULL)) != NULL) {
        SetWindowTheme(child, dark ? L"DarkMode_Explorer" : NULL, NULL);
        InvalidateRect(child, NULL, TRUE);
    }
    if (g_app.tooltip_window != NULL) {
        InvalidateRect(g_app.tooltip_window, NULL, TRUE);
    }
    InvalidateRect(g_app.window, NULL, TRUE);
}

static HWND make_ui_control(DWORD extended_style, const WCHAR *class_name,
        const WCHAR *caption, DWORD style, int x, int y, int width,
        int height, HWND parent, int id, BOOL developer)
{
    HWND control = make_control(extended_style, class_name, caption, style,
            x, y, width, height, parent, id);
    if (control != NULL) {
        SendMessageW(control, WM_SETFONT, (WPARAM)g_app.ui_font, TRUE);
        if (developer && g_app.dev_control_count < ARRAYSIZE(g_app.dev_controls)) {
            g_app.dev_controls[g_app.dev_control_count++] = control;
            ShowWindow(control, SW_HIDE);
        }
    }
    return control;
}

static unsigned int current_edit_ms(HWND edit, unsigned int fallback,
        unsigned int minimum, unsigned int maximum, unsigned int step)
{
    WCHAR text[16];
    WCHAR *end;
    unsigned long value;
    if (edit == NULL || GetWindowTextW(edit, text, ARRAYSIZE(text)) == 0) {
        return fallback;
    }
    value = wcstoul(text, &end, 10);
    if (end == text || *end != L'\0' || value < minimum
            || value > maximum || value % step != 0ul) {
        return fallback;
    }
    return (unsigned int)value;
}

static const WCHAR *help_text(UINT id)
{
    static WCHAR dynamic_text[512];
    switch (id) {
        case ID_HELP_CONNECTION:
            return tr(
                    L"Direct TCP and UDP bypass Android AudioTrack on USB, Wi-Fi and Bluetooth PAN. "
                    L"TCP preserves every record; UDP avoids retransmission stalls when a packet is lost. "
                    L"Bluetooth PAN can be slow and unstable because it shares the radio with ASHA. "
                    L"Choose the same protocol on the phone and copy its IP and port here.",
                    L"Direct TCP и UDP обходят Android AudioTrack через USB, Wi-Fi и Bluetooth PAN. "
                    L"TCP сохраняет все кадры; UDP не ждёт повторной передачи потерянного пакета. "
                    L"Bluetooth PAN может работать нестабильно, поскольку делит радиоканал с ASHA. "
                    L"Выберите на телефоне тот же протокол и скопируйте сюда IP и порт.");
        case ID_HELP_BUFFER:
            StringCchPrintfW(dynamic_text, ARRAYSIZE(dynamic_text), tr(
                    L"Phone buffer is now %u ms. Increase it if audio breaks up; "
                    L"the buffer is only part of total delay.",
                    L"Буфер телефона сейчас %u мс. Увеличьте при обрывах звука; "
                    L"это лишь часть общей задержки."),
                    current_edit_ms(g_app.buffer_edit, g_app.direct_buffer_ms,
                            AA_DIRECT_MIN_BUFFER_MS, AA_DIRECT_MAX_BUFFER_MS,
                            AA_DIRECT_BUFFER_STEP_MS));
            return dynamic_text;
        case ID_HELP_KEEPALIVE:
            return tr(
                    L"Keeps Windows audio capture ready before any app plays sound. "
                    L"Direct mode continues sending silence, so Start stays active.",
                    L"Поддерживает захват звука Windows до первого звука. Direct "
                    L"передаёт тишину, поэтому «Запустить» остаётся активным.");
        case ID_HELP_PREROLL:
            StringCchPrintfW(dynamic_text, ARRAYSIZE(dynamic_text), tr(
                    L"Sender preroll is now %u ms. The PC holds this much audio before "
                    L"sending the first sound. More can smooth playback but adds delay. "
                    L"At 0 ms, one complete 10 ms record is still required.",
                    L"Предбуфер сейчас %u мс. ПК собирает столько звука до отправки "
                    L"первого кадра. Больший запас сглаживает запуск, но добавляет "
                    L"задержку. При 0 мс всё равно нужен один кадр 10 мс."),
                    current_edit_ms(g_app.preroll_edit, g_app.sender_preroll_ms,
                            DIRECT_MIN_PREROLL_MS, DIRECT_MAX_PREROLL_MS,
                            DIRECT_PREROLL_STEP_MS));
            return dynamic_text;
        default:
            return L"";
    }
}

static const WCHAR *short_help_text(UINT id)
{
    static WCHAR dynamic_text[256];
    switch (id) {
        case ID_HELP_CONNECTION:
            return tr(L"Direct TCP/UDP: USB, Wi-Fi or Bluetooth PAN. Match the phone mode.",
                    L"Direct TCP/UDP: USB, Wi-Fi или Bluetooth PAN. Режимы должны совпадать.");
        case ID_HELP_BUFFER:
            StringCchPrintfW(dynamic_text, ARRAYSIZE(dynamic_text),
                    tr(L"Phone buffer: %u ms. Increase it if sound breaks up.",
                       L"Буфер телефона: %u мс. Увеличьте при обрывах звука."),
                    current_edit_ms(g_app.buffer_edit, g_app.direct_buffer_ms,
                            AA_DIRECT_MIN_BUFFER_MS, AA_DIRECT_MAX_BUFFER_MS,
                            AA_DIRECT_BUFFER_STEP_MS));
            return dynamic_text;
        case ID_HELP_KEEPALIVE:
            return tr(L"Keeps capture and the connection active during silence.",
                    L"Сохраняет захват и подключение активными при тишине.");
        case ID_HELP_PREROLL:
            StringCchPrintfW(dynamic_text, ARRAYSIZE(dynamic_text),
                    tr(L"PC collects %u ms before sending: smoother start, more delay.",
                       L"ПК собирает %u мс до отправки: ровнее старт, больше задержка."),
                    current_edit_ms(g_app.preroll_edit, g_app.sender_preroll_ms,
                            DIRECT_MIN_PREROLL_MS, DIRECT_MAX_PREROLL_MS,
                            DIRECT_PREROLL_STEP_MS));
            return dynamic_text;
        default:
            return L"";
    }
}

static void set_help_hover(HWND button, BOOL visible)
{
    RECT rect;
    RECT measure = {0, 0, 350, 0};
    MONITORINFO monitor;
    HDC dc;
    HGDIOBJ old_font;
    const WCHAR *caption;
    int x, y, width, height;
    if (g_app.tooltip_window == NULL || !IsWindow(g_app.tooltip_window)
            || button == NULL) {
        return;
    }
    ZeroMemory(&monitor, sizeof(monitor));
    monitor.cbSize = sizeof(monitor);
    if (visible) {
        if (g_app.active_help_button != NULL
                && g_app.active_help_button != button) {
            set_help_hover(g_app.active_help_button, FALSE);
        }
        caption = short_help_text((UINT)GetDlgCtrlID(button));
        SetWindowTextW(g_app.tooltip_window, caption);
        dc = GetDC(g_app.tooltip_window);
        if (dc == NULL) {
            return;
        }
        old_font = SelectObject(dc, g_app.ui_font);
        DrawTextW(dc, caption, -1, &measure,
                DT_CALCRECT | DT_WORDBREAK | DT_NOPREFIX);
        SelectObject(dc, old_font);
        ReleaseDC(g_app.tooltip_window, dc);
        width = measure.right + 24;
        height = measure.bottom + 20;
        GetWindowRect(button, &rect);
        x = rect.left;
        y = rect.bottom + 8;
        if (GetMonitorInfoW(MonitorFromWindow(button,
                MONITOR_DEFAULTTONEAREST), &monitor)) {
            if (x + width > monitor.rcWork.right) {
                x = monitor.rcWork.right - width - 8;
            }
            if (x < monitor.rcWork.left) {
                x = monitor.rcWork.left + 8;
            }
            if (y + height > monitor.rcWork.bottom) {
                y = rect.top - height - 8;
            }
        }
        SetWindowPos(g_app.tooltip_window, HWND_TOPMOST,
                x, y, width, height,
                SWP_NOACTIVATE | SWP_SHOWWINDOW | SWP_NOCOPYBITS);
        g_app.active_help_button = button;
    } else if (g_app.active_help_button == button) {
        ShowWindow(g_app.tooltip_window, SW_HIDE);
        g_app.active_help_button = NULL;
    }
}

static LRESULT CALLBACK help_button_proc(HWND button, UINT message,
        WPARAM wparam, LPARAM lparam, UINT_PTR subclass_id, DWORD_PTR ref_data)
{
    (void)subclass_id;
    (void)ref_data;
    switch (message) {
        case WM_MOUSEMOVE: {
            TRACKMOUSEEVENT tracking;
            ZeroMemory(&tracking, sizeof(tracking));
            tracking.cbSize = sizeof(tracking);
            tracking.dwFlags = TME_LEAVE;
            tracking.hwndTrack = button;
            TrackMouseEvent(&tracking);
            if (g_app.active_help_button != button) {
                set_help_hover(button, TRUE);
            }
            break;
        }
        case WM_MOUSELEAVE:
        case WM_LBUTTONDOWN:
            set_help_hover(button, FALSE);
            break;
        case WM_NCDESTROY:
            set_help_hover(button, FALSE);
            RemoveWindowSubclass(button, help_button_proc, 1);
            break;
        default:
            break;
    }
    return DefSubclassProc(button, message, wparam, lparam);
}

static void add_help_tool(HWND button)
{
    if (button != NULL) {
        SetWindowSubclass(button, help_button_proc, 1, 0);
    }
}

static void update_help_tool(HWND button)
{
    if (button != NULL && g_app.active_help_button == button) {
        SetWindowTextW(g_app.tooltip_window,
                short_help_text((UINT)GetDlgCtrlID(button)));
    }
}

static int checkbox_caption_width(HWND control)
{
    WCHAR caption[160];
    SIZE size = {0, 0};
    HDC dc;
    HGDIOBJ old_font;
    if (control == NULL) {
        return 400;
    }
    GetWindowTextW(control, caption, ARRAYSIZE(caption));
    dc = GetDC(control);
    if (dc == NULL) {
        return 400;
    }
    old_font = SelectObject(dc, g_app.ui_font);
    GetTextExtentPoint32W(dc, caption, (int)wcslen(caption), &size);
    SelectObject(dc, old_font);
    ReleaseDC(control, dc);
    return size.cx + 36;
}

static void position_control(HWND control, int x, int y, int width, int height)
{
    if (control != NULL) {
        SetWindowPos(control, NULL, x, y,
                width > 1 ? width : 1, height > 1 ? height : 1,
                SWP_NOZORDER | SWP_NOACTIVATE | SWP_NOREDRAW | SWP_NOCOPYBITS);
    }
}

static void layout_controls(void)
{
    RECT client;
    int width, height, main_width, main_right, inner_right;
    int port_x, side_x, side_width, direct_width, direct_udp_width;
    int keepalive_width;
    if (g_app.window == NULL || g_app.heading_label == NULL) {
        return;
    }
    if (g_app.active_help_button != NULL) {
        set_help_hover(g_app.active_help_button, FALSE);
    }
    GetClientRect(g_app.window, &client);
    width = client.right;
    height = client.bottom;
    main_width = g_app.developers_visible ? (width - 60) * 62 / 100 : width - 40;
    if (g_app.developers_visible && main_width < 690) {
        main_width = 690;
    }
    main_right = 20 + main_width;
    inner_right = main_right - 24;
    port_x = inner_right - 116;
    direct_width = checkbox_caption_width(g_app.direct_checkbox);
    direct_udp_width = checkbox_caption_width(g_app.direct_udp_checkbox);
    keepalive_width = checkbox_caption_width(g_app.keepalive_checkbox);
    if (direct_width > inner_right - 86) {
        direct_width = inner_right - 86;
    }
    if (direct_udp_width > inner_right - 86) {
        direct_udp_width = inner_right - 86;
    }
    if (keepalive_width > inner_right - 86) {
        keepalive_width = inner_right - 86;
    }

    position_control(g_app.heading_label, 30, 16, main_width - 330, 42);
    position_control(g_app.subtitle_label, 32, 57, main_width - 330, 29);
    position_control(g_app.appearance_label, main_right - 310, 14, 292, 25);
    position_control(g_app.theme_combo, main_right - 310, 40, 156, 180);
    position_control(g_app.language_combo, main_right - 144, 40, 126, 180);
    position_control(g_app.connection_group, 20, 104, main_width, 30);
    position_control(g_app.target_label, 36, 141, port_x - 52, 26);
    position_control(g_app.port_label, port_x, 141, 110, 26);
    position_control(g_app.ip_edit, 36, 170, port_x - 52, 34);
    position_control(g_app.port_edit, port_x, 170, 110, 34);
    position_control(g_app.direct_checkbox, 36, 224, direct_width, 32);
    position_control(g_app.direct_help_button, 44 + direct_width, 224, 32, 32);
    position_control(g_app.direct_udp_checkbox, 36, 267, direct_udp_width, 32);
    position_control(g_app.keepalive_checkbox, 36, 310, keepalive_width, 32);
    position_control(g_app.keepalive_help_button,
            44 + keepalive_width, 310, 32, 32);
    position_control(g_app.start_button, 36, 360, 135, 44);
    position_control(g_app.stop_button, 183, 360, 135, 44);
    position_control(g_app.setup_hint, 36, 421, main_width - 38, 54);
    position_control(g_app.developers_button, 36, 485, 336, 42);
    position_control(g_app.status_heading, 36, 548, main_width - 40, 30);
    position_control(g_app.status_label, 36, 583, main_width - 40, height - 603);

    side_x = main_right + 20;
    side_width = width - side_x - 20;
    position_control(g_app.advanced_group, side_x, 104, side_width, 30);
    position_control(g_app.buffer_label, side_x + 16, 141, 142, 27);
    position_control(g_app.buffer_edit, side_x + 16, 170, 86, 34);
    position_control(g_app.buffer_help_button, side_x + 108, 171, 32, 32);
    position_control(g_app.preroll_label, side_x + 165, 141, side_width - 185, 27);
    position_control(g_app.preroll_edit, side_x + 165, 170, 86, 34);
    position_control(g_app.preroll_help_button, side_x + 259, 171, 32, 32);
    position_control(g_app.gate_label, side_x + 16, 213, 126, 27);
    position_control(g_app.min_dbfs_edit, side_x + 16, 242, 86, 34);
    position_control(g_app.output_label, side_x + 165, 213, side_width - 185, 27);
    position_control(g_app.max_dbfs_edit, side_x + 165, 242, 86, 34);
    position_control(g_app.adaptive_checkbox, side_x + 16, 292, side_width - 30, 30);
    position_control(g_app.logs_label, side_x + 16, 340, side_width - 30, 28);
    position_control(g_app.log_label, side_x + 16, 376,
            side_width - 32, height - 396);
    RedrawWindow(g_app.window, NULL, NULL,
            RDW_INVALIDATE | RDW_ERASE | RDW_ALLCHILDREN);
}

static void draw_action_button(const DRAWITEMSTRUCT *item)
{
    RECT rect = item->rcItem;
    BOOL dark = dark_theme_active();
    BOOL help = item->CtlID >= ID_HELP_CONNECTION
            && item->CtlID <= ID_HELP_PREROLL;
    BOOL primary = item->CtlID == ID_START;
    BOOL disabled = (item->itemState & ODS_DISABLED) != 0;
    COLORREF background = dark ? RGB(17, 25, 39) : RGB(242, 246, 252);
    COLORREF fill = primary ? RGB(38, 101, 231)
            : dark ? RGB(43, 60, 82) : RGB(225, 235, 249);
    COLORREF foreground = primary ? RGB(255, 255, 255)
            : dark ? RGB(238, 244, 255) : RGB(28, 56, 98);
    HBRUSH background_brush = CreateSolidBrush(background);
    HBRUSH fill_brush = CreateSolidBrush(fill);
    HPEN border_pen = CreatePen(PS_SOLID, 1,
            primary ? RGB(38, 101, 231) : dark ? RGB(71, 93, 120)
            : RGB(193, 212, 238));
    HGDIOBJ old_brush, old_pen;
    WCHAR caption[128];
    FillRect(item->hDC, &rect, background_brush);
    old_brush = SelectObject(item->hDC, fill_brush);
    old_pen = SelectObject(item->hDC, border_pen);
    if (help) {
        Ellipse(item->hDC, rect.left + 1, rect.top + 1,
                rect.right - 1, rect.bottom - 1);
    } else {
        RoundRect(item->hDC, rect.left + 1, rect.top + 1,
                rect.right - 1, rect.bottom - 1, 14, 14);
    }
    SelectObject(item->hDC, old_pen);
    SelectObject(item->hDC, old_brush);
    GetWindowTextW(item->hwndItem, caption, ARRAYSIZE(caption));
    SetBkMode(item->hDC, TRANSPARENT);
    SetTextColor(item->hDC, disabled
            ? (dark ? RGB(141, 154, 172) : RGB(137, 150, 166))
            : foreground);
    DrawTextW(item->hDC, caption, -1, &rect,
            DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
    if ((item->itemState & ODS_FOCUS) != 0) {
        InflateRect(&rect, -4, -4);
        DrawFocusRect(item->hDC, &rect);
    }
    DeleteObject(border_pen);
    DeleteObject(fill_brush);
    DeleteObject(background_brush);
}

static BOOL add_tray_icon(void)
{
    NOTIFYICONDATAW icon;
    ZeroMemory(&icon, sizeof(icon));
    icon.cbSize = sizeof(icon);
    icon.hWnd = g_app.window;
    icon.uID = 1;
    icon.uFlags = NIF_MESSAGE | NIF_ICON | NIF_TIP;
    icon.uCallbackMessage = WM_TRAY_ICON;
    icon.hIcon = LoadIconW(GetModuleHandleW(NULL), MAKEINTRESOURCEW(IDI_ASHAOS));
    StringCchCopyW(icon.szTip, ARRAYSIZE(icon.szTip),
            tr(L"AshaOS — Windows audio", L"AshaOS — звук Windows"));
    g_app.tray_added = Shell_NotifyIconW(NIM_ADD, &icon);
    return g_app.tray_added;
}

static void remove_tray_icon(void)
{
    NOTIFYICONDATAW icon;
    if (!g_app.tray_added) {
        return;
    }
    ZeroMemory(&icon, sizeof(icon));
    icon.cbSize = sizeof(icon);
    icon.hWnd = g_app.window;
    icon.uID = 1;
    Shell_NotifyIconW(NIM_DELETE, &icon);
    g_app.tray_added = FALSE;
}

static void show_tray_menu(void)
{
    POINT point;
    HMENU menu = CreatePopupMenu();
    if (menu == NULL) {
        return;
    }
    AppendMenuW(menu, MF_STRING, ID_TRAY_OPEN,
            tr(L"Open AshaOS", L"Открыть AshaOS"));
    AppendMenuW(menu, MF_SEPARATOR, 0, NULL);
    AppendMenuW(menu, MF_STRING, ID_TRAY_EXIT,
            tr(L"Exit completely", L"Выйти полностью"));
    GetCursorPos(&point);
    SetForegroundWindow(g_app.window);
    TrackPopupMenu(menu, TPM_RIGHTBUTTON, point.x, point.y,
            0, g_app.window, NULL);
    PostMessageW(g_app.window, WM_NULL, 0, 0);
    DestroyMenu(menu);
}

static void refresh_ui_text(void)
{
    SendMessageW(g_app.theme_combo, CB_RESETCONTENT, 0, 0);
    SendMessageW(g_app.theme_combo, CB_ADDSTRING, 0,
            (LPARAM)tr(L"System", L"Системная"));
    SendMessageW(g_app.theme_combo, CB_ADDSTRING, 0,
            (LPARAM)tr(L"Light", L"Светлая"));
    SendMessageW(g_app.theme_combo, CB_ADDSTRING, 0,
            (LPARAM)tr(L"Dark", L"Тёмная"));
    SendMessageW(g_app.theme_combo, CB_SETCURSEL, g_app.ui_theme, 0);
    SendMessageW(g_app.language_combo, CB_RESETCONTENT, 0, 0);
    SendMessageW(g_app.language_combo, CB_ADDSTRING, 0, (LPARAM)L"English");
    SendMessageW(g_app.language_combo, CB_ADDSTRING, 0, (LPARAM)L"Русский");
    SendMessageW(g_app.language_combo, CB_SETCURSEL, g_app.ui_language, 0);
    SetWindowTextW(g_app.window, tr(
            L"AshaOS 1.7 · Windows audio", L"AshaOS 1.7 · звук Windows"));
    SetWindowTextW(g_app.heading_label, L"AshaOS 1.7");
    SetWindowTextW(g_app.subtitle_label, tr(
            L"Windows sound to your hearing aids",
            L"Звук Windows в слуховые аппараты"));
    SetWindowTextW(g_app.target_label, tr(
            L"Phone IPv4 address", L"IPv4-адрес телефона"));
    SetWindowTextW(g_app.port_label, tr(L"Port", L"Порт"));
    SetWindowTextW(g_app.appearance_label, tr(L"Appearance", L"Оформление"));
    SetWindowTextW(g_app.connection_group, tr(
            L"Connection", L"Подключение"));
    SetWindowTextW(g_app.advanced_group, tr(
            L"Advanced audio", L"Расширенные настройки звука"));
    SetWindowTextW(g_app.buffer_label, tr(
            L"Direct buffer, ms", L"Буфер Direct, мс"));
    SetWindowTextW(g_app.preroll_label, tr(
            L"Preroll, ms", L"Предбуфер, мс"));
    SetWindowTextW(g_app.gate_label, tr(
            L"Min dBFS", L"Мин. dBFS"));
    SetWindowTextW(g_app.output_label, tr(
            L"Max output, dBFS", L"Макс. громкость, dBFS"));
    SetWindowTextW(g_app.logs_label, tr(
            L"Logs and diagnostics", L"Журнал и диагностика"));
    SetWindowTextW(g_app.status_heading, tr(
            L"Connection status", L"Состояние подключения"));
    SetWindowTextW(g_app.setup_hint, tr(
            L"Choose the matching Direct USB, Wi-Fi or Bluetooth PAN mode on the phone, then copy its IP:port.",
            L"На телефоне выберите такой же Direct USB, Wi-Fi или Bluetooth PAN, затем скопируйте IP:порт."));
    SetWindowTextW(g_app.direct_checkbox, tr(
            L"AshaOS Direct · bypass Android AudioTrack",
            L"AshaOS Direct · без Android AudioTrack"));
    SetWindowTextW(g_app.direct_udp_checkbox, tr(
            L"Use UDP (unchecked: TCP)",
            L"Использовать UDP (без галочки: TCP)"));
    SetWindowTextW(g_app.keepalive_checkbox, tr(
            L"Keep Windows audio engine active",
            L"Поддерживать аудиодвижок Windows"));
    SetWindowTextW(g_app.adaptive_checkbox, tr(
            L"Adaptive buffer (+5 ms after underrun)",
            L"Адаптивный буфер (+5 мс при сбое)"));
    SetWindowTextW(g_app.start_button, tr(L"Start", L"Запустить"));
    SetWindowTextW(g_app.stop_button, tr(L"Stop", L"Остановить"));
    SetWindowTextW(g_app.developers_button, g_app.developers_visible
            ? tr(L"Hide For developers", L"Скрыть «Для разработчиков»")
            : tr(L"Show For developers", L"Показать «Для разработчиков»"));
    update_help_tool(g_app.direct_help_button);
    update_help_tool(g_app.keepalive_help_button);
    update_help_tool(g_app.buffer_help_button);
    update_help_tool(g_app.preroll_help_button);
    layout_controls();
}

static void set_developers_visible(BOOL visible)
{
    UINT index;
    RECT window_rect;
    int next_width;
    GetWindowRect(g_app.window, &window_rect);
    if (visible) {
        g_app.preferred_width = window_rect.right - window_rect.left;
        next_width = g_app.preferred_width + 420;
    } else {
        next_width = g_app.preferred_width > 0 ? g_app.preferred_width : 740;
    }
    g_app.developers_visible = visible;
    for (index = 0; index < g_app.dev_control_count; index++) {
        ShowWindow(g_app.dev_controls[index], visible ? SW_SHOW : SW_HIDE);
    }
    SetWindowPos(g_app.window, NULL, 0, 0,
            next_width, window_rect.bottom - window_rect.top,
            SWP_NOMOVE | SWP_NOZORDER);
    refresh_ui_text();
    update_status();
}

static LRESULT initialize_window(HWND window)
{
    WCHAR port_text[16], buffer_text[16], preroll_text[16];
    WCHAR min_dbfs_text[16], max_dbfs_text[16];
    DWORD label_style = WS_CHILD | WS_VISIBLE;
    DWORD button_style = WS_CHILD | WS_VISIBLE | WS_TABSTOP;
    DWORD edit_style = WS_CHILD | WS_VISIBLE | WS_TABSTOP | ES_AUTOHSCROLL;
    g_app.window = window;
    g_app.background_brush = CreateSolidBrush(RGB(242, 246, 252));
    g_app.surface_brush = CreateSolidBrush(RGB(255, 255, 255));
    g_app.ui_font = CreateFontW(-18, 0, 0, 0, FW_NORMAL, 0, 0, 0,
            DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
            CLEARTYPE_QUALITY, DEFAULT_PITCH, L"Segoe UI");
    g_app.heading_font = CreateFontW(-34, 0, 0, 0, FW_BOLD, 0, 0, 0,
            DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
            CLEARTYPE_QUALITY, DEFAULT_PITCH, L"Segoe UI");
    g_app.section_font = CreateFontW(-22, 0, 0, 0, FW_SEMIBOLD, 0, 0, 0,
            DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
            CLEARTYPE_QUALITY, DEFAULT_PITCH, L"Segoe UI");
    g_app.mono_font = CreateFontW(-15, 0, 0, 0, FW_NORMAL, 0, 0, 0,
            DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
            CLEARTYPE_QUALITY, FIXED_PITCH, L"Consolas");

    g_app.heading_label = make_ui_control(0, L"STATIC", L"AshaOS",
            label_style, 30, 16, 320, 42, window, 0, FALSE);
    SendMessageW(g_app.heading_label, WM_SETFONT,
            (WPARAM)g_app.heading_font, TRUE);
    g_app.subtitle_label = make_ui_control(0, L"STATIC", L"",
            label_style, 32, 57, 405, 27, window, 0, FALSE);
    g_app.appearance_label = make_ui_control(0, L"STATIC", L"",
            label_style, 476, 14, 200, 23, window, 0, FALSE);
    g_app.theme_combo = make_ui_control(0, L"COMBOBOX", L"",
            button_style | CBS_DROPDOWNLIST | WS_VSCROLL,
            476, 40, 105, 150, window, ID_THEME, FALSE);
    g_app.language_combo = make_ui_control(0, L"COMBOBOX", L"",
            button_style | CBS_DROPDOWNLIST | WS_VSCROLL,
            590, 40, 112, 120, window, ID_LANGUAGE, FALSE);
    g_app.connection_group = make_ui_control(0, L"STATIC", L"",
            label_style, 20, 100, 690, 30, window, 0, FALSE);
    SendMessageW(g_app.connection_group, WM_SETFONT,
            (WPARAM)g_app.section_font, TRUE);
    g_app.target_label = make_ui_control(0, L"STATIC", L"",
            label_style, 36, 130, 340, 22, window, 0, FALSE);
    g_app.port_label = make_ui_control(0, L"STATIC", L"",
            label_style, 410, 130, 140, 22, window, 0, FALSE);
    g_app.ip_edit = make_ui_control(WS_EX_CLIENTEDGE, L"EDIT", g_app.target_ip,
            edit_style, 36, 156, 358, 29, window, ID_IP, FALSE);
    StringCchPrintfW(port_text, ARRAYSIZE(port_text), L"%u", g_app.target_port);
    g_app.port_edit = make_ui_control(WS_EX_CLIENTEDGE, L"EDIT", port_text,
            edit_style | ES_NUMBER, 410, 156, 100, 29,
            window, ID_PORT, FALSE);
    g_app.direct_help_button = make_ui_control(0, L"BUTTON", L"?",
            button_style | BS_OWNERDRAW, 645, 198, 32, 32,
            window, ID_HELP_CONNECTION, FALSE);
    g_app.direct_checkbox = make_ui_control(0, L"BUTTON", L"",
            button_style | BS_AUTOCHECKBOX, 36, 203, 595, 26,
            window, ID_DIRECT, FALSE);
    SendMessageW(g_app.direct_checkbox, BM_SETCHECK,
            g_app.direct_enabled ? BST_CHECKED : BST_UNCHECKED, 0);
    g_app.direct_udp_checkbox = make_ui_control(0, L"BUTTON", L"",
            button_style | BS_AUTOCHECKBOX, 36, 241, 595, 26,
            window, ID_DIRECT_UDP, FALSE);
    SendMessageW(g_app.direct_udp_checkbox, BM_SETCHECK,
            g_app.direct_udp_enabled ? BST_CHECKED : BST_UNCHECKED, 0);
    EnableWindow(g_app.direct_udp_checkbox, g_app.direct_enabled);
    g_app.keepalive_help_button = make_ui_control(0, L"BUTTON", L"?",
            button_style | BS_OWNERDRAW, 645, 236, 32, 32,
            window, ID_HELP_KEEPALIVE, FALSE);
    g_app.keepalive_checkbox = make_ui_control(0, L"BUTTON", L"",
            button_style | BS_AUTOCHECKBOX, 36, 241, 595, 26,
            window, ID_KEEPALIVE, FALSE);
    SendMessageW(g_app.keepalive_checkbox, BM_SETCHECK,
            g_app.keepalive_enabled ? BST_CHECKED : BST_UNCHECKED, 0);
    g_app.start_button = make_ui_control(0, L"BUTTON", L"",
            button_style | BS_OWNERDRAW, 36, 284, 124, 39,
            window, ID_START, FALSE);
    g_app.stop_button = make_ui_control(0, L"BUTTON", L"",
            button_style | BS_OWNERDRAW, 172, 284, 124, 39,
            window, ID_STOP, FALSE);
    EnableWindow(g_app.stop_button, FALSE);
    g_app.setup_hint = make_ui_control(0, L"STATIC", L"",
            label_style, 36, 347, 650, 44, window, 0, FALSE);
    g_app.developers_button = make_ui_control(0, L"BUTTON", L"",
            button_style | BS_OWNERDRAW, 36, 396, 325, 37,
            window, ID_DEVELOPERS, FALSE);
    g_app.status_heading = make_ui_control(0, L"STATIC", L"",
            label_style, 36, 450, 650, 26, window, 0, FALSE);
    SendMessageW(g_app.status_heading, WM_SETFONT,
            (WPARAM)g_app.section_font, TRUE);
    g_app.status_label = make_ui_control(0, L"STATIC", L"",
            label_style | SS_LEFT, 36, 482, 650, 105,
            window, ID_STATUS, FALSE);

    g_app.advanced_group = make_ui_control(0, L"STATIC", L"",
            label_style, 730, 100, 398, 30,
            window, 0, TRUE);
    SendMessageW(g_app.advanced_group, WM_SETFONT,
            (WPARAM)g_app.section_font, TRUE);
    g_app.buffer_label = make_ui_control(0, L"STATIC", L"",
            label_style, 748, 130, 175, 22, window, 0, TRUE);
    StringCchPrintfW(buffer_text, ARRAYSIZE(buffer_text), L"%u",
            g_app.direct_buffer_ms);
    g_app.buffer_edit = make_ui_control(WS_EX_CLIENTEDGE, L"EDIT", buffer_text,
            edit_style | ES_NUMBER, 748, 156, 75, 28,
            window, ID_BUFFER_MS, TRUE);
    g_app.buffer_help_button = make_ui_control(0, L"BUTTON", L"?",
            button_style | BS_OWNERDRAW, 831, 155, 32, 32,
            window, ID_HELP_BUFFER, TRUE);
    g_app.preroll_label = make_ui_control(0, L"STATIC", L"",
            label_style, 895, 130, 170, 22, window, 0, TRUE);
    StringCchPrintfW(preroll_text, ARRAYSIZE(preroll_text), L"%u",
            g_app.sender_preroll_ms);
    g_app.preroll_edit = make_ui_control(WS_EX_CLIENTEDGE, L"EDIT", preroll_text,
            edit_style | ES_NUMBER, 895, 156, 75, 28,
            window, ID_PREROLL_MS, TRUE);
    g_app.preroll_help_button = make_ui_control(0, L"BUTTON", L"?",
            button_style | BS_OWNERDRAW, 992, 155, 32, 32,
            window, ID_HELP_PREROLL, TRUE);
    g_app.gate_label = make_ui_control(0, L"STATIC", L"",
            label_style, 748, 195, 130, 22, window, 0, TRUE);
    StringCchPrintfW(min_dbfs_text, ARRAYSIZE(min_dbfs_text), L"%d",
            (int)g_app.min_dbfs);
    g_app.min_dbfs_edit = make_ui_control(WS_EX_CLIENTEDGE, L"EDIT", min_dbfs_text,
            edit_style, 748, 221, 75, 28,
            window, ID_MIN_DBFS, TRUE);
    g_app.output_label = make_ui_control(0, L"STATIC", L"",
            label_style, 895, 195, 205, 22, window, 0, TRUE);
    StringCchPrintfW(max_dbfs_text, ARRAYSIZE(max_dbfs_text), L"%d",
            (int)g_app.max_dbfs);
    g_app.max_dbfs_edit = make_ui_control(WS_EX_CLIENTEDGE, L"EDIT", max_dbfs_text,
            edit_style, 895, 221, 75, 28,
            window, ID_MAX_DBFS, TRUE);
    g_app.adaptive_checkbox = make_ui_control(0, L"BUTTON", L"",
            button_style | BS_AUTOCHECKBOX, 748, 263, 355, 24,
            window, ID_ADAPTIVE, TRUE);
    SendMessageW(g_app.adaptive_checkbox, BM_SETCHECK,
            g_app.direct_adaptive_enabled ? BST_CHECKED : BST_UNCHECKED, 0);
    g_app.logs_label = make_ui_control(0, L"STATIC", L"",
            label_style, 748, 326, 350, 24, window, 0, TRUE);
    SendMessageW(g_app.logs_label, WM_SETFONT,
            (WPARAM)g_app.section_font, TRUE);
    g_app.log_label = make_ui_control(WS_EX_CLIENTEDGE, L"EDIT", L"",
            WS_CHILD | WS_VISIBLE | WS_VSCROLL | ES_LEFT | ES_MULTILINE
                    | ES_AUTOVSCROLL | ES_READONLY,
            748, 353, 360, 216, window, 0, TRUE);
    SendMessageW(g_app.log_label, WM_SETFONT, (WPARAM)g_app.mono_font, TRUE);
    g_app.tooltip_window = CreateWindowExW(
            WS_EX_TOPMOST | WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE,
            L"STATIC", L"", WS_POPUP | WS_BORDER | SS_LEFT | SS_NOPREFIX,
            0, 0, 0, 0,
            window, NULL, GetModuleHandleW(NULL), NULL);
    if (g_app.tooltip_window != NULL) {
        SendMessageW(g_app.tooltip_window, WM_SETFONT,
                (WPARAM)g_app.ui_font, FALSE);
        add_help_tool(g_app.direct_help_button);
        add_help_tool(g_app.keepalive_help_button);
        add_help_tool(g_app.buffer_help_button);
        add_help_tool(g_app.preroll_help_button);
    }
    refresh_ui_text();
    apply_theme();
    add_tray_icon();
    SetTimer(window, ID_TIMER, 1000, NULL);
    update_status();
    return 0;
}

static LRESULT CALLBACK window_proc(
        HWND window, UINT message, WPARAM wparam, LPARAM lparam)
{
    switch (message) {
        case WM_CREATE:
            return initialize_window(window);
        case WM_GETMINMAXINFO: {
            MINMAXINFO *limits = (MINMAXINFO *)lparam;
            limits->ptMinTrackSize.x = g_app.developers_visible ? 1140 : 740;
            limits->ptMinTrackSize.y = 700;
            return 0;
        }
        case WM_SIZE:
            if (wparam != SIZE_MINIMIZED) {
                layout_controls();
            }
            return 0;
        case WM_DRAWITEM:
            if (wparam == ID_START || wparam == ID_STOP
                    || wparam == ID_DEVELOPERS
                    || (wparam >= ID_HELP_CONNECTION
                            && wparam <= ID_HELP_PREROLL)) {
                draw_action_button((const DRAWITEMSTRUCT *)lparam);
                return TRUE;
            }
            break;
        case WM_TRAY_ICON:
            if (lparam == WM_LBUTTONDBLCLK) {
                ShowWindow(window, SW_RESTORE);
                SetForegroundWindow(window);
                return 0;
            }
            if (lparam == WM_RBUTTONUP || lparam == WM_CONTEXTMENU) {
                show_tray_menu();
                return 0;
            }
            break;
        case WM_CLOSE:
            if (!g_app.exit_requested && g_app.tray_added) {
                NOTIFYICONDATAW icon;
                ShowWindow(window, SW_HIDE);
                ZeroMemory(&icon, sizeof(icon));
                icon.cbSize = sizeof(icon);
                icon.hWnd = window;
                icon.uID = 1;
                icon.uFlags = NIF_INFO;
                StringCchCopyW(icon.szInfoTitle, ARRAYSIZE(icon.szInfoTitle),
                        L"AshaOS");
                StringCchCopyW(icon.szInfo, ARRAYSIZE(icon.szInfo),
                        tr(L"Still running. Open or exit from the tray icon.",
                           L"Работает в фоне. Откройте или завершите через значок в трее."));
                Shell_NotifyIconW(NIM_MODIFY, &icon);
                return 0;
            }
            DestroyWindow(window);
            return 0;
        case WM_COMMAND:
            if (LOWORD(wparam) == ID_TRAY_OPEN) {
                ShowWindow(window, SW_RESTORE);
                SetForegroundWindow(window);
                return 0;
            }
            if (LOWORD(wparam) == ID_TRAY_EXIT) {
                g_app.exit_requested = TRUE;
                DestroyWindow(window);
                return 0;
            }
            if (LOWORD(wparam) == ID_DEVELOPERS
                    && HIWORD(wparam) == BN_CLICKED) {
                set_developers_visible(!g_app.developers_visible);
                return 0;
            }
            if (LOWORD(wparam) == ID_THEME
                    && HIWORD(wparam) == CBN_SELCHANGE) {
                g_app.ui_theme = (int)SendMessageW(
                        g_app.theme_combo, CB_GETCURSEL, 0, 0);
                apply_theme();
                save_settings();
                return 0;
            }
            if (LOWORD(wparam) == ID_LANGUAGE
                    && HIWORD(wparam) == CBN_SELCHANGE) {
                g_app.ui_language = (int)SendMessageW(
                        g_app.language_combo, CB_GETCURSEL, 0, 0);
                refresh_ui_text();
                update_status();
                save_settings();
                return 0;
            }
            if (HIWORD(wparam) == BN_CLICKED
                    && LOWORD(wparam) >= ID_HELP_CONNECTION
                    && LOWORD(wparam) <= ID_HELP_PREROLL) {
                MessageBoxW(window, help_text(LOWORD(wparam)),
                        tr(L"How this works", L"Как это работает"),
                        MB_OK | MB_ICONINFORMATION);
                return 0;
            }
            if (LOWORD(wparam) == ID_START) {
                start_capture();
                return 0;
            }
            if (LOWORD(wparam) == ID_STOP) {
                stop_capture();
                return 0;
            }
            if (LOWORD(wparam) == ID_DIRECT && HIWORD(wparam) == BN_CLICKED) {
                g_app.direct_enabled = SendMessageW(
                        g_app.direct_checkbox, BM_GETCHECK, 0, 0) == BST_CHECKED;
                EnableWindow(g_app.ip_edit, TRUE);
                EnableWindow(g_app.port_edit, TRUE);
                EnableWindow(g_app.keepalive_checkbox, g_app.direct_enabled);
                EnableWindow(g_app.direct_udp_checkbox, g_app.direct_enabled);
                update_status();
                EnableWindow(g_app.buffer_edit, g_app.direct_enabled);
                EnableWindow(g_app.preroll_edit, g_app.direct_enabled);
                EnableWindow(g_app.min_dbfs_edit, g_app.direct_enabled);
                EnableWindow(g_app.max_dbfs_edit, g_app.direct_enabled);
                EnableWindow(g_app.adaptive_checkbox, g_app.direct_enabled);
                return 0;
            }
            if (LOWORD(wparam) == ID_DIRECT_UDP
                    && HIWORD(wparam) == BN_CLICKED) {
                g_app.direct_udp_enabled = SendMessageW(
                        g_app.direct_udp_checkbox,
                        BM_GETCHECK, 0, 0) == BST_CHECKED;
                update_status();
                return 0;
            }
            if (LOWORD(wparam) == ID_USB_TETHER
                    && HIWORD(wparam) == BN_CLICKED) {
                g_app.usb_tether_enabled = SendMessageW(
                        g_app.usb_tether_checkbox,
                        BM_GETCHECK, 0, 0) == BST_CHECKED;
                update_network_adb_buttons();
                update_status();
                return 0;
            }
            if (LOWORD(wparam) == ID_WIFI_ADB
                    && HIWORD(wparam) == BN_CLICKED) {
                connect_network_adb(AA_ADB_NETWORK_WIFI);
                return 0;
            }
            if (LOWORD(wparam) == ID_BLUETOOTH_PAN_ADB
                    && HIWORD(wparam) == BN_CLICKED) {
                connect_network_adb(AA_ADB_NETWORK_BLUETOOTH_PAN);
                return 0;
            }
            if (LOWORD(wparam) == ID_KEEPALIVE
                    && HIWORD(wparam) == BN_CLICKED) {
                g_app.keepalive_enabled = SendMessageW(
                        g_app.keepalive_checkbox,
                        BM_GETCHECK, 0, 0) == BST_CHECKED;
                update_status();
                return 0;
            }
            break;
        case WM_ERASEBKGND: {
            RECT rect;
            GetClientRect(window, &rect);
            FillRect((HDC)wparam, &rect, g_app.background_brush);
            return 1;
        }
        case WM_CTLCOLORSTATIC:
        case WM_CTLCOLORBTN: {
            HDC dc = (HDC)wparam;
            if ((HWND)lparam == g_app.tooltip_window) {
                SetTextColor(dc, dark_theme_active()
                        ? RGB(245, 248, 255) : RGB(35, 49, 70));
                SetBkColor(dc, dark_theme_active()
                        ? RGB(52, 68, 89) : RGB(255, 251, 226));
                return (LRESULT)g_app.tooltip_brush;
            }
            SetBkMode(dc, TRANSPARENT);
            SetTextColor(dc, dark_theme_active()
                    ? RGB(235, 243, 255) : RGB(28, 43, 64));
            return (LRESULT)g_app.background_brush;
        }
        case WM_CTLCOLOREDIT:
        case WM_CTLCOLORLISTBOX: {
            HDC dc = (HDC)wparam;
            SetTextColor(dc, dark_theme_active()
                    ? RGB(235, 243, 255) : RGB(28, 43, 64));
            SetBkColor(dc, dark_theme_active()
                    ? RGB(29, 41, 59) : RGB(255, 255, 255));
            return (LRESULT)g_app.surface_brush;
        }
        case WM_SETTINGCHANGE:
            if (g_app.ui_theme == 0) {
                apply_theme();
            }
            break;
        case WM_TIMER:
            if (wparam == ID_TIMER) {
                update_status();
                return 0;
            }
            break;
        case WM_CAPTURE_STOPPED:
            if (g_app.capture_thread != NULL
                    && WaitForSingleObject(g_app.capture_thread, 0) == WAIT_OBJECT_0) {
                CloseHandle(g_app.capture_thread);
                g_app.capture_thread = NULL;
            }
            EnableWindow(g_app.start_button, TRUE);
            EnableWindow(g_app.stop_button, FALSE);
            EnableWindow(g_app.ip_edit, TRUE);
            EnableWindow(g_app.port_edit, TRUE);
            EnableWindow(g_app.direct_checkbox, TRUE);
            EnableWindow(g_app.direct_udp_checkbox, g_app.direct_enabled);
            EnableWindow(g_app.usb_tether_checkbox, g_app.direct_enabled);
            EnableWindow(g_app.keepalive_checkbox, g_app.direct_enabled);
            update_status();
            EnableWindow(g_app.buffer_edit, g_app.direct_enabled);
            EnableWindow(g_app.preroll_edit, g_app.direct_enabled);
            EnableWindow(g_app.min_dbfs_edit, g_app.direct_enabled);
            EnableWindow(g_app.max_dbfs_edit, g_app.direct_enabled);
            EnableWindow(g_app.adaptive_checkbox, g_app.direct_enabled);
            return 0;
        case WM_DESTROY:
            KillTimer(window, ID_TIMER);
            stop_capture();
            save_settings();
            remove_tray_icon();
            DeleteObject(g_app.background_brush);
            DeleteObject(g_app.surface_brush);
            DeleteObject(g_app.tooltip_brush);
            DeleteObject(g_app.ui_font);
            DeleteObject(g_app.heading_font);
            DeleteObject(g_app.section_font);
            DeleteObject(g_app.mono_font);
            PostQuitMessage(0);
            return 0;
        default:
            break;
    }
    if (message == g_taskbar_created && g_taskbar_created != 0) {
        g_app.tray_added = FALSE;
        add_tray_icon();
        return 0;
    }
    return DefWindowProcW(window, message, wparam, lparam);
}

int WINAPI wWinMain(
        HINSTANCE instance, HINSTANCE previous, PWSTR command_line, int show_command)
{
    WNDCLASSW window_class;
    INITCOMMONCONTROLSEX controls;
    HWND window;
    MSG message;
    WSADATA winsock;
    (void)previous;
    (void)command_line;

    ZeroMemory(&g_app, sizeof(g_app));
    controls.dwSize = sizeof(controls);
    controls.dwICC = ICC_WIN95_CLASSES;
    InitCommonControlsEx(&controls);
    g_taskbar_created = RegisterWindowMessageW(L"TaskbarCreated");
    InitializeCriticalSection(&g_app.status_lock);
    load_settings();
    g_app.stop_event = CreateEventW(NULL, TRUE, FALSE, NULL);
    if (g_app.stop_event == NULL
            || WSAStartup(MAKEWORD(2, 2), &winsock) != 0) {
        MessageBoxW(NULL, L"Cannot initialize Windows networking.",
                L"Audio-Asha Sender", MB_OK | MB_ICONERROR);
        if (g_app.stop_event != NULL) {
            CloseHandle(g_app.stop_event);
        }
        DeleteCriticalSection(&g_app.status_lock);
        return 1;
    }

    ZeroMemory(&window_class, sizeof(window_class));
    window_class.lpfnWndProc = window_proc;
    window_class.hInstance = instance;
    window_class.hIcon = LoadIconW(instance, MAKEINTRESOURCEW(IDI_ASHAOS));
    window_class.lpszClassName = L"AshaOsAudioAshaSenderWindow";
    window_class.hCursor = LoadCursorW(NULL, IDC_ARROW);
    window_class.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
    if (RegisterClassW(&window_class) == 0) {
        MessageBoxW(NULL, L"Cannot register the sender window.",
                L"Audio-Asha Sender", MB_OK | MB_ICONERROR);
        WSACleanup();
        CloseHandle(g_app.stop_event);
        DeleteCriticalSection(&g_app.status_lock);
        return 1;
    }

    window = CreateWindowExW(0, window_class.lpszClassName,
            L"AshaOS 1.7 · Windows audio",
            WS_OVERLAPPEDWINDOW | WS_CLIPCHILDREN,
            CW_USEDEFAULT, CW_USEDEFAULT, 780, 700,
            NULL, NULL, instance, NULL);
    if (window == NULL) {
        WSACleanup();
        CloseHandle(g_app.stop_event);
        DeleteCriticalSection(&g_app.status_lock);
        return 1;
    }
    g_app.window = window;
    ShowWindow(window, show_command);
    apply_theme();
    UpdateWindow(window);
    while (GetMessageW(&message, NULL, 0, 0) > 0) {
        TranslateMessage(&message);
        DispatchMessageW(&message);
    }

    WSACleanup();
    CloseHandle(g_app.stop_event);
    DeleteCriticalSection(&g_app.status_lock);
    return (int)message.wParam;
}
