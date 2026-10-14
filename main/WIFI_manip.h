#pragma once


#include "stdio.h"
#include <sys/socket.h>
#include <sys/types.h>
#include <arpa/inet.h>
#include "unistd.h"
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include "esp_event.h"
#include "esp_hosted.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_wifi.h"
#include "nvs_flash.h"
#include "stdbool.h"
#include"esp_err.h"






typedef enum {
    WIFI_STATE_IDLE,          /* not associated, nothing in flight        */
    WIFI_STATE_CONNECTING,    /* user just called wifi_connect()          */
    WIFI_STATE_CONNECTED,     /* associated and got an IP                 */
    WIFI_STATE_FAILED,        /* retries exhausted                        */
} wifi_state_t;

typedef void (*wifi_state_cb_t)(wifi_state_t state, const char *ip_or_null);



void wifi_init(void);
esp_err_t wifi_connect(const char *ssid, const char *password);
esp_err_t wifi_disconnect(void);
void wifi_set_state_cb(wifi_state_cb_t cb);
bool wifi_is_connected(void);
bool wifi_get_ip(char *out, size_t capacity);
int test_udp(void);