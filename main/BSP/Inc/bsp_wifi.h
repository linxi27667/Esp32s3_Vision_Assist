/**
 * @file    bsp_wifi.h
 * @brief   底层 Wi-Fi 驱动隔离层 (隐藏 ESP-IDF 的复杂逻辑)
 */
#ifndef __BSP_WIFI_H__
#define __BSP_WIFI_H__

#include <stdint.h>
#include <stdbool.h>

/* 定义 Wi-Fi 配置结构体 */
typedef struct {
    char ssid[32];       // 路由器名称
    char password[64];   // 路由器密码
    uint8_t max_retry;   // 最大重连次数
} bsp_wifi_config_t;

/* --- 暴露给应用层的极简 API --- */

/**
 * @brief 初始化并阻塞等待 Wi-Fi 连接
 * @return true 成功拿到 IP, false 连接失败
 */
bool BSP_WiFi_Init_And_Connect(bsp_wifi_config_t *config);

#endif