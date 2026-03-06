/**
 * @file    uart_task.h
 * @dir     main/APP/Inc
 * @brief   串口应用层适配头文件
 */

#ifndef __UART_TASK_H__
#define __UART_TASK_H__

#include "bsp_uart.h"

/* 声明当前系统存在的 AI 对话串口实体 */
extern uart_t Uart_AI_Chat; 

/* 应用层一键初始化与启动接口 */
void App_UART_Task_Start(void);

#endif /* __UART_TASK_H__ */