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
#include "app_message.h"
#include "app_audio.h" // 引入音频模块
#include "app_yolo.h"  // 引入 YOLO 模块

static const char *TAG = "APP_MAIN";

void app_main(void)
{   

    // 1. 基础环境与总线初始化
    App_WiFi_System_Init();
    App_Message_Init();
    
    // 2. 启动各大独立模块的后台任务
    // 错峰启动，避免并发电流冲击
    Video_Server_System_Init(); // 启动图传推流服务器 (内部会初始化摄像头，优先抢占 PSRAM)

    // 3. 初始化 YOLO 模型 (加载模型到 PSRAM)
    // 注意：YOLO 模型非常大，必须在摄像头分配完帧缓冲后加载，否则摄像头会因为内存碎片或空间不足而初始化失败
    if (App_Yolo_Init()) {
        ESP_LOGI(TAG, "YOLO Init Success");
    } else {
        ESP_LOGE(TAG, "YOLO Init Failed");
    }
    
    // 暂不启动 UART 和 Doubao 任务
    // App_UART_Task_Start();      
    // App_Doubao_Task_Start();    
    
    // 3. 主循环空闲，让出 CPU
    while (1) {
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}
