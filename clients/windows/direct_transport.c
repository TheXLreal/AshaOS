/*
 * Copyright (C) 2026 The AshaOS Project
 * Licensed under the Apache License, Version 2.0.
 */

#include "direct_transport.h"

#include <ws2tcpip.h>
#include <strsafe.h>

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define ADB_OUTPUT_BYTES (2u * 1024u * 1024u)
#define ADB_TIMEOUT_MS 10000u

static void set_message(
        WCHAR *destination,
        size_t destination_chars,
        const WCHAR *format,
        ...)
{
    va_list arguments;
    if (destination == NULL || destination_chars == 0u) {
        return;
    }
    va_start(arguments, format);
    StringCchVPrintfW(destination, destination_chars, format, arguments);
    va_end(arguments);
}

static BOOL file_exists(const WCHAR *path)
{
    DWORD attributes = GetFileAttributesW(path);
    return attributes != INVALID_FILE_ATTRIBUTES
            && (attributes & FILE_ATTRIBUTE_DIRECTORY) == 0u;
}

static BOOL find_adb(
        WCHAR *adb_path,
        size_t adb_path_chars,
        WCHAR *error,
        size_t error_chars)
{
    WCHAR module_path[MAX_PATH];
    WCHAR candidate[MAX_PATH];
    WCHAR *separator;
    DWORD length;

    length = GetModuleFileNameW(NULL, module_path, ARRAYSIZE(module_path));
    if (length > 0u && length < ARRAYSIZE(module_path)) {
        separator = wcsrchr(module_path, L'\\');
        if (separator != NULL) {
            *separator = L'\0';
            if (SUCCEEDED(StringCchPrintfW(candidate, ARRAYSIZE(candidate),
                    L"%s\\adb.exe", module_path))
                    && file_exists(candidate)) {
                return SUCCEEDED(StringCchCopyW(
                        adb_path, adb_path_chars, candidate));
            }
            if (SUCCEEDED(StringCchPrintfW(candidate, ARRAYSIZE(candidate),
                    L"%s\\..\\adb.exe", module_path))
                    && file_exists(candidate)) {
                return SUCCEEDED(StringCchCopyW(
                        adb_path, adb_path_chars, candidate));
            }
        }
    }

    if (file_exists(L"C:\\platform-tools\\adb.exe")) {
        return SUCCEEDED(StringCchCopyW(adb_path, adb_path_chars,
                L"C:\\platform-tools\\adb.exe"));
    }

    length = SearchPathW(NULL, L"adb.exe", NULL, (DWORD)adb_path_chars,
            adb_path, NULL);
    if (length > 0u && length < adb_path_chars) {
        return TRUE;
    }

    set_message(error, error_chars,
            L"adb.exe not found. Put the sender beside scrcpy's adb.exe "
            L"or install it in C:\\platform-tools.");
    return FALSE;
}

