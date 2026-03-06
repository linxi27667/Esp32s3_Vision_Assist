/**
 * @file    uart.c
 * @dir     main/BSP/Src
 * @brief   串口驱动核心逻辑流转 (纯逻辑，无系统依赖)
 */
#include "bsp_uart.h"
#include <stdlib.h>

void BSP_UART_Init_Device(uart_t *uart) {
    if (uart == NULL) return;

    uart->is_init = false;
    
    // 触发应用层绑定的底层硬件初始化
    if (uart->Hw_Init != NULL) {
        if (uart->Hw_Init(uart)) {
            uart->is_init = true;
        }
    }
}

// 供应用层 Task 轮询调用的逻辑引擎 (处理了极其干净的内存分配)
void BSP_UART_Process_Event(uart_t *uart) {
    if (uart == NULL || !uart->is_init) return;

    uint32_t event_size = 0;

    // 1. 调用底层的等待机制 (ESP32 的队列阻塞 / STM32 的轮询)
    if (uart->Hw_Wait_Event != NULL && uart->Hw_Wait_Event(uart, &event_size)) {
        
        // 2. 有数据来了！开辟缓冲区把数据装下
        uint8_t *dtmp = (uint8_t *)malloc(uart->rx_buf_size);
        if (dtmp != NULL) {
            
            // 3. 指挥底层去读真实的数据
            if (uart->Hw_Read != NULL) {
                uint32_t len = uart->Hw_Read(uart, dtmp, event_size);
                dtmp[len] = '\0'; // 强制封口变字符串
                
                // 4. 触发业务回调！(把数据甩给大模型或者电机)
                if (uart->On_Data_Received != NULL) {
                    uart->On_Data_Received(dtmp, len);
                }
            }
            free(dtmp); // 用完释放，绝不漏内存！
        }
    }
}