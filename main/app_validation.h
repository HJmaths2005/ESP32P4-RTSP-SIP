#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

static inline bool app_wifi_credentials_valid(const char *ssid, const char *password)
{
    if (!ssid || !ssid[0] || strlen(ssid) > 32) return false;
    size_t n = password ? strlen(password) : 0;
    if (n == 0 || (n >= 8 && n <= 63)) return true;
    if (n != 64) return false;
    for (size_t i = 0; i < n; ++i) {
        char c = password[i];
        if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F'))) return false;
    }
    return true;
}

/* Percent-encode SIP userinfo so separators in passwords cannot change the URI. */
static inline bool app_uri_escape(const char *src, char *dst, size_t capacity)
{
    static const char hex[] = "0123456789ABCDEF";
    if (!src || !dst || !capacity) return false;
    size_t used = 0;
    for (size_t i = 0; src[i]; ++i) {
        unsigned char c = (unsigned char)src[i];
        bool plain = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
                     (c >= '0' && c <= '9') || c == '-' || c == '_' || c == '.' || c == '~';
        size_t count = plain ? 1 : 3;
        if (used + count >= capacity) { dst[0] = '\0'; return false; }
        if (plain) dst[used++] = (char)c;
        else { dst[used++] = '%'; dst[used++] = hex[c >> 4]; dst[used++] = hex[c & 15]; }
    }
    dst[used] = '\0';
    return true;
}

static inline bool app_sip_host_valid(const char *host)
{
    if (!host || !host[0] || strlen(host) > 63) return false;
    for (size_t i = 0; host[i]; ++i) {
        char c = host[i];
        if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
              (c >= '0' && c <= '9') || c == '-' || c == '.')) return false;
    }
    return true; /* IPv4 address or DNS name; port is fixed to 5060. */
}
