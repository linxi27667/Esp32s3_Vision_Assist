/**
 * @file    app_uart.c
 * @dir     main/APP/Src
 * @brief   串口硬件实例化绑定与 FreeRTOS 业务流转
 */
#include "app_uart.h"
#include "driver/uart.h"        // 【解禁区】包含 ESP32 底层库
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include <string.h>
#include <stdio.h>
#include "sdkconfig.h"
#include "app_message.h" // 引入消息队列模块

static const char *TAG = "APP_UART";

static void On_AI_Chat_Received(const uint8_t *data, uint32_t len) {
    char clean_str[512] = {0};
    
    // 1. 安全拷贝并处理换行符
    if (len >= sizeof(clean_str)) len = sizeof(clean_str) - 1;
    memcpy(clean_str, data, len);
    clean_str[strcspn(clean_str, "\r\n")] = 0;

    if (strlen(clean_str) > 0) {
        ESP_LOGI(TAG, "💻 串口收到指令: %s", clean_str);
        
        // 2. 判断指令类型并投递到队列
        if (strncmp(clean_str, "img", 3) == 0) {
            // 发送图像识别指令 (非阻塞)
            App_Message_Send(MSG_TYPE_CMD_IMAGE, NULL);
        } else {
            // 发送文本对话请求 (非阻塞)
            App_Message_Send(MSG_TYPE_TEXT, clean_str);
        }
    }
}

static void console_event_task(void *pvParameters) {
    char input_buf[512];
    int idx = 0;
    
    // 清空缓冲区
    memset(input_buf, 0, sizeof(input_buf));

    while (1) {
        int c = fgetc(stdin); // 逐字符读取，最稳妥的方式
        
        if (c != EOF) {
            // 回显字符 (让用户知道自己输入了什么)
            fputc(c, stdout);
            
            // 如果遇到换行符，说明一句话结束了
            if (c == '\n' || c == '\r') {
                if (idx > 0) {
                    fputc('\n', stdout); // 补一个换行美观
                    input_buf[idx] = '\0'; // 确保字符串结束
                    On_AI_Chat_Received((const uint8_t *)input_buf, idx);
                    idx = 0;
                    memset(input_buf, 0, sizeof(input_buf));
                }
            } else {
                // 正常字符，存入缓冲区
                if (idx < sizeof(input_buf) - 1) {
                    input_buf[idx++] = (char)c;
                }
            }
        } else {
            // 如果没有输入，稍微休息一下，释放 CPU
            vTaskDelay(pdMS_TO_TICKS(10));
        }
    }
}

#if !(CONFIG_ESP_CONSOLE_UART || CONFIG_ESP_CONSOLE_USB_CDC || CONFIG_ESP_CONSOLE_USB_SERIAL_JTAG)
/* ================= 1. ESP32 专属底层驱动适配 (HW_ 前缀) ================= */

static bool HW_Uart_Init(uart_t *dev) {
    uart_config_t config = {
        .baud_rate = dev->baud_rate,
        .data_bits = UART_DATA_8_BITS,
        .parity    = UART_PARITY_DISABLE,
        .stop_bits = UART_STOP_BITS_1,
        .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
        .source_clk = UART_SCLK_DEFAULT,
    };
    
    uart_param_config((uart_port_t)dev->port_id, &config);
    if (dev->port_id == UART_PORT_0) {
        uart_set_pin((uart_port_t)dev->port_id, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE);
    } else {
        uart_set_pin((uart_port_t)dev->port_id, dev->tx_pin, dev->rx_pin, -1, -1);
    }

    QueueHandle_t q_handle;
    esp_err_t ret = uart_driver_install((uart_port_t)dev->port_id, dev->rx_buf_size * 2, 0, 20, &q_handle, 0);
    
    if (ret == ESP_OK) {
        dev->queue_handle = (void *)q_handle; // 绑定队列句柄
        return true;
    }
    return false;
}

// 极其关键的适配：用 FreeRTOS 队列阻塞代替空循环
static bool HW_Wait_Event(uart_t *dev, uint32_t *out_size) {
    uart_event_t event;
    QueueHandle_t queue = (QueueHandle_t)dev->queue_handle;
    
    if (xQueueReceive(queue, (void *)&event, portMAX_DELAY)) {
        switch (event.type) {
            case UART_DATA:
                // 【核心知识点】
                // ESP32 的 UART_DATA 事件涵盖了 "FIFO 满中断" 和 "RX 空闲中断 (RX_TIMEOUT)"
                // 只要对方停止发送超过一定时间（默认 10 个字符时间），就会触发此事件
                // 所以这里天然就是"空闲中断"逻辑！
                *out_size = event.size;
                return true;
            
            case UART_FIFO_OVF:
                ESP_LOGW(TAG, "硬件 FIFO 溢出！正在清空...");
                uart_flush_input((uart_port_t)dev->port_id);
                xQueueReset(queue);
                break;

            case UART_BUFFER_FULL:
                ESP_LOGW(TAG, "软件 Ring Buffer 满！正在清空...");
                uart_flush_input((uart_port_t)dev->port_id);
                xQueueReset(queue);
                break;

            case UART_BREAK:
                ESP_LOGI(TAG, "检测到串口 Break 信号");
                break;

            case UART_PARITY_ERR:
                ESP_LOGE(TAG, "串口奇偶校验错误");
                break;

            case UART_FRAME_ERR:
                ESP_LOGE(TAG, "串口帧错误");
                break;

            default:
                ESP_LOGI(TAG, "其他串口事件 ID: %d", event.type);
                break;
        }
    }
    return false;
}

static uint32_t HW_Uart_Read(uart_t *dev, uint8_t *buf, uint32_t len) {
    return uart_read_bytes((uart_port_t)dev->port_id, buf, len, portMAX_DELAY);
}

/* ================= 2. 核心业务回调 (拿到数据干什么) ================= */

/* ================= 3. 实例化 AI 聊天串口对象 ================= */

uart_t Uart_AI_Chat = {
    .port_id      = UART_PORT_0,
    .baud_rate    = 115200,
    .tx_pin       = 43,
    .rx_pin       = 44,
    .rx_buf_size  = 2048,
    
    .Hw_Init      = HW_Uart_Init,
    .Hw_Wait_Event= HW_Wait_Event,
    .Hw_Read      = HW_Uart_Read,
    
    .On_Data_Received = On_AI_Chat_Received
};

/* ================= 4. FreeRTOS 业务死循环 ================= */

static void uart_event_task(void *pvParameters) {
    uart_t *my_uart = (uart_t *)pvParameters;
    
    ESP_LOGI(TAG, "UART0 终端已启动，等待输入...");
    
    while (1) {
        BSP_UART_Process_Event(my_uart);
    }
}
#endif

/* ================= 5. 主函数唯一入口 ================= */

void App_UART_Task_Start(void) {
#if (CONFIG_ESP_CONSOLE_UART || CONFIG_ESP_CONSOLE_USB_CDC || CONFIG_ESP_CONSOLE_USB_SERIAL_JTAG)
    ESP_LOGI(TAG, "控制台输入模式已启动，等待输入...");
    xTaskCreate(console_event_task, "console_ai_task", 8192, NULL, 10, NULL);
#else
    BSP_UART_Init_Device(&Uart_AI_Chat);
    if (Uart_AI_Chat.is_init) {
        xTaskCreate(uart_event_task, "uart_ai_task", 16384, &Uart_AI_Chat, 10, NULL);
    } else {
        ESP_LOGE(TAG, "UART0 初始化失败！");
    }
#endif
}