static BOOL run_adb_capture(
        const WCHAR *adb_path,
        const WCHAR *arguments,
        char *output,
        DWORD output_capacity,
        DWORD *exit_code,
        WCHAR *error,
        size_t error_chars)
{
    SECURITY_ATTRIBUTES security;
    STARTUPINFOW startup;
    PROCESS_INFORMATION process;
    HANDLE pipe_read = NULL;
    HANDLE pipe_write = NULL;
    WCHAR command_line[2048];
    DWORD output_bytes = 0u;
    ULONGLONG deadline;
    BOOL process_finished = FALSE;
    BOOL success = FALSE;

    if (output != NULL && output_capacity != 0u) {
        output[0] = '\0';
    }
    if (exit_code != NULL) {
        *exit_code = (DWORD)-1;
    }

    ZeroMemory(&security, sizeof(security));
    security.nLength = sizeof(security);
    security.bInheritHandle = TRUE;
    if (!CreatePipe(&pipe_read, &pipe_write, &security, 0)
            || !SetHandleInformation(pipe_read, HANDLE_FLAG_INHERIT, 0)) {
        set_message(error, error_chars,
                L"Cannot create adb output pipe: Windows error %lu",
                GetLastError());
        goto cleanup;
    }

    if (FAILED(StringCchPrintfW(command_line, ARRAYSIZE(command_line),
            L"\"%s\" %s", adb_path, arguments))) {
        set_message(error, error_chars, L"adb command is too long");
        goto cleanup;
    }

    ZeroMemory(&startup, sizeof(startup));
    startup.cb = sizeof(startup);
    startup.dwFlags = STARTF_USESTDHANDLES | STARTF_USESHOWWINDOW;
    startup.wShowWindow = SW_HIDE;
    startup.hStdInput = GetStdHandle(STD_INPUT_HANDLE);
    startup.hStdOutput = pipe_write;
    startup.hStdError = pipe_write;
    ZeroMemory(&process, sizeof(process));

    if (!CreateProcessW(NULL, command_line, NULL, NULL, TRUE,
            CREATE_NO_WINDOW, NULL, NULL, &startup, &process)) {
        set_message(error, error_chars,
                L"Cannot start adb.exe: Windows error %lu", GetLastError());
        goto cleanup;
    }
    CloseHandle(pipe_write);
    pipe_write = NULL;

    deadline = GetTickCount64() + ADB_TIMEOUT_MS;
    while (!process_finished) {
        DWORD available = 0u;
        if (!PeekNamedPipe(pipe_read, NULL, 0, NULL, &available, NULL)) {
            DWORD pipe_error = GetLastError();
            if (pipe_error == ERROR_BROKEN_PIPE) {
                process_finished = TRUE;
                continue;
            }
            set_message(error, error_chars,
                    L"Cannot read adb output: Windows error %lu", pipe_error);
            goto process_cleanup;
        }
        if (available != 0u) {
            char discard[4096];
            char *destination = discard;
            DWORD capacity = sizeof(discard);
            DWORD read_bytes = 0u;
            if (output != NULL && output_capacity > output_bytes + 1u) {
                destination = output + output_bytes;
                capacity = output_capacity - output_bytes - 1u;
                if (capacity > available) {
                    capacity = available;
                }
            } else if (capacity > available) {
                capacity = available;
            }
            if (!ReadFile(pipe_read, destination, capacity, &read_bytes, NULL)) {
                set_message(error, error_chars,
                        L"Cannot read adb output: Windows error %lu",
                        GetLastError());
                goto process_cleanup;
            }
            if (destination != discard) {
                output_bytes += read_bytes;
                output[output_bytes] = '\0';
            }
            continue;
        }

        if (WaitForSingleObject(process.hProcess, 10) == WAIT_OBJECT_0) {
            process_finished = TRUE;
            continue;
        }
        if (GetTickCount64() >= deadline) {
            TerminateProcess(process.hProcess, ERROR_TIMEOUT);
            WaitForSingleObject(process.hProcess, 1000);
            set_message(error, error_chars,
                    L"adb command timed out after %u ms", ADB_TIMEOUT_MS);
            goto process_cleanup;
        }
    }

    for (;;) {
        DWORD available = 0u;
        DWORD read_bytes = 0u;
        char discard[4096];
        char *destination = discard;
        DWORD capacity = sizeof(discard);
        if (!PeekNamedPipe(pipe_read, NULL, 0, NULL, &available, NULL)
                || available == 0u) {
            break;
        }
        if (output != NULL && output_capacity > output_bytes + 1u) {
            destination = output + output_bytes;
            capacity = output_capacity - output_bytes - 1u;
            if (capacity > available) {
                capacity = available;
            }
        } else if (capacity > available) {
            capacity = available;
        }
        if (!ReadFile(pipe_read, destination, capacity, &read_bytes, NULL)) {
            break;
        }
        if (destination != discard) {
            output_bytes += read_bytes;
            output[output_bytes] = '\0';
        }
    }

    if (!GetExitCodeProcess(process.hProcess, exit_code)) {
        set_message(error, error_chars,
                L"Cannot read adb exit code: Windows error %lu", GetLastError());
        goto process_cleanup;
    }
    success = TRUE;

process_cleanup:
    CloseHandle(process.hThread);
    CloseHandle(process.hProcess);

cleanup:
    if (pipe_write != NULL) {
        CloseHandle(pipe_write);
    }
    if (pipe_read != NULL) {
        CloseHandle(pipe_read);
    }
    return success;
}

