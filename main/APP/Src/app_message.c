#include "app_message.h"
#include <string.h>
#include "esp_log.h"

static const char *TAG = "APP_MESSAGE";

/* ================= 全局变量定义 ================= */
QueueHandle_t xQueue_Brain = NULL;

/* ================= 1. 初始化消息队列 ================= */
void App_Message_Init(void) {
    // 创建深度为 5 的队列，每个元素是 Input_Message_t 结构体
    xQueue_Brain = xQueueCreate(5, sizeof(Input_Message_t));
    
    if (xQueue_Brain == NULL) {
        ESP_LOGE(TAG, "Failed to create Brain Queue!");
    } else {
        ESP_LOGI(TAG, "Brain Queue Created Successfully");
    }
}

/* ================= 2. 发送消息封装 ================= */
BaseType_t App_Message_Send(Input_Type_t type, const char* text) {
    if (xQueue_Brain == NULL) return pdFAIL;

    Input_Message_t msg;
    msg.type = type;
    
    // 安全拷贝内容
    if (text != NULL) {
        strncpy(msg.content, text, MAX_MSG_LEN - 1);
        msg.content[MAX_MSG_LEN - 1] = '\0'; // 确保字符串结束符
    } else {
        msg.content[0] = '\0';
    }

    // 发送消息 (如果队列满，等待 10 ticks)
    // 注意：如果在中断中调用，需要使用 xQueueSendFromISR，这里暂时假设在任务中调用
    // 因为目前的 UART 回调是在 RingBuffer 处理任务中调用的，属于任务上下文
    if (xQueueSend(xQueue_Brain, &msg, (TickType_t)10) != pdPASS) {
        ESP_LOGW(TAG, "Queue Full! Message Dropped: %s", msg.content);
        return pdFAIL;
    }
    
    return pdPASS;
}
