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

/* ================= AI 大脑任务：统一处理所有智能决策 ================= */
static void AI_Brain_Task(void *pvParameters) {
    Input_Message_t msg;
    
    ESP_LOGI(TAG, "🧠 AI 大脑任务启动，正在监听指令队列...");

    while (1) {
        // 永久阻塞等待消息队列 (核心：这里释放了 CPU 给其他任务)
        if (xQueueReceive(xQueue_Brain, &msg, portMAX_DELAY) == pdPASS) {
            
            ESP_LOGI(TAG, "🧠 大脑收到指令，类型: %d", msg.type);

            switch (msg.type) {
                case MSG_TYPE_TEXT:
                    // 收到文本，直接请求豆包大模型
                    App_Doubao_Post(msg.content);
                    break;

                case MSG_TYPE_CMD_IMAGE:
                    // 收到拍照指令，触发视觉识别流程
                    ESP_LOGI(TAG, "📸 执行视觉识别任务...");
                    // 传入 NULL 让函数内部使用默认提示词
                    App_Video_Capture_And_Recognize(NULL); 
                    break;

                default:
                    ESP_LOGW(TAG, "未知指令类型: %d", msg.type);
                    break;
            }

            // 处理完一条指令后，打印剩余内存监控
            ESP_LOGI(TAG, "任务完成。剩余堆内存: %lu Bytes", esp_get_free_heap_size());
        }
    }
}

void app_main(void)
{   
    // 1. 初始化 NVS 和 Wi-Fi (必须最先)
    App_WiFi_System_Init();

    // 2. 初始化消息队列 (大脑的神经元)
    App_Message_Init();

    // 3. 启动 AI 大脑主任务 (创建独立的 FreeRTOS 任务)
    // 栈大小给 8192 (8KB)，优先级设为 5 (中等优先级)
    xTaskCreate(AI_Brain_Task, "AI_Brain_Task", 8192, NULL, 5, NULL);

    // 4. 启动串口监听任务 (耳朵)
    // 串口收到数据后，会通过 App_Message_Send 投递给大脑
    App_UART_Task_Start();
    
    // 主循环可以空闲，或者做系统监控
    while (1) {
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}
