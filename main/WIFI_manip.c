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

static const char *TAG = "wifi-starter";
extern char ip_address[16];

#define WIFI_CONNECTED_BIT  BIT0
#define WIFI_FAIL_BIT       BIT1
#define MAX_RETRIES         5

static EventGroupHandle_t s_wifi_event_group;
static int s_retry_num = 0;
static bool s_should_connect = false;
static bool s_connected = false;
static wifi_state_cb_t s_state_cb = NULL;


static void notify(wifi_state_t state, const char* ip){
    if(s_state_cb) s_state_cb(state, ip);
}

static void event_handler(void *arg, esp_event_base_t event_base,
                           int32_t event_id, void *event_data)
{
    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START) {
        // esp_wifi_connect();
        notify(WIFI_STATE_IDLE,NULL);
        return ;

    } else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) {
        s_connected = false;
        if(!s_should_connect){
            notify(WIFI_STATE_IDLE,NULL);
            return ;
        }
        if (s_retry_num < MAX_RETRIES) {
            s_retry_num++;
            ESP_LOGI(TAG, "Retrying WiFi (%d/%d)...", s_retry_num, MAX_RETRIES);
            esp_wifi_connect();
        } else {
            s_should_connect = false;
            xEventGroupSetBits(s_wifi_event_group, WIFI_FAIL_BIT);
            notify(WIFI_STATE_FAILED, NULL);
            // ESP_LOGE(TAG, "Could not connect to '%s'", CONFIG_WIFI_SSID);
        }
        return; 

    } else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t *event = (ip_event_got_ip_t *)event_data;
        ESP_LOGI(TAG, "Connected! IP: " IPSTR, IP2STR(&event->ip_info.ip));
        esp_ip4addr_ntoa(&event->ip_info.ip, ip_address, sizeof(ip_address));
        s_retry_num = 0;
        s_connected = true;
        xEventGroupSetBits(s_wifi_event_group, WIFI_CONNECTED_BIT);
        notify(WIFI_STATE_CONNECTED,ip_address);
        return;
    }
}

void wifi_init(void)
{
    ESP_LOGI(TAG, "ESP32-P4 + ESP32-C6 WiFi starter");

    // Step 1: Bring up the ESP32-C6 co-processor over SDIO.
    // This must happen before anything touches the WiFi stack.
    // The C6 runs esp_hosted firmware that exposes WiFi to the P4.
    ESP_LOGI(TAG, "Initializing ESP32-C6 co-processor...");
    ESP_ERROR_CHECK(esp_hosted_init());
    ESP_ERROR_CHECK(esp_hosted_connect_to_slave());
    ESP_LOGI(TAG, "Co-processor ready");

    // Step 2: Initialize NVS. The WiFi stack uses it to store calibration data.
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    // Step 3: Initialize the TCP/IP stack and event loop.
    s_wifi_event_group = xEventGroupCreate();
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    esp_netif_create_default_wifi_sta();

    // Step 4: Initialize WiFi and register event handlers.
    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    esp_event_handler_instance_t inst_any_id, inst_got_ip;
    ESP_ERROR_CHECK(esp_event_handler_instance_register(
        WIFI_EVENT, ESP_EVENT_ANY_ID, &event_handler, NULL, &inst_any_id));
    ESP_ERROR_CHECK(esp_event_handler_instance_register(
        IP_EVENT, IP_EVENT_STA_GOT_IP, &event_handler, NULL, &inst_got_ip));
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_start());
    ESP_ERROR_CHECK(esp_wifi_set_ps(WIFI_PS_NONE));  
    
    }

    void wifi_connect(const char* ssid, const char *password){
    // Step 5: Configure and start WiFi.
    if(!ssid || ssid[0] == '\0' ){
        ESP_LOGW(TAG, "wifi_connect : empty ssid, ignoring");
        return ;

    }
    if(!s_wifi_event_group){
        ESP_LOGE(TAG,"wifi_connect called before wifi init");
        return ;
    }
    /*
    wifi_config_t wifi_config = {
        .sta = {
            .ssid     = CONFIG_WIFI_SSID,
            .password = CONFIG_WIFI_PASS,
            .threshold.authmode = WIFI_AUTH_WPA2_PSK,
        },
    };
    */
    wifi_config_t  wifi_config = {0};
    strncpy((char *)wifi_config.sta.ssid,     ssid,     sizeof(wifi_config.sta.ssid)     - 1);
    strncpy((char *)wifi_config.sta.password, password ? password : "",
                                     sizeof(wifi_config.sta.password) - 1);
    wifi_config.sta.threshold.authmode = WIFI_AUTH_WPA2_PSK;
    ESP_LOGI(TAG, "Connecting to '%s'...", ssid);
    esp_wifi_disconnect();
    vTaskDelay(pdMS_TO_TICKS(50));
    xEventGroupClearBits(s_wifi_event_group, WIFI_CONNECTED_BIT  | WIFI_FAIL_BIT);
    s_retry_num          = 0;
    s_should_connect = true;
    // ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_config));
    // ESP_ERROR_CHECK(esp_wifi_start());
    // ESP_ERROR_CHECK(esp_wifi_set_ps(WIFI_PS_NONE));
    // ESP_LOGI(TAG, "Connecting to '%s'...", CONFIG_WIFI_SSID);

    // Wait for connection or failure.
    /*
    EventBits_t bits = xEventGroupWaitBits(s_wifi_event_group,
        WIFI_CONNECTED_BIT | WIFI_FAIL_BIT, pdFALSE, pdFALSE, portMAX_DELAY);
    */
   
    // notify(WIFI_STATE_CONNECTING, NULL);
    ESP_ERROR_CHECK(esp_wifi_connect());
    /*
    if (bits & WIFI_CONNECTED_BIT) {
        ESP_LOGI(TAG, "WiFi connected. You're done — build on this.");
    } else {
        ESP_LOGE(TAG, "WiFi failed. Check SSID/password and C6 firmware version.");
    }
    */
    
}

