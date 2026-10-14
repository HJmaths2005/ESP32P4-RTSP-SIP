"""Host regression checks for the production SIP event handler.

Run: ADF_PATH=/path/to/esp-adf python3 -m unittest discover -s tests -v
Only the event-state portion is compiled; hardware/audio code is excluded.
"""
import os
from pathlib import Path
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]


class SipStateTests(unittest.TestCase):
    def test_registration_and_call_events(self):
        include_dir = os.environ.get("ESP_RTC_INCLUDE_DIR")
        if not include_dir:
            adf = os.environ.get("ADF_PATH")
            self.assertIsNotNone(adf, "Set ADF_PATH or ESP_RTC_INCLUDE_DIR")
            include_dir = str(Path(adf) / "components/esp-adf-libs/esp_media_protocols/include")
        includes = Path(include_dir)
        self.assertTrue((includes / "esp_rtc.h").is_file(),
                        "Set ADF_PATH or ESP_RTC_INCLUDE_DIR to the protocol headers")
        source = (ROOT / "main/VoIP_manip.c").read_text()
        # Compile the actual state and event handler, with only SDK logging mocked.
        state = source[source.index("static const char *TAG"):source.index("int _send_audio(")]
        harness = r'''
#include <assert.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdatomic.h>
#include "esp_rtc.h"
#define ESP_OK 0
#define ESP_LOGI(tag, ...) ((void)(tag))
static bool wifi_get_ip(char *address, size_t size) { (void)address; (void)size; return false; }
'''
        checks = r'''
int main(void) {
    assert(!sip_service_registered());
    assert(_esp_sip_event_handler(ESP_RTC_EVENT_REGISTERED, NULL) == ESP_OK);
    assert(sip_service_registered());
    assert(sip_service_last_event() == ESP_RTC_EVENT_REGISTERED);
    _esp_sip_event_handler(ESP_RTC_EVENT_INCOMING, NULL);
    assert(sip_service_incoming());
    assert(sip_service_registered());
    _esp_sip_event_handler(ESP_RTC_EVENT_AUDIO_SESSION_BEGIN, NULL);
    assert(sip_service_in_call() && !sip_service_incoming());
    _esp_sip_event_handler(ESP_RTC_EVENT_HANGUP, NULL);
    assert(!sip_service_in_call() && !sip_service_incoming());
    assert(sip_service_registered());
    _esp_sip_event_handler(ESP_RTC_EVENT_UNREGISTERED, NULL);
    assert(!sip_service_registered());
    _esp_sip_event_handler(ESP_RTC_EVENT_REGISTERED, NULL);
    _esp_sip_event_handler(ESP_RTC_EVENT_CALLING, NULL);
    assert(sip_service_in_call());
    _esp_sip_event_handler(ESP_RTC_EVENT_ERROR, NULL);
    assert(!sip_service_registered() && !sip_service_in_call() && !sip_service_incoming());
    assert(sip_service_last_event() == ESP_RTC_EVENT_ERROR);
    return 0;
}
'''
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory)
            (path / "state.c").write_text(harness + state + checks)
            subprocess.run([os.environ.get("CC", "cc"), "-std=c11", "-Wall", "-Wextra",
                            "-Wno-unused-parameter", "-Werror=implicit-fallthrough",
                            "-I", str(includes), str(path / "state.c"),
                            "-o", str(path / "state")], check=True)
            subprocess.run([str(path / "state")], check=True)


class SipRetryTests(unittest.TestCase):
    def test_error_restarts_service_and_pending_registration_does_not(self):
        include_dir = os.environ.get("ESP_RTC_INCLUDE_DIR")
        if not include_dir:
            adf = os.environ.get("ADF_PATH")
            self.assertIsNotNone(adf, "Set ADF_PATH or ESP_RTC_INCLUDE_DIR")
            include_dir = str(Path(adf) / "components/esp-adf-libs/esp_media_protocols/include")
        source = (ROOT / "main/UI_engine.c").read_text()
        start = source.index("static void voice_register_cb(lv_event_t *e)\n{")
        callback = source[start:source.index("\nvoid voice_dial_clicked_cb", start)]
        harness = r'''
#include <assert.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>
#include "esp_rtc.h"
#include "app_validation.h"
#define ESP_OK 0
#define ESP_LOG_VERBOSE 5
#define ESP_LOGE(...) ((void)0)
typedef void lv_event_t;
static void *s_voice_status = (void *)1;
static void *g_codec_dev = (void *)1;
static char sip_server_ip[64] = "192.168.1.2";
static char s_sip_user[32] = "103", s_sip_password[64] = "secret", s_sip_uri[512];
static esp_rtc_handle_t rtc_handler;
static bool registered, connected = true;
static esp_rtc_event_t last_event;
static int stop_count, start_count, stop_result;
static bool wifi_is_connected(void) { return connected; }
static bool sip_service_registered(void) { return registered; }
static esp_rtc_event_t sip_service_last_event(void) { return last_event; }
static void esp_log_level_set(const char *tag, int level) { (void)tag; (void)level; }
static void lv_label_set_text(void *label, const char *text) { (void)label; (void)text; }
static int sip_service_stop(esp_rtc_handle_t handle) {
    assert(handle); ++stop_count; return stop_result;
}
static esp_rtc_handle_t sip_service_start(const char *uri) {
    assert(strcmp(uri, "udp://103:secret@192.168.1.2:5060") == 0);
    ++start_count; return (esp_rtc_handle_t)(uintptr_t)2;
}
'''
        checks = r'''
int main(void) {
    rtc_handler = (esp_rtc_handle_t)(uintptr_t)1;
    last_event = ESP_RTC_EVENT_ERROR;
    voice_register_cb(NULL);
    assert(stop_count == 1 && start_count == 1);
    assert(rtc_handler == (esp_rtc_handle_t)(uintptr_t)2);
    last_event = ESP_RTC_EVENT_UNREGISTERED;
    voice_register_cb(NULL);
    assert(stop_count == 1 && start_count == 1);
    registered = true;
    last_event = ESP_RTC_EVENT_REGISTERED;
    voice_register_cb(NULL);
    assert(stop_count == 1 && start_count == 1);
    registered = false;
    last_event = ESP_RTC_EVENT_ERROR;
    stop_result = -1;
    voice_register_cb(NULL);
    assert(stop_count == 2 && start_count == 1);
    assert(rtc_handler == (esp_rtc_handle_t)(uintptr_t)2);
    connected = false;
    voice_register_cb(NULL);
    assert(stop_count == 2 && start_count == 1);
    return 0;
}
'''
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory)
            (path / "retry.c").write_text(harness + callback + checks)
            subprocess.run([os.environ.get("CC", "cc"), "-std=c11", "-Wall", "-Wextra",
                            "-Wno-unused-parameter", "-I", include_dir,
                            "-I", str(ROOT / "main"), str(path / "retry.c"),
                            "-o", str(path / "retry")], check=True)
            subprocess.run([str(path / "retry")], check=True)


if __name__ == "__main__":
    unittest.main()
