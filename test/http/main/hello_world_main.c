#include <stdio.h>
#include <string.h>

#include "esp_event.h"
#include "esp_http_client.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_system.h"
#include "esp_wifi.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "nvs_flash.h"
#include "bsp/esp-bsp.h"
#include "esp_spiffs.h"

// #include "bsp_board.h"


#define WIFI_SSID "sjl"
#define WIFI_PASS "12345678"

static const char *TAG = "HTTP_EXAMPLE";
static const char *context = "You are talking to a virtual assistant designed to help";

// WiFi event handler
static void wifi_event_handler(void *arg, esp_event_base_t event_base, int32_t event_id,
                               void *event_data) {
    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START) {
        esp_wifi_connect();
    } else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) {
        esp_wifi_connect();
    } else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t *event = (ip_event_got_ip_t *)event_data;
    }
}

void wifi_init_sta(void) {
    esp_netif_init();
    esp_event_loop_create_default();
    esp_netif_create_default_wifi_sta();

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    esp_wifi_init(&cfg);

    esp_event_handler_instance_register(WIFI_EVENT, ESP_EVENT_ANY_ID, &wifi_event_handler, NULL,
                                        NULL);
    esp_event_handler_instance_register(IP_EVENT, IP_EVENT_STA_GOT_IP, &wifi_event_handler, NULL,
                                        NULL);

    wifi_config_t wifi_config = {
        .sta =
            {
                .ssid = WIFI_SSID,
                .password = WIFI_PASS,
            },
    };
    esp_wifi_set_mode(WIFI_MODE_STA);
    esp_wifi_set_config(WIFI_IF_STA, &wifi_config);
    esp_wifi_start();
}

#define MAX_HTTP_OUTPUT_BUFFER (1024 * 20)
static char http_response[MAX_HTTP_OUTPUT_BUFFER];
#define AUDIO_FILE_PATH "/spiffs/result.wav"

esp_err_t _http_mp3_event_handler(esp_http_client_event_t *evt) {
    static FILE *file = NULL;
    static int total_bytes_received = 0;
    static char buffer[MAX_HTTP_OUTPUT_BUFFER];
    static int buffer_pos = 0;

    switch (evt->event_id) {
    case HTTP_EVENT_ON_DATA:
        if (!esp_http_client_is_chunked_response(evt->client)) {
            if (file == NULL) {
                // 打开 SPIFFS 文件系统中的文件用于写入
                file = fopen(AUDIO_FILE_PATH, "wb");
                if (!file) {
                    ESP_LOGE(TAG, "Failed to open file for writing");
                    return ESP_FAIL;
                }
            }

            // 累积数据
            if (buffer_pos + evt->data_len < MAX_HTTP_OUTPUT_BUFFER) {
                memcpy(buffer + buffer_pos, evt->data, evt->data_len);
                buffer_pos += evt->data_len;
            } else {
                // 溢出
                size_t written = fwrite(buffer, 1, buffer_pos, file);
                if (written != buffer_pos) {
                    ESP_LOGE(TAG, "File write failed");
                    fclose(file);
                    return ESP_FAIL;
                }

                total_bytes_received += buffer_pos;
                ESP_LOGI(TAG, "Received and wrote %d bytes, total %d", buffer_pos,
                         total_bytes_received);

                buffer_pos = 0;
                memcpy(buffer + buffer_pos, evt->data, evt->data_len);
                buffer_pos += evt->data_len;
            }
        }
        break;
    case HTTP_EVENT_ON_FINISH:
        // 完成时关闭文件
        if (file != NULL) {
            fclose(file);
            ESP_LOGI(TAG, "File successfully written, total size: %d bytes", total_bytes_received);
            file = NULL;
            total_bytes_received = 0;
        }

        break;
    default:
        break;
    }
    return ESP_OK;
}

void download_and_play_wav() {
    esp_http_client_config_t config = {.url = "http://172.20.10.2:5000/get_wav",
                                       .event_handler = _http_mp3_event_handler,
                                       .timeout_ms = 20000};

    esp_http_client_handle_t client = esp_http_client_init(&config);
    esp_err_t err = esp_http_client_perform(client);

    // audio_play_task(AUDIO_FILE_PATH);

    err = unlink(AUDIO_FILE_PATH);
    if (err == 0) {
        ESP_LOGI("File Delete", "Successfully deleted %s", AUDIO_FILE_PATH);
    } else {
        ESP_LOGE("File Delete", "Failed to delete %s", AUDIO_FILE_PATH);
    }

    esp_http_client_cleanup(client);
}

void task_http_request(void *pvParameters) {
    while (1) {
        download_and_play_wav();
        vTaskDelay(10000 / portTICK_PERIOD_MS);
    }
    vTaskDelete(NULL);
}

void format_spiffs() {
    esp_err_t ret = esp_spiffs_format(NULL);
    if (ret == ESP_OK) {
        ESP_LOGI(TAG, "SPIFFS formatted successfully!");
    } else {
        ESP_LOGE(TAG, "Failed to format SPIFFS (%s)", esp_err_to_name(ret));
    }
}

void app_main(void) {
    // Initialize NVS
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    // Initialize WiFi
    wifi_init_sta();

    // Wait for WiFi to connect (you can implement event-based notification for better handling)
    vTaskDelay(10000 / portTICK_PERIOD_MS);  // Wait for WiFi connection

    bsp_spiffs_mount();

    format_spiffs();

    // 创建一个HTTP请求的任务
    xTaskCreate(&task_http_request, "http_request_task", 8192, NULL, 5, NULL);
}
