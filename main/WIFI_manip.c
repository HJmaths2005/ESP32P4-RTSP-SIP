// Copyright 2026 David M. King
// SPDX-License-Identifier: Apache-2.0
//
// Minimal ESP32-P4 + ESP32-C6 WiFi starter.
//
// The ESP32-P4 has no built-in radio. WiFi is provided by an ESP32-C6
// co-processor connected via SDIO, managed by the esp_hosted library.
//
// Initialization order is strict:
//   1. esp_hosted_init() + esp_hosted_connect_to_slave()  — bring up C6 over SDIO
//   2. nvs_flash_init()                                   — WiFi stack needs NVS
//   3. esp_netif_init() + esp_event_loop_create_default() — network plumbing
//   4. esp_wifi_init() + configure + start                — WiFi itself
//
// Reversing steps 1 and 2+ causes cryptic failures. esp_hosted MUST be
// initialized before anything in the WiFi stack is touched.

#include "WIFI_manip.h"
#include "app_validation.h"
#include <assert.h>

static const char *TAG = "wifi-starter";
extern char ip_address[16];
#define WIFI_CONNECTED_BIT BIT0
#define WIFI_FAIL_BIT BIT1
#define MAX_RETRIES 5
ESP_EVENT_DEFINE_BASE(APP_WIFI_EVENT);
enum { APP_WIFI_CONNECT, APP_WIFI_DISCONNECT };
static EventGroupHandle_t s_wifi_event_group;
static portMUX_TYPE s_state_lock = portMUX_INITIALIZER_UNLOCKED;
static bool s_connected;
static bool s_should_connect;
static bool s_attempting;
static bool s_switch_pending;
static bool s_disconnecting;
static int s_retry_num;
static wifi_config_t s_pending_config;
static wifi_state_cb_t s_state_cb;

static void notify(wifi_state_t state, const char *ip)
{
    portENTER_CRITICAL(&s_state_lock);
    wifi_state_cb_t cb = s_state_cb;
    portEXIT_CRITICAL(&s_state_lock);
    if (cb) cb(state, ip); /* Always on the event task, never inside an LVGL callback. */
}

static void clear_connection(void)
{
    portENTER_CRITICAL(&s_state_lock);
    s_connected = false;
    ip_address[0] = '\0';
    portEXIT_CRITICAL(&s_state_lock);
    xEventGroupClearBits(s_wifi_event_group, WIFI_CONNECTED_BIT);
}

static void connection_failed(esp_err_t error)
{
    s_should_connect = false;
    s_attempting = false;
    clear_connection();
    xEventGroupSetBits(s_wifi_event_group, WIFI_FAIL_BIT);
    ESP_LOGW(TAG, "Connection failed: %s", esp_err_to_name(error));
    notify(WIFI_STATE_FAILED, NULL);
}

static void apply_pending_config(void)
{
    s_switch_pending = false;
    s_retry_num = 0;
    esp_err_t ret = esp_wifi_set_config(WIFI_IF_STA, &s_pending_config);
    if (ret != ESP_OK) { connection_failed(ret); return; }
    s_should_connect = true;
    s_attempting = true;
    notify(WIFI_STATE_CONNECTING, NULL);
    ret = esp_wifi_connect();
    if (ret != ESP_OK) connection_failed(ret);
}

static void event_handler(void *arg, esp_event_base_t base, int32_t id, void *data)
{
    if (base == APP_WIFI_EVENT) {
        bool was_attempting = s_attempting;
        bool was_connected = wifi_is_connected();
        bool was_disconnecting = s_disconnecting;
        s_should_connect = false;
        clear_connection();
        xEventGroupClearBits(s_wifi_event_group, WIFI_FAIL_BIT);
        if (id == APP_WIFI_DISCONNECT) {
            s_switch_pending = false;
            memset(&s_pending_config, 0, sizeof(s_pending_config));
            if (!was_disconnecting && (was_connected || was_attempting)) {
                esp_err_t ret = esp_wifi_disconnect();
                if (ret == ESP_OK) s_disconnecting = true;
                else if (ret == ESP_ERR_WIFI_NOT_CONNECT) s_attempting = false;
                else { connection_failed(ret); return; }
            }
            notify(WIFI_STATE_IDLE, NULL);
            return;
        }
        s_pending_config = *(const wifi_config_t *)data;
        s_switch_pending = true;
        notify(WIFI_STATE_CONNECTING, NULL);
        if (was_disconnecting) return; /* The outstanding disconnect will apply the newest request. */
        if (was_connected || was_attempting) {
            esp_err_t ret = esp_wifi_disconnect();
            if (ret == ESP_OK) { s_disconnecting = true; return; } /* Apply only after STA_DISCONNECTED. */
            if (ret != ESP_ERR_WIFI_NOT_CONNECT) { s_switch_pending = false; connection_failed(ret); return; }
        }
        s_attempting = false;
        apply_pending_config();
    } else if (base == WIFI_EVENT && id == WIFI_EVENT_STA_START) {
        notify(WIFI_STATE_IDLE, NULL);
    } else if (base == WIFI_EVENT && id == WIFI_EVENT_STA_DISCONNECTED) {
        s_disconnecting = false;
        clear_connection();
        s_attempting = false;
        if (s_switch_pending) { apply_pending_config(); return; }
        if (!s_should_connect) { notify(WIFI_STATE_IDLE, NULL); return; }
        if (s_retry_num++ >= MAX_RETRIES) { connection_failed(ESP_FAIL); return; }
        notify(WIFI_STATE_CONNECTING, NULL);
        s_attempting = true;
        esp_err_t ret = esp_wifi_connect();
        if (ret != ESP_OK) connection_failed(ret);
    } else if (base == IP_EVENT && id == IP_EVENT_STA_GOT_IP) {
        if (!s_should_connect || s_switch_pending) return;
        ip_event_got_ip_t *event = data;
        char address[16];
        esp_ip4addr_ntoa(&event->ip_info.ip, address, sizeof(address));
        portENTER_CRITICAL(&s_state_lock);
        memcpy(ip_address, address, sizeof(address));
        s_connected = true;
        portEXIT_CRITICAL(&s_state_lock);
        s_attempting = false;
        s_retry_num = 0;
        xEventGroupClearBits(s_wifi_event_group, WIFI_FAIL_BIT);
        xEventGroupSetBits(s_wifi_event_group, WIFI_CONNECTED_BIT);
        notify(WIFI_STATE_CONNECTED, address);
    } else if (base == IP_EVENT && id == IP_EVENT_STA_LOST_IP) {
        clear_connection();
        if (s_should_connect) s_attempting = true;
        notify(WIFI_STATE_CONNECTING, NULL);
    }
}

