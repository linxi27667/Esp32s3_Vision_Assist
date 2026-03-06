/**
 * @file    uart.h
 * @dir     main/BSP/Inc
 * @brief   串口驱动核心框架层 (跨平台，纯C语言面向对象实现)
 */

#ifndef __BSP_UART_H__
#define __BSP_UART_H__

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

/* ================= 1. 基础数据类型 ================= */
typedef enum {
    UART_PORT_0 = 0,
    UART_PORT_1,
    UART_PORT_2
} uart_port_id_t;

/* ================= 2. 核心类定义 (Class) ================= */
typedef struct uart_dev {
    /* --- 1. 物理配置 (Config) --- */
    uart_port_id_t port_id;       // 串口号
    uint32_t       baud_rate;     // 波特率
    uint16_t       tx_pin;        // TX 引脚号
    uint16_t       rx_pin;        // RX 引脚号
    uint32_t       rx_buf_size;   // 接收缓冲区大小

    /* --- 2. 运行状态 (Status) --- */
    bool           is_init;       // 初始化状态标志
    void           *queue_handle; // 跨平台的底层消息队列指针 (存放 OS 队列)

    /* --- 3. 抽象硬件操作方法 (底层驱动实现) --- */
    bool     (*Hw_Init)(struct uart_dev *dev);
    bool     (*Hw_Wait_Event)(struct uart_dev *dev, uint32_t *out_size); // 抽象阻塞等待
    uint32_t (*Hw_Read)(struct uart_dev *dev, uint8_t *buf, uint32_t len);
    
    /* --- 4. 业务层回调函数 (数据出口) --- */
    void     (*On_Data_Received)(const uint8_t *data, uint32_t len);
} uart_t;

/* ================= 3. 对外暴露的控制 API ================= */
void BSP_UART_Init_Device(uart_t *uart);
void BSP_UART_Process_Event(uart_t *uart);

#endif /* __BSP_UART_H__ */