static BOOL parse_first_ipv4(
        const char *output,
        char *address,
        size_t address_bytes)
{
    const char *start = strstr(output, " inet ");
    const char *end;
    size_t length;
    struct in_addr parsed;

    if (start == NULL) {
        return FALSE;
    }
    start += strlen(" inet ");
    end = strchr(start, '/');
    if (end == NULL) {
        return FALSE;
    }
    length = (size_t)(end - start);
    if (length == 0u || length >= address_bytes) {
        return FALSE;
    }
    memcpy(address, start, length);
    address[length] = '\0';
    return InetPtonA(AF_INET, address, &parsed) == 1;
}

BOOL aa_adb_connect_network(
        aa_adb_network_kind kind,
        WCHAR *endpoint,
        size_t endpoint_chars,
        WCHAR *error,
        size_t error_chars)
{
    WCHAR adb_path[MAX_PATH];
    WCHAR arguments[512];
    WCHAR address_wide[INET_ADDRSTRLEN];
    const WCHAR *interface_command;
    char output[4096];
    char address[INET_ADDRSTRLEN];
    char *port_end;
    unsigned long port;
    DWORD exit_code = (DWORD)-1;

    if (endpoint == NULL || endpoint_chars == 0u) {
        set_message(error, error_chars, L"Network endpoint buffer is invalid");
        return FALSE;
    }
    endpoint[0] = L'\0';
    SetEnvironmentVariableW(L"ANDROID_SERIAL", NULL);
    if (!find_adb(adb_path, ARRAYSIZE(adb_path), error, error_chars)) {
        return FALSE;
    }
    if (!run_adb_capture(adb_path,
            L"-d shell getprop service.adb.tls.port",
            output, sizeof(output), &exit_code, error, error_chars)
            || exit_code != 0u) {
        set_message(error, error_chars,
                L"Cannot query the Pixel over USB ADB. Keep one authorized "
                L"USB device connected and unlocked.");
        return FALSE;
    }
    port = strtoul(output, &port_end, 10);
    if (port_end == output || port == 0ul || port > 65535ul) {
        set_message(error, error_chars,
                L"Wireless debugging is off. Enable Developer options > "
                L"Wireless debugging on the Pixel, then press this button again.");
        return FALSE;
    }

    interface_command = kind == AA_ADB_NETWORK_WIFI
            ? L"-d shell ip -4 -o addr show wlan0"
            : L"-d shell ip -4 -o addr show bt-pan";
    if (!run_adb_capture(adb_path, interface_command,
            output, sizeof(output), &exit_code, error, error_chars)
            || exit_code != 0u
            || !parse_first_ipv4(output, address, sizeof(address))) {
        if (kind == AA_ADB_NETWORK_BLUETOOTH_PAN
                && run_adb_capture(adb_path,
                        L"-d shell ip -4 -o addr show bnep0",
                        output, sizeof(output), &exit_code, error, error_chars)
                && exit_code == 0u
                && parse_first_ipv4(output, address, sizeof(address))) {
            /* Some kernels expose the PAN as bnep0 instead of bt-pan. */
        } else {
            set_message(error, error_chars,
                    kind == AA_ADB_NETWORK_WIFI
                            ? L"Pixel Wi-Fi has no IPv4 address. Connect it to "
                              L"the same router as this PC."
                            : L"Bluetooth PAN has no IPv4 address. Enable "
                              L"Bluetooth tethering and connect Windows to the "
                              L"Pixel Access point first.");
            return FALSE;
        }
    }
    if (MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, address, -1,
            address_wide, ARRAYSIZE(address_wide)) == 0
            || FAILED(StringCchPrintfW(endpoint, endpoint_chars,
                    L"%s:%lu", address_wide, port))
            || FAILED(StringCchPrintfW(arguments, ARRAYSIZE(arguments),
                    L"connect %s", endpoint))) {
        set_message(error, error_chars, L"Cannot format the ADB network endpoint");
        endpoint[0] = L'\0';
        return FALSE;
    }
    if (!run_adb_capture(adb_path, arguments,
            output, sizeof(output), &exit_code, error, error_chars)
            || exit_code != 0u
            || strstr(output, "failed") != NULL
            || strstr(output, "cannot") != NULL) {
        set_message(error, error_chars,
                L"ADB could not connect to %s. For the first connection, tap "
                L"Pair device with pairing code on the Pixel and run adb pair "
                L"once; later this button connects automatically.", endpoint);
        endpoint[0] = L'\0';
        return FALSE;
    }
    SetEnvironmentVariableW(L"ANDROID_SERIAL", endpoint);
    return TRUE;
}

