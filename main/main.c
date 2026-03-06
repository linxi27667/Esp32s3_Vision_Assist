/*
 * SPDX-FileCopyrightText: 2010-2022 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: CC0-1.0
 */

/*引入库驱动*/
#include <stdio.h>    // printf 等标准输出
#include <stdint.h>   // uint8_t, uint32_t 等固定宽度的整型
#include <string.h>   // strcpy, memset 等字符串和内存操作
#include <inttypes.h> // PRIu32 等跨平台打印宏
#include "sdkconfig.h" //包含在 menuconfig 里配置的所有宏定义
#include "freertos/FreeRTOS.h" // FreeRTOS 核心定义
#include "freertos/task.h"     // vTaskDelay, xTaskCreate 等任务调度 API
#include "freertos/queue.h"    // 队列通讯
#include "esp_chip_info.h" // 获取芯片型号、几核 CPU
#include "esp_flash.h"     // 操作外部 SPI Flash 闪存
#include "esp_system.h"    // 系统级函数
#include "esp_log.h"       // ESP_LOGI

/*引入应用层*/
#include "app_wifi.h"
#include "app_video_server.h"
#include "app_doubao_client.h"
#include "app_uart.h"
#include "app_message.h" // 引入消息队列模块

static const char *TAG = "APP_MAIN";

void app_main(void)
{   

    // 1. 基础环境与总线初始化
    App_WiFi_System_Init();
    App_Message_Init();

    // 2. 启动各大独立模块的后台任务
    // 错峰启动，避免并发电流冲击
    Video_Server_System_Init(); // 启动图传推流服务器
    
    App_UART_Task_Start();      // 启动串口/控制台监听任务

    App_Doubao_Task_Start();    // 启动大模型认知任务
    
    // 3. 主循环空闲，让出 CPU
    while (1) {
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}
