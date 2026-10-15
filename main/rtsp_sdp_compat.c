#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include "media_lib_os.h"

/* The prebuilt SIP/RTSP stack sends FU-A but omits fmtp (RFC 6184 defaults
 * to single-NAL mode). Match only its H.264 media template. The caller uses
 * this function's length to generate Content-Length, so it stays correct.
 * Keep the library allocator: the server releases strings with media_lib_free.
 */
int __wrap_media_lib_asprintf(char **str, const char *fmt, ...)
{
    static const char original[] =
        "m=video %d RTP/AVP%s96\r\n"
        "a=rtpmap:96 H264/90000\r\n"
        "a=control:track%d\r\n";
    static const char corrected[] =
        "m=video %d RTP/AVP%s96\r\n"
        "a=rtpmap:96 H264/90000\r\n"
        "a=fmtp:96 packetization-mode=1\r\n"
        "a=control:track%d\r\n";
    if (!str || !fmt) return -1;
    *str = NULL;
    if (strcmp(fmt, original) == 0) fmt = corrected;
    va_list args, count_args;
    va_start(args, fmt);
    va_copy(count_args, args);
    int length = vsnprintf(NULL, 0, fmt, count_args);
    va_end(count_args);
    if (length < 0) { va_end(args); return -1; }
    char *buffer = media_lib_malloc((size_t)length + 1);
    if (!buffer) { va_end(args); return -1; }
    int written = vsnprintf(buffer, (size_t)length + 1, fmt, args);
    va_end(args);
    if (written != length) { media_lib_free(buffer); return -1; }
    *str = buffer;
    return written;
}

/* SIP 0.6.1 builds SDP through snprintf, not media_lib_asprintf. Its audio
 * and H.264 templates are formatted separately into a 1024-byte allocation.
 * Preserve snprintf's return/truncation semantics and touch only this exact
 * H.264 template, so SIP Content-Length includes the added attributes. */
int __wrap_snprintf(char *str, size_t capacity, const char *fmt, ...)
{
    static const char sip_original[] =
        "m=video %d RTP/AVP 96\r\n"
        "a=rtpmap:96 H264/90000\r\n";
    static const char sip_corrected[] =
        "m=video %d RTP/AVP 96\r\n"
        "a=rtpmap:96 H264/90000\r\n"
        "a=fmtp:96 packetization-mode=1\r\n"
        "a=sendonly\r\n";
    if (!fmt || (!str && capacity)) return -1;
    if (strcmp(fmt, sip_original) == 0) fmt = sip_corrected;
    va_list args;
    va_start(args, fmt);
    int written = vsnprintf(str, capacity, fmt, args);
    va_end(args);
    return written;
}
