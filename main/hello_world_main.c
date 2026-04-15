/*
 * SPDX-FileCopyrightText: 2010-2022 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: CC0-1.0
 */

#include <stdio.h>
#include <string.h>
#include "nvs_flash.h" //required for wifi to store configs
#include "esp_flash.h"
#include "esp_system.h"
#include "esp_netif.h" // the networking interface layer
#include "esp_log.h"
#include "esp_mac.h"
#include "esp_http_server.h"
#include "esp_event.h" // required for wifi event loops
#include "esp_wifi.h" // wifi driver
#include "driver/ledc.h"
#include "driver/gpio.h"

// PWM Configuration
#define PWM_FREQ        5000 // 5 kHz is great for DC motors
#define PWM_RES         LEDC_TIMER_8_BIT
#define PWM_SPEED_MODE  LEDC_LOW_SPEED_MODE
#define PWM_TIMER       LEDC_TIMER_0

#define MAX_SPEED       255
#define DRIVE_SPEED     200  // ~80% speed for basic movements
#define ESP_WIFI_SSID   "ESP32_RC_CAR"
#define ESP_WIFI_PASS  "pass1234"
#define MAX_STA_CONN   4

// Motor A (Left)
#define MOTOR_A_IN1 GPIO_NUM_25
#define MOTOR_A_IN2 GPIO_NUM_26

// Motor B (Right)
#define MOTOR_B_IN1 GPIO_NUM_32
#define MOTOR_B_IN2 GPIO_NUM_33

static const char *TAG = "MAIN";
/* Symbols created by the linker for the embedded file */
extern const uint8_t index_html_start[] asm("_binary_index_html_start");
extern const uint8_t index_html_end[]   asm("_binary_index_html_end");

extern const uint8_t style_css_start[] asm("_binary_output_css_start");
extern const uint8_t style_css_end[]   asm("_binary_output_css_end");

void init_pwm_motors(void) {
    // 1. Configure the Timer
    ledc_timer_config_t timer_conf = {
        .speed_mode       = PWM_SPEED_MODE,
        .timer_num        = PWM_TIMER,
        .duty_resolution  = PWM_RES,
        .freq_hz          = PWM_FREQ,
        .clk_cfg          = LEDC_AUTO_CLK
    };
    ESP_ERROR_CHECK(ledc_timer_config(&timer_conf));

    // 2. Configure the 4 Channels (one for each motor pin)
    ledc_channel_config_t ledc_channel[4] = {
        { .channel = LEDC_CHANNEL_0, .gpio_num = MOTOR_A_IN1, .speed_mode = PWM_SPEED_MODE, .timer_sel = PWM_TIMER, .duty = 0 },
        { .channel = LEDC_CHANNEL_1, .gpio_num = MOTOR_A_IN2, .speed_mode = PWM_SPEED_MODE, .timer_sel = PWM_TIMER, .duty = 0 },
        { .channel = LEDC_CHANNEL_2, .gpio_num = MOTOR_B_IN1, .speed_mode = PWM_SPEED_MODE, .timer_sel = PWM_TIMER, .duty = 0 },
        { .channel = LEDC_CHANNEL_3, .gpio_num = MOTOR_B_IN2, .speed_mode = PWM_SPEED_MODE, .timer_sel = PWM_TIMER, .duty = 0 }
    };

    for (int i = 0; i < 4; i++) {
        ESP_ERROR_CHECK(ledc_channel_config(&ledc_channel[i]));
    }
}
// Helper function to easily apply speed to a specific channel
void set_pin_pwm(ledc_channel_t channel, uint32_t duty) {
    ledc_set_duty(PWM_SPEED_MODE, channel, duty);
    ledc_update_duty(PWM_SPEED_MODE, channel);
}

void stop_motors() {
    set_pin_pwm(LEDC_CHANNEL_0, 0);
    set_pin_pwm(LEDC_CHANNEL_1, 0);
    set_pin_pwm(LEDC_CHANNEL_2, 0);
    set_pin_pwm(LEDC_CHANNEL_3, 0);
}

void move_forward() {
    set_pin_pwm(LEDC_CHANNEL_0, DRIVE_SPEED); // Motor A Forward
    set_pin_pwm(LEDC_CHANNEL_1, 0);
    set_pin_pwm(LEDC_CHANNEL_2, DRIVE_SPEED); // Motor B Forward
    set_pin_pwm(LEDC_CHANNEL_3, 0);
}

void move_backward() {
    set_pin_pwm(LEDC_CHANNEL_0, 0);
    set_pin_pwm(LEDC_CHANNEL_1, DRIVE_SPEED); // Motor A Reverse
    set_pin_pwm(LEDC_CHANNEL_2, 0);
    set_pin_pwm(LEDC_CHANNEL_3, DRIVE_SPEED); // Motor B Reverse
}

void turn_left() {
    set_pin_pwm(LEDC_CHANNEL_0, 0);
    set_pin_pwm(LEDC_CHANNEL_1, DRIVE_SPEED); // Motor A Reverse
    set_pin_pwm(LEDC_CHANNEL_2, DRIVE_SPEED); // Motor B Forward
    set_pin_pwm(LEDC_CHANNEL_3, 0);
}

void turn_right() {
    set_pin_pwm(LEDC_CHANNEL_0, DRIVE_SPEED); // Motor A Forward
    set_pin_pwm(LEDC_CHANNEL_1, 0);
    set_pin_pwm(LEDC_CHANNEL_2, 0);
    set_pin_pwm(LEDC_CHANNEL_3, DRIVE_SPEED); // Motor B Reverse
}

