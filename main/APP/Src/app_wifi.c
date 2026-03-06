/**
 * @file    app_wifi.c
 * @brief   存放 SSID、密码，并触发网络连接
 */
#include "app_wifi.h"
#include "bsp_wifi.h"
#include "esp_log.h"
#include "sdkconfig.h"
#include <string.h>
#include "esp_wifi.h"       // 必须包含：定义了 wifi_scan_config_t 和所有 esp_wifi_ 函数
#include "esp_event.h"
#include "nvs_flash.h"      // 必须有：解决 nvs_flash_init 报错
#include "esp_netif.h"      // 必须有：解决 esp_netif_init 报错

static const char *TAG = "APP_WIFI";

// 在这里修改你的网络配置
#define ROUTER_SSID      "Ll"
#define ROUTER_PASSWORD  "ljc12345"
#define MAX_RETRY_TIMES  5

void App_WiFi_System_Init(void) {
    esp_log_level_set("wifi", ESP_LOG_WARN);
    esp_log_level_set("esp_netif_handlers", ESP_LOG_WARN);
    esp_log_level_set("BSP_WIFI", ESP_LOG_WARN);

    bsp_wifi_config_t my_wifi_cfg = {
        .max_retry = MAX_RETRY_TIMES,
        .ssid = {0},
        .password = {0}
    };
    
    // 拷贝账号密码
    strncpy(my_wifi_cfg.ssid, ROUTER_SSID, sizeof(my_wifi_cfg.ssid) - 1);
    my_wifi_cfg.ssid[sizeof(my_wifi_cfg.ssid) - 1] = '\0';
    strncpy(my_wifi_cfg.password, ROUTER_PASSWORD, sizeof(my_wifi_cfg.password) - 1);
    my_wifi_cfg.password[sizeof(my_wifi_cfg.password) - 1] = '\0';

#if (DEBUG_MODE == 1)
    ESP_LOGI(TAG, "网络子系统启动，正在连接热点: %s", ROUTER_SSID);
#endif

    // 调用底层开始阻塞连接
    if (BSP_WiFi_Init_And_Connect(&my_wifi_cfg)) {
        ESP_LOGI(TAG, "WiFi 连接成功");
    } else {
#if (DEBUG_MODE == 1)
        ESP_LOGE(TAG, "网络连接失败，请检查密码或路由器状态。系统将在脱机模式下运行。");
#endif
    }
}

void App_Wifi_Scan(void) {
    // 1. 必须先初始化 NVS（Wi-Fi 存储校准数据必用）
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    // 2. 初始化底层网络堆栈
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    esp_netif_create_default_wifi_sta();

    // 3. 启动 Wi-Fi 驱动并设为 Station 模式
    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_start());

    wifi_scan_config_t scan_config = { .ssid = 0, .bssid = 0, .channel = 0, .show_hidden = true };
    ESP_ERROR_CHECK(esp_wifi_scan_start(&scan_config, true));
    
    uint16_t ap_count = 0;
    esp_wifi_scan_get_ap_num(&ap_count);
    wifi_ap_record_t *ap_list = (wifi_ap_record_t *)malloc(sizeof(wifi_ap_record_t) * ap_count);
    ESP_ERROR_CHECK(esp_wifi_scan_get_ap_records(&ap_count, ap_list));
    
    for (int i = 0; i < ap_count; i++) {
        printf("找到热点: SSID=%s, RSSI=%d \n", ap_list[i].ssid, ap_list[i].rssi);
    }
    free(ap_list);
}