void wifi_init(void)
{
    ESP_ERROR_CHECK(esp_hosted_init());
    ESP_ERROR_CHECK(esp_hosted_connect_to_slave());
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);
    s_wifi_event_group = xEventGroupCreate();
    assert(s_wifi_event_group);
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    esp_netif_create_default_wifi_sta();
    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));
    /* Credentials are supplied at runtime and are not persisted automatically. */
    ESP_ERROR_CHECK(esp_wifi_set_storage(WIFI_STORAGE_RAM));
    ESP_ERROR_CHECK(esp_event_handler_register(APP_WIFI_EVENT, ESP_EVENT_ANY_ID, event_handler, NULL));
    ESP_ERROR_CHECK(esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID, event_handler, NULL));
    ESP_ERROR_CHECK(esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, event_handler, NULL));
    ESP_ERROR_CHECK(esp_event_handler_register(IP_EVENT, IP_EVENT_STA_LOST_IP, event_handler, NULL));
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_start());
    ESP_ERROR_CHECK(esp_wifi_set_ps(WIFI_PS_NONE));
}

esp_err_t wifi_connect(const char *ssid, const char *password)
{
    if (!s_wifi_event_group) return ESP_ERR_INVALID_STATE;
    if (!app_wifi_credentials_valid(ssid, password)) return ESP_ERR_INVALID_ARG;
    wifi_config_t config = {0};
    memcpy(config.sta.ssid, ssid, strlen(ssid));
    size_t length = password ? strlen(password) : 0;
    if (length) memcpy(config.sta.password, password, length);
    config.sta.threshold.authmode = length ? WIFI_AUTH_WPA2_PSK : WIFI_AUTH_OPEN;
    return esp_event_post(APP_WIFI_EVENT, APP_WIFI_CONNECT, &config, sizeof(config), 0);
}

esp_err_t wifi_disconnect(void)
{
    if (!s_wifi_event_group) return ESP_ERR_INVALID_STATE;
    return esp_event_post(APP_WIFI_EVENT, APP_WIFI_DISCONNECT, NULL, 0, 0);
}

void wifi_set_state_cb(wifi_state_cb_t cb)
{
    portENTER_CRITICAL(&s_state_lock);
    s_state_cb = cb;
    portEXIT_CRITICAL(&s_state_lock);
}

bool wifi_is_connected(void)
{
    portENTER_CRITICAL(&s_state_lock);
    bool connected = s_connected;
    portEXIT_CRITICAL(&s_state_lock);
    return connected;
}

bool wifi_get_ip(char *out, size_t capacity)
{
    if (!out || capacity < sizeof(ip_address)) return false;
    portENTER_CRITICAL(&s_state_lock);
    bool connected = s_connected;
    memcpy(out, ip_address, sizeof(ip_address));
    portEXIT_CRITICAL(&s_state_lock);
    return connected;
}


int test_udp(void){


   int sockfd = socket(AF_INET, SOCK_DGRAM, 0);
if (sockfd < 0) { ESP_LOGE("smoke", "socket: %d", errno); return 1; }

struct sockaddr_in local = {0};
local.sin_family = AF_INET;
local.sin_port   = htons(5060);
local.sin_addr.s_addr = INADDR_ANY;
int br = bind(sockfd, (struct sockaddr *)&local, sizeof(local));
ESP_LOGE("smoke", "bind source 5060 -> %d errno %d", br, errno);

struct sockaddr_in dst = {0};
dst.sin_family = AF_INET;
dst.sin_port   = htons(5060);
inet_pton(AF_INET, "10.33.233.165", &dst.sin_addr);
int s = sendto(sockfd, "hi", 2, 0, (struct sockaddr *)&dst, sizeof(dst));
ESP_LOGE("smoke", "sendto -> %d errno %d", s, errno);
return 0;


}