static int hex_value(char value)
{
    if (value >= '0' && value <= '9') {
        return value - '0';
    }
    if (value >= 'a' && value <= 'f') {
        return value - 'a' + 10;
    }
    if (value >= 'A' && value <= 'F') {
        return value - 'A' + 10;
    }
    return -1;
}

static BOOL parse_session_nonce(
        const char *dumpsys,
        uint8_t *nonce,
        WCHAR *error,
        size_t error_chars)
{
    const char *marker = "Session nonce";
    const char *cursor = strstr(dumpsys, marker);
    BOOL any_nonzero = FALSE;
    size_t index;

    if (cursor == NULL) {
        set_message(error, error_chars,
                L"AshaOS direct-audio nonce is absent. Install the direct-path "
                L"system build, connect the hearing device, then press Start "
                L"in Audio-Asha direct mode.");
        return FALSE;
    }
    cursor = strchr(cursor, ':');
    if (cursor == NULL) {
        set_message(error, error_chars, L"Malformed AshaOS nonce diagnostics");
        return FALSE;
    }
    cursor++;
    while (*cursor == ' ' || *cursor == '\t') {
        cursor++;
    }

    for (index = 0u; index < AA_DIRECT_NONCE_BYTES; index++) {
        int high = hex_value(cursor[index * 2u]);
        int low = hex_value(cursor[index * 2u + 1u]);
        if (high < 0 || low < 0) {
            set_message(error, error_chars,
                    L"Malformed AshaOS direct-audio session nonce");
            return FALSE;
        }
        nonce[index] = (uint8_t)((high << 4) | low);
        any_nonzero = any_nonzero || nonce[index] != 0u;
    }
    if (!any_nonzero) {
        set_message(error, error_chars,
                L"Direct ASHA session is not active. Connect the hearing "
                L"device and press Start in Audio-Asha direct mode first.");
        return FALSE;
    }
    return TRUE;
}
static void remove_forward(aa_direct_connection *connection)
{
    char output[4096];
    DWORD exit_code;
    WCHAR ignored[128];

    if (!connection->forward_active || connection->adb_path[0] == L'\0') {
        return;
    }
    run_adb_capture(connection->adb_path,
            L"forward --remove tcp:48101",
            output, sizeof(output), &exit_code, ignored, ARRAYSIZE(ignored));
    connection->forward_active = FALSE;
}

