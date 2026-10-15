"""Compile production SIP/video functions with mocked hardware boundaries."""
import os
from pathlib import Path
import re
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]


def run_c(source, headers=None):
    include = os.environ.get("ESP_RTC_INCLUDE_DIR", str(ROOT / "managed_components/espressif__esp_media_protocols/include"))
    with tempfile.TemporaryDirectory() as directory:
        path = Path(directory)
        for name, contents in (headers or {}).items():
            (path / name).write_text(contents)
        (path / "check.c").write_text(source)
        subprocess.run([os.environ.get("CC", "cc"), "-std=c11", "-Wall", "-Wextra",
                        "-Wno-unused-parameter", "-I", str(path), "-I", include,
                        str(path / "check.c"), "-o", str(path / "check")], check=True)
        subprocess.run([str(path / "check")], check=True)


class SipVideoTests(unittest.TestCase):
    def test_mode_configuration_and_negotiated_video_callback(self):
        source = (ROOT / "main/VoIP_manip.c").read_text()
        source = re.sub(r'^#include[^\n]*\n', '', source, flags=re.M)
        mocks = r'''
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include <strings.h>
#include <stdatomic.h>
#include "esp_rtc.h"
typedef int esp_err_t;
#define ESP_OK 0
#define ESP_ERR_INVALID_ARG 1
#define ESP_ERR_NOT_FOUND 2
#define ESP_CODEC_DEV_OK 0
#define PCM_FRAME_SAMPLES 160
#define CAMERA_H_RES 640
#define CAMERA_V_RES 480
#define JPEG_BUF_SIZE (640 * 1024)
#define CONFIG_APP_VIDEO_CALL_FPS 15
#define ESP_LOGI(tag, ...) ((void)(tag))
static void *g_codec_dev = (void *)1;
static bool h264 = true, connected = true, fail_init;
static int frames, deinitializations, init_count, frame_result;
static esp_rtc_config_t captured;
static bool wifi_is_connected(void) { return connected; }
static bool wifi_get_ip(char *p, size_t n) { if (!connected) return false; strncpy(p,"192.168.1.2",n); return true; }
static bool camera_uses_h264(void) { return h264; }
static int media_lib_add_default_adapter(void) { return 0; }
static int audio_read_mic(void *p, int n) { memset(p,0,n); return 0; }
static int audio_write_spk(const void *p, int n) { return 0; }
static unsigned char esp_g711a_encode(int16_t s) { return 0; }
static int16_t esp_g711a_decode(unsigned char s) { return 0; }
static esp_err_t camera_copy_encoded_frame(uint8_t *p, size_t capacity, size_t *n, uint32_t *seq) {
    assert(capacity == JPEG_BUF_SIZE);
    ++frames;
    if (frame_result) return frame_result;
    memcpy(p,"NAL!",4); *n=4; ++*seq; return ESP_OK;
}
esp_rtc_handle_t esp_rtc_service_init(esp_rtc_config_t *config) {
    ++init_count; captured=*config;
    return fail_init ? NULL : (esp_rtc_handle_t)(uintptr_t)1;
}
int esp_rtc_service_deinit(esp_rtc_handle_t handle) { ++deinitializations; return ESP_OK; }
esp_rtc_handle_t sip_service_start_mode(const char *, bool);
'''
        checks = r'''
int main(void) {
    unsigned char data[8]; unsigned int len=0;
    assert(sip_service_start("udp://103:secret@192.168.1.2:5060"));
    assert(!captured.vcodec_info && !captured.data_cb->send_video && !captured.data_cb->receive_video);
    assert(captured.acodec_type == RTC_ACODEC_G711A && !captured.p2p_mode);
    assert(!sip_service_has_video());
    assert(sip_service_stop((esp_rtc_handle_t)(uintptr_t)1) == ESP_OK);
    assert(sip_service_start_mode("udp://103:secret@192.168.1.2:5060", true));
    assert(captured.vcodec_info->vcodec == RTC_VCODEC_H264);
    assert(captured.vcodec_info->width == 640 && captured.vcodec_info->height == 480);
    assert(captured.vcodec_info->fps == 15 && captured.vcodec_info->len == JPEG_BUF_SIZE);
    assert(captured.data_cb->send_audio && captured.data_cb->receive_audio);
    assert(sip_service_has_video() && !sip_service_video_active());
    assert(captured.data_cb->send_video(data,&len,NULL) == ESP_OK && len == 0 && frames == 0);
    _esp_sip_event_handler(ESP_RTC_EVENT_VIDEO_SESSION_BEGIN,NULL);
    /* SDK supplies len=0: it is an output parameter, not destination capacity. */
    assert(captured.data_cb->send_video(data,&len,NULL) == ESP_OK && len == 4 && frames == 1);
    assert(memcmp(data,"NAL!",4) == 0);
    frame_result=ESP_ERR_NOT_FOUND; len=99;
    captured.data_cb->send_video(data,&len,NULL);
    assert(len == 0);
    _esp_sip_event_handler(ESP_RTC_EVENT_HANGUP,NULL);
    assert(!sip_service_in_call() && !sip_service_video_active());
    len=99; captured.data_cb->send_video(data,&len,NULL); assert(len == 0 && frames == 2);
    assert(sip_service_stop((esp_rtc_handle_t)(uintptr_t)1) == ESP_OK);
    assert(!sip_service_has_video());
    h264=false;
    assert(!sip_service_start_mode("uri",true) && init_count == 2);
    assert(sip_service_start("uri")); /* Audio still works with an MJPEG camera. */
    sip_service_stop((esp_rtc_handle_t)(uintptr_t)1);
    h264=true; fail_init=true;
    assert(!sip_service_start_mode("uri",true) && !sip_service_has_video());
    connected=false;
    assert(!sip_service_start("uri"));
    return 0;
}
'''
        run_c(mocks + source + checks)

    def test_frame_copy_bounds_duplicate_cache_and_busy_producer(self):
        source = (ROOT / "main/camera_manip.c").read_text()
        source = source[source.index("esp_err_t camera_copy_encoded_frame("):]
        mocks = r'''
#include <assert.h>
#include <stdint.h>
#include <stddef.h>
#include <string.h>
typedef int esp_err_t;
#define ESP_OK 0
#define ESP_ERR_INVALID_ARG 1
#define ESP_ERR_INVALID_STATE 2
#define ESP_ERR_TIMEOUT 3
#define ESP_ERR_NOT_FOUND 4
#define ESP_ERR_INVALID_SIZE 5
#define pdTRUE 1
#define ESP_CACHE_MSYNC_FLAG_DIR_M2C 1
#define ALIGN_UP(n,a) (((n)+(a)-1)&~((a)-1))
static void *s_frame_lock=(void *)1;
static unsigned char encoded[64]="frame123";
static unsigned char *s_jpeg_out_buf=encoded;
static size_t s_jpeg_out_len=8;
static uint32_t s_jpeg_seq=1;
static int busy, held, cache_result;
static int xSemaphoreTake(void *lock,int ticks) { assert(ticks==0); if(busy)return 0; held=1; return 1; }
static void xSemaphoreGive(void *lock) { assert(held); held=0; }
static int esp_cache_msync(void *p,size_t n,int flags) { assert(held && n==64); return cache_result; }
'''
        checks = r'''
int main(void) {
    unsigned char out[12]; memset(out,0x55,sizeof out);
    size_t n=99; uint32_t sequence=0;
    assert(camera_copy_encoded_frame(out,5,&n,&sequence)==ESP_ERR_INVALID_SIZE);
    assert(n==0 && sequence==0 && out[0]==0x55 && !held);
    cache_result=77;
    assert(camera_copy_encoded_frame(out,8,&n,&sequence)==77 && n==0 && sequence==0 && !held);
    cache_result=0;
    assert(camera_copy_encoded_frame(out,8,&n,&sequence)==ESP_OK);
    assert(n==8 && sequence==1 && !memcmp(out,"frame123",8) && out[8]==0x55 && !held);
    assert(camera_copy_encoded_frame(out,8,&n,&sequence)==ESP_ERR_NOT_FOUND && n==0);
    s_jpeg_seq=2; busy=1;
    assert(camera_copy_encoded_frame(out,8,&n,&sequence)==ESP_ERR_TIMEOUT && n==0 && sequence==1);
    busy=0;
    assert(camera_copy_encoded_frame(out,8,&n,&sequence)==ESP_OK && sequence==2);
    s_jpeg_out_len=0;
    assert(camera_copy_encoded_frame(out,8,&n,&sequence)==ESP_ERR_NOT_FOUND && n==0);
    assert(camera_copy_encoded_frame(NULL,8,&n,&sequence)==ESP_ERR_INVALID_ARG);
    return 0;
}
'''
        run_c(mocks + source + checks)

    def test_h264_sdp_packetization_direction_and_audio_passthrough(self):
        source = (ROOT / "main/rtsp_sdp_compat.c").read_text()
        mocks = r'''
#include <assert.h>
#include <stdlib.h>
#include <string.h>
static int fail_alloc;
void *media_lib_malloc(size_t n) { return fail_alloc ? NULL : malloc(n); }
void media_lib_free(void *p) { free(p); }
'''
        checks = r'''
int main(void) {
    char *text=NULL;
    int n=__wrap_media_lib_asprintf(&text,"m=video %d RTP/AVP 96\r\na=rtpmap:96 H264/90000\r\n",5004);
    assert(n==(int)strlen(text));
    assert(strstr(text,"m=video 5004 RTP/AVP 96\r\n"));
    assert(strstr(text,"a=fmtp:96 packetization-mode=1\r\n"));
    assert(strstr(text,"a=sendonly\r\n"));
    media_lib_free(text);
    n=__wrap_media_lib_asprintf(&text,"m=audio %d RTP/AVP 8\r\n",5002);
    assert(!strcmp(text,"m=audio 5002 RTP/AVP 8\r\n") && n==(int)strlen(text));
    media_lib_free(text);
    fail_alloc=1;
    assert(__wrap_media_lib_asprintf(&text,"%s","test")==-1 && !text);
    return 0;
}
'''
        run_c(mocks + source + checks, {"media_lib_os.h": "#include <stddef.h>\nvoid *media_lib_malloc(size_t);\nvoid media_lib_free(void *);\n"})


if __name__ == "__main__":
    unittest.main()