void wifi_disconnect(void)
{
    s_should_connect = false;
    s_connected      = false;
    esp_wifi_disconnect();
    // notify(WIFI_STATE_IDLE, NULL);
}

void wifi_set_state_cb(wifi_state_cb_t cb) { s_state_cb = cb; }
bool wifi_is_connected(void) { return s_connected; }


void test_udp_sip(void)
{
    int sockfd = socket(AF_INET, SOCK_DGRAM, 0);  // UDP, not TCP
    if (sockfd < 0) {
        ESP_LOGE("TEST", "Socket creation failed");
        return;
    }
    
    struct sockaddr_in address;
    address.sin_family = AF_INET;
    address.sin_port = htons(5060);
    address.sin_addr.s_addr = inet_addr("10.193.74.165");
    
    char *msg = "REGISTER sip:10.193.74.165 SIP/2.0\r\n"
                "Via: SIP/2.0/UDP 10.193.74.235:5060\r\n"
                "From: <sip:103@10.193.74.165>\r\n"
                "To: <sip:103@10.193.74.165>\r\n"
                "Call-ID: test123\r\n"
                "CSeq: 1 REGISTER\r\n"
                "Content-Length: 0\r\n\r\n";
    
    int sent = sendto(sockfd, msg, strlen(msg), 0, 
                      (struct sockaddr*)&address, sizeof(address));
    
    if (sent > 0) {
        ESP_LOGI("TEST", "✅ Sent %d bytes to Asterisk via UDP", sent);
        
        // Try to receive response
        char buffer[1024];
        struct sockaddr_in from_addr;
        socklen_t from_len = sizeof(from_addr);
        struct timeval tv = {.tv_sec = 5, .tv_usec = 0};
        setsockopt(sockfd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
        
        int received = recvfrom(sockfd, buffer, sizeof(buffer)-1, 0,
                                (struct sockaddr*)&from_addr, &from_len);
        if (received > 0) {
            buffer[received] = '\0';
            ESP_LOGI("TEST", "✅ Received %d bytes:\n%s", received, buffer);
        } else {
            ESP_LOGE("TEST", "❌ No response (timeout)");
        }
    } else {
        ESP_LOGE("TEST", "❌ Send failed, errno=%d", errno);
    }
    close(sockfd);
}

/*
void ping_laptop(void){
    int sockfd;
    int len;
    struct sockaddr_in address;
    int result;
    
    sockfd = socket(AF_INET, SOCK_STREAM, 0);
    address.sin_family = AF_INET;
    address.sin_port  = htons(6000);
    address.sin_addr.s_addr = inet_addr("10.193.74.165");
    char *msg = "hello logstash this is a simple test for you\n";
    result = connect(sockfd, (struct sockaddr*)&address , sizeof(address)); 
    if(result!= 0 ){
        fprintf(stdout,"there was and error connecting and the errorno is equal to : %d\n" ,errno);

    }
    write(sockfd,msg,strlen(msg));
    close(sockfd);
}
*/