static esp_err_t index_get_handler(httpd_req_t *req) {
    // Calculate the size of the file
    const uint32_t index_html_len = index_html_end - index_html_start;

    // Set the content type so the browser knows it's HTML
    httpd_resp_set_type(req, "text/html");

    // Send the raw data directly from Flash
    httpd_resp_send(req, (const char *)index_html_start, index_html_len);
    
    return ESP_OK;
}

static esp_err_t css_get_handler(httpd_req_t *req) {
    const uint32_t css_len = style_css_end - style_css_start;
    httpd_resp_set_type(req, "text/css"); // CRITICAL: Tells browser this is CSS
    return httpd_resp_send(req, (const char *)style_css_start, css_len);
}

static const httpd_uri_t css_uri = {
    .uri       = "/output.css",
    .method    = HTTP_GET,
    .handler   = css_get_handler,
    .user_ctx  = NULL
};

static void wifi_event_handler(void* arg, esp_event_base_t event_base, int32_t event_id, void* event_data) {
    if (event_id == WIFI_EVENT_AP_STACONNECTED) {
        wifi_event_ap_staconnected_t* event = (wifi_event_ap_staconnected_t*) event_data;
        ESP_LOGI(TAG, "Station "MACSTR" joined, AID=%d", MAC2STR(event->mac), event->aid);
    } else if (event_id == WIFI_EVENT_AP_STADISCONNECTED) {
        wifi_event_ap_stadisconnected_t* event = (wifi_event_ap_stadisconnected_t*) event_data;
        ESP_LOGI(TAG, "Station "MACSTR" left, AID=%d", MAC2STR(event->mac), event->aid);
    }
}

void wifi_init_softap(void) {
    ESP_LOGI(TAG, "wifi_init_softap");
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    esp_netif_create_default_wifi_ap();

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    ESP_ERROR_CHECK(esp_event_handler_instance_register(WIFI_EVENT,
                                                        ESP_EVENT_ANY_ID,
                                                        &wifi_event_handler,
                                                        NULL,
                                                        NULL));

    wifi_config_t wifi_config = {
        .ap = {
            .ssid = ESP_WIFI_SSID,
            .ssid_len = strlen(ESP_WIFI_SSID),
            .password = ESP_WIFI_PASS,
            .max_connection = MAX_STA_CONN,
            .authmode = WIFI_AUTH_WPA_WPA2_PSK
        },
    };

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_AP));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_AP, &wifi_config));
    ESP_ERROR_CHECK(esp_wifi_start());

    ESP_LOGI(TAG, "WiFi AP Finished. SSID:%s password:%s", ESP_WIFI_SSID, ESP_WIFI_PASS);
}

static esp_err_t websocket_handler(httpd_req_t *req) {
    if (req->method == HTTP_GET) {
        ESP_LOGI(TAG, "Handshake done, the new connection was opened");
        return ESP_OK;
    }

    httpd_ws_frame_t ws_pkt;
    uint8_t *buf = NULL;
    memset(&ws_pkt, 0, sizeof(httpd_ws_frame_t));
    ws_pkt.type = HTTPD_WS_TYPE_TEXT;

    /* Set max_len = 0 to get the frame info first */
    esp_err_t ret = httpd_ws_recv_frame(req, &ws_pkt, 0);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "httpd_ws_recv_frame failed to get frame len");
        return ret;
    }

    if (ws_pkt.len > 0) {
        buf = calloc(1, ws_pkt.len + 1);
        ws_pkt.payload = buf;
        ret = httpd_ws_recv_frame(req, &ws_pkt, ws_pkt.len);
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "httpd_ws_recv_frame failed");
            free(buf);
            return ret;
        }
        
        // This is where the magic happens!
        ESP_LOGI(TAG, "Received packet: %s", ws_pkt.payload);

        char cmd = ((char*)ws_pkt.payload)[0];
        
        switch(cmd) {
            case 'F': move_forward(); break;
            case 'B': move_backward(); break;
            case 'L': turn_left(); break;
            case 'R': turn_right(); break;
            case 'S': stop_motors(); break;
            default:  stop_motors(); break;
        }
    }
    
    free(buf);
    return ESP_OK;
}

static const httpd_uri_t index_uri = {
  .uri       = "/",
  .method    = HTTP_GET,
  .handler   = index_get_handler,
  .user_ctx  = NULL
};

static const httpd_uri_t ws = {
  .uri        = "/ws",
  .method     = HTTP_GET,
  .handler    = websocket_handler,
  .user_ctx   = NULL,
  .is_websocket = true
};

httpd_handle_t start_webserver(void) {
    httpd_handle_t server = NULL;
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();

    ESP_LOGI(TAG, "Starting server on port: '%d'", config.server_port);
    if (httpd_start(&server, &config) == ESP_OK) {
        httpd_register_uri_handler(server, &index_uri);
        httpd_register_uri_handler(server, &ws);
        httpd_register_uri_handler(server, &css_uri);
        return server;
    }

    ESP_LOGI(TAG, "Error starting server!");
    return NULL;
}

void app_main(void) {
  esp_err_t ret = nvs_flash_init();
  if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
    ESP_ERROR_CHECK(nvs_flash_erase());
    ret = nvs_flash_init();
  }
  ESP_ERROR_CHECK(ret);

  ESP_LOGI(TAG, "ESP_WIFI_MODE_AP Initializing...");
  wifi_init_softap();

  ESP_LOGI(TAG, "Initializing Motors...");
  init_pwm_motors();

  start_webserver();
}
