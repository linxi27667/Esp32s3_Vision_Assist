/**
 * @file    bsp_wifi.c
 * @brief   ESP-IDF Wi-Fi 栈的初始化与事件处理
 */
#include "bsp_wifi.h"
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_log.h"
#include "nvs_flash.h"
#include <string.h>

static const char *TAG = "BSP_WIFI";

/* FreeRTOS 事件标志组，用于通知连接结果 */
static EventGroupHandle_t s_wifi_event_group;
#define WIFI_CONNECTED_BIT BIT0
#define WIFI_FAIL_BIT      BIT1

/* 内部状态记录 */
static uint8_t s_retry_num = 0;
static uint8_t s_max_retry = 0;

/* ================= ESP-IDF 异步事件回调函数 ================= */
static void event_handler(void* arg, esp_event_base_t event_base, int32_t event_id, void* event_data) {
    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START) {
        ESP_LOGI(TAG, "WiFi启动成功，开始连接...");
        esp_wifi_connect(); // Wi-Fi 启动后，立刻发起连接
    } 
    else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) {
        wifi_event_sta_disconnected_t* event = (wifi_event_sta_disconnected_t*) event_data;
        ESP_LOGE(TAG, "WiFi断开连接，原因代码: %d", event->reason);
        ESP_LOGE(TAG, "SSID: %s", (char*)event->ssid);
        
        if (s_retry_num < s_max_retry) {
            esp_wifi_connect();
            s_retry_num++;
            ESP_LOGW(TAG, "重试连接中... (%d/%d)", s_retry_num, s_max_retry);
        } else {
            ESP_LOGE(TAG, "已达到最大重试次数 %d，停止重试", s_max_retry);
            // 重试耗尽，设置失败标志位
            xEventGroupSetBits(s_wifi_event_group, WIFI_FAIL_BIT);
        }
    } 
    else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t* event = (ip_event_got_ip_t*) event_data;
        ESP_LOGI(TAG, "成功获取到 IP 地址: " IPSTR, IP2STR(&event->ip_info.ip));
        s_retry_num = 0;
        // 获取 IP 成功，设置成功标志位
        xEventGroupSetBits(s_wifi_event_group, WIFI_CONNECTED_BIT);
    }
}

/* ================= 核心连接逻辑 ================= */
bool BSP_WiFi_Init_And_Connect(bsp_wifi_config_t *config) {
    if (config == NULL) return false;
    
    s_max_retry = config->max_retry;
    s_wifi_event_group = xEventGroupCreate();

    // 1. 初始化 NVS (ESP32 Wi-Fi 必须依赖 NVS 存储校准数据)
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    // 2. 初始化网络接口和事件循环
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    esp_netif_create_default_wifi_sta();

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    // 3. 注册事件监听器 (将回调函数挂载到系统上)
    esp_event_handler_instance_t instance_any_id;
    esp_event_handler_instance_t instance_got_ip;
    ESP_ERROR_CHECK(esp_event_handler_instance_register(WIFI_EVENT, ESP_EVENT_ANY_ID, &event_handler, NULL, &instance_any_id));
    ESP_ERROR_CHECK(esp_event_handler_instance_register(IP_EVENT, IP_EVENT_STA_GOT_IP, &event_handler, NULL, &instance_got_ip));

    // 4. 组装配置信息
    wifi_config_t wifi_config = {
        .sta = {
            // 自动检测认证模式，支持WPA/WPA2/WPA3
            .threshold.authmode = WIFI_AUTH_OPEN,
        },
    };
    strncpy((char *)wifi_config.sta.ssid, config->ssid, sizeof(wifi_config.sta.ssid));
    strncpy((char *)wifi_config.sta.password, config->password, sizeof(wifi_config.sta.password));

    ESP_LOGI(TAG, "配置WiFi: SSID=%s, 密码长度=%d", config->ssid, strlen(config->password));

    // 5. 启动 Wi-Fi
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_config));
    ESP_ERROR_CHECK(esp_wifi_start());

    ESP_LOGI(TAG, "Wi-Fi 驱动初始化完成，正在连接 %s...", config->ssid);

    // 6. 阻塞等待结果 (死等标志位被事件回调函数置位)
    EventBits_t bits = xEventGroupWaitBits(s_wifi_event_group,
            WIFI_CONNECTED_BIT | WIFI_FAIL_BIT,
            pdFALSE, pdFALSE, portMAX_DELAY);

    // 7. 判断最终结果
    if (bits & WIFI_CONNECTED_BIT) {
        ESP_LOGI(TAG, "成功连接到热点: %s", config->ssid);
        return true;
    } else if (bits & WIFI_FAIL_BIT) {
        ESP_LOGE(TAG, "无法连接到热点: %s", config->ssid);
        return false;
    } else {
        ESP_LOGE(TAG, "发生了未知的连接异常");
        return false;
    }
}