BOOL aa_direct_open(
        aa_direct_connection *connection,
        const WCHAR *target_ip,
        USHORT target_port,
        BOOL use_udp,
        WCHAR *error,
        size_t error_chars)
{
    char *dumpsys = NULL;
    DWORD exit_code = (DWORD)-1;
    struct sockaddr_in destination;
    int enabled = 1;
    int timeout_ms = 1000;

    ZeroMemory(connection, sizeof(*connection));
    connection->socket_handle = INVALID_SOCKET;
    connection->datagram = use_udp;
    ZeroMemory(&destination, sizeof(destination));
    destination.sin_family = AF_INET;
    destination.sin_port = htons(target_port);
    if (target_ip == NULL || target_ip[0] == L'\0' || target_port == 0u
            || InetPtonW(AF_INET, target_ip, &destination.sin_addr) != 1) {
        set_message(error, error_chars,
                L"Enter the IPv4 address and port shown by the phone.");
        return FALSE;
    }
    if (!find_adb(connection->adb_path, ARRAYSIZE(connection->adb_path),
            error, error_chars)) {
        return FALSE;
    }



    dumpsys = (char *)HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY,
            ADB_OUTPUT_BYTES);
    if (dumpsys == NULL) {
        set_message(error, error_chars, L"Cannot allocate adb diagnostics buffer");
        goto failure;
    }
    if (!run_adb_capture(connection->adb_path,
            L"shell dumpsys bluetooth_manager",
            dumpsys, ADB_OUTPUT_BYTES, &exit_code, error, error_chars)) {
        WCHAR cause[256];
        StringCchCopyW(cause, ARRAYSIZE(cause), error);
        set_message(error, error_chars,
                L"Cannot read AshaOS Bluetooth state through ADB: %ls. "
                L"Connect the USB cable, enable USB debugging, and allow this PC.",
                cause);
        goto failure;
    }
    if (exit_code != 0u) {
        set_message(error, error_chars,
                L"ADB could not read AshaOS Bluetooth state (exit %lu). "
                L"Run adb devices: the phone must show as device, not "
                L"unauthorized. Reconnect USB and allow debugging on the phone.",
                exit_code);
        goto failure;
    }
    if (!parse_session_nonce(dumpsys, connection->session_nonce,
            error, error_chars)) {
        goto failure;
    }
    connection->usb_tether_active = TRUE;
    HeapFree(GetProcessHeap(), 0, dumpsys);
    dumpsys = NULL;

    connection->socket_handle = socket(AF_INET,
            use_udp ? SOCK_DGRAM : SOCK_STREAM,
            use_udp ? IPPROTO_UDP : IPPROTO_TCP);
    if (connection->socket_handle == INVALID_SOCKET) {
        set_message(error, error_chars,
                L"Cannot create Direct %ls socket: Winsock error %d",
                use_udp ? L"UDP" : L"TCP",
                WSAGetLastError());
        goto failure;
    }
    if (!use_udp) {
        setsockopt(connection->socket_handle, IPPROTO_TCP, TCP_NODELAY,
                (const char *)&enabled, sizeof(enabled));
    }
    setsockopt(connection->socket_handle, SOL_SOCKET, SO_SNDTIMEO,
            (const char *)&timeout_ms, sizeof(timeout_ms));

    if (connect(connection->socket_handle,
            (const struct sockaddr *)&destination,
            sizeof(destination)) == SOCKET_ERROR) {
        set_message(error, error_chars,
                L"Cannot connect to AshaOS Direct %ls at %ls:%u: Winsock error %d. "
                L"Select the matching Direct mode on the phone and check its network.",
                use_udp ? L"UDP" : L"TCP", target_ip,
                (unsigned int)target_port, WSAGetLastError());
        goto failure;
    }
    return TRUE;

failure:
    if (dumpsys != NULL) {
        HeapFree(GetProcessHeap(), 0, dumpsys);
    }
    aa_direct_close(connection);
    return FALSE;
}

static BOOL send_record(
        aa_direct_connection *connection,
        const BYTE *record,
        size_t record_bytes,
        const WCHAR *record_name,
        WCHAR *error,
        size_t error_chars)
{
    size_t sent_bytes = 0u;

    if (connection->datagram) {
        int sent = send(connection->socket_handle,
                (const char *)record, (int)record_bytes, 0);
        if (sent == SOCKET_ERROR || (size_t)sent != record_bytes) {
            set_message(error, error_chars,
                    L"Direct UDP %ls send failed: Winsock error %d",
                    record_name, WSAGetLastError());
            return FALSE;
        }
        return TRUE;
    }

    while (sent_bytes < record_bytes) {
        int sent = send(connection->socket_handle,
                (const char *)record + sent_bytes,
                (int)(record_bytes - sent_bytes), 0);
        if (sent == SOCKET_ERROR || sent == 0) {
            set_message(error, error_chars,
                    L"Direct TCP %ls send failed: Winsock error %d",
                    record_name, WSAGetLastError());
            return FALSE;
        }
        sent_bytes += (size_t)sent;
    }
    return TRUE;
}

BOOL aa_direct_send_pcm(
        aa_direct_connection *connection,
        uint32_t sequence,
        const int16_t *pcm,
        WCHAR *error,
        size_t error_chars)
{
    BYTE record[AA_DIRECT_RECORD_BYTES];
    aa_direct_packet_header header;

    header.magic = AA_DIRECT_MAGIC;
    header.protocol_version = AA_DIRECT_PROTOCOL_VERSION;
    header.header_bytes = AA_DIRECT_HEADER_BYTES;
    header.sequence = sequence;
    memcpy(header.session_nonce, connection->session_nonce,
            sizeof(header.session_nonce));
    header.frame_count = AA_DIRECT_FRAMES_PER_RECORD;
    header.channels = AA_CHANNELS;
    header.reserved = AA_DIRECT_RECORD_TYPE_PCM;
    memcpy(record, &header, sizeof(header));
    memcpy(record + AA_DIRECT_HEADER_BYTES, pcm, AA_DIRECT_PCM_BYTES);

    return send_record(connection, record, sizeof(record), L"audio",
            error, error_chars);
}

BOOL aa_direct_send_config(
        aa_direct_connection *connection,
        uint16_t buffer_ms,
        BOOL adaptive_enabled,
        WCHAR *error,
        size_t error_chars)
{
    BYTE record[AA_DIRECT_RECORD_BYTES];
    aa_direct_packet_header header;
    aa_direct_config config;

    if (buffer_ms < AA_DIRECT_MIN_BUFFER_MS
            || buffer_ms > AA_DIRECT_MAX_BUFFER_MS
            || buffer_ms % AA_DIRECT_BUFFER_STEP_MS != 0u) {
        set_message(error, error_chars,
                L"Direct buffer must be 1-300 ms in 1 ms steps");
        return FALSE;
    }

    ZeroMemory(record, sizeof(record));
    ZeroMemory(&config, sizeof(config));
    header.magic = AA_DIRECT_MAGIC;
    header.protocol_version = AA_DIRECT_PROTOCOL_VERSION;
    header.header_bytes = AA_DIRECT_HEADER_BYTES;
    header.sequence = 0u;
    memcpy(header.session_nonce, connection->session_nonce,
            sizeof(header.session_nonce));
    header.frame_count = 0u;
    header.channels = 0u;
    header.reserved = AA_DIRECT_RECORD_TYPE_CONFIG;
    config.buffer_ms = buffer_ms;
    config.adaptive_enabled = adaptive_enabled ? 1u : 0u;
    memcpy(record, &header, sizeof(header));
    memcpy(record + AA_DIRECT_HEADER_BYTES, &config, sizeof(config));

    return send_record(connection, record, sizeof(record), L"configuration",
            error, error_chars);
}

void aa_direct_close(aa_direct_connection *connection)
{
    if (connection->socket_handle != INVALID_SOCKET) {
        if (!connection->datagram) {
            shutdown(connection->socket_handle, SD_SEND);
        }
        closesocket(connection->socket_handle);
        connection->socket_handle = INVALID_SOCKET;
    }
    remove_forward(connection);
    SecureZeroMemory(connection->session_nonce,
            sizeof(connection->session_nonce));
    connection->usb_tether_active = FALSE;
    connection->network_adb_active = FALSE;
}
