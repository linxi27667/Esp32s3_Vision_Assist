#ifndef __APP_MESSAGE_H__
#define __APP_MESSAGE_H__

#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"

/* ================= 1. 定义消息类型 (枚举) ================= */
typedef enum {
    MSG_TYPE_TEXT,       // 普通文本对话
    MSG_TYPE_CMD_IMAGE   // 图像识别指令 ("img")
} Input_Type_t;

/* ================= 2. 定义消息载体 (结构体) ================= */
#define MAX_MSG_LEN 512  // 限制单条消息最大长度

typedef struct {
    Input_Type_t type;
    char content[MAX_MSG_LEN]; // 消息内容 (对于 IMAGE_CMD 可为空)
} Input_Message_t;

/* ================= 3. 全局队列句柄声明 ================= */
extern QueueHandle_t xQueue_Brain;

/* ================= 4. 对外 API ================= */
void App_Message_Init(void);
BaseType_t App_Message_Send(Input_Type_t type, const char* text);

#endif // __APP_MESSAGE_H__
