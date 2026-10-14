#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include "media_lib_os.h"

/* ADF's prebuilt RTSP server sends FU-A but omits fmtp (RFC 6184 defaults
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
