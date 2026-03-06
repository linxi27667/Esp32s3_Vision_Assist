/*
 * SPDX-FileCopyrightText: 2010-2022 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: CC0-1.0
 */

 /*引入库驱动*/
#include <stdio.h>    // printf 等标准输出
#include <stdint.h>   // uint8_t, uint32_t 等固定宽度的整型 (极其重要)
#include <string.h>   // strcpy, memset 等字符串和内存操作
#include <inttypes.h> // PRIu32 等跨平台打印宏
#include "sdkconfig.h" //包含在 menuconfig 里配置的所有宏定义 (比如系统主频、Wi-Fi 密码等)
#include "freertos/FreeRTOS.h" // FreeRTOS 核心定义 (必须第一个包含)
#include "freertos/task.h"     // vTaskDelay, xTaskCreate 等任务调度 API
#include "freertos/queue.h"    // 队列通讯 (你以后做传感器数据传递肯定用得上)
#include "esp_chip_info.h" // 获取芯片型号、几核 CPU
#include "esp_flash.h"     // 操作外部 SPI Flash 闪存
#include "esp_system.h"    // 系统级函数，比如 esp_restart() 软件复位、获取剩余内存
#include "esp_log.h"       // ESP_LOGI


/*引入应用层*/
#include "app_wifi.h"
#include "app_video_server.h"
#include "app_doubao_client.h"
#include "app_uart.h"

/*定义日志类别*/
// static const char *TAG = "app_main";



void app_main(void)
{   
    App_WiFi_System_Init();
    //Video_Server_System_Init();
    //ESP_LOGI(TAG, "图传服务器启动完成，等待客户端接入...");
    App_UART_Task_Start();
    //App_Doubao_Task_Start();
    while (1) {
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}
