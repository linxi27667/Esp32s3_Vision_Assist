#ifndef DOUBAO_CLIENT_H
#define DOUBAO_CLIENT_H

#include <stdbool.h>

// 启动豆包 AI 测试任务
void App_Doubao_Task_Start(void);

// 发送文本给豆包 AI (对外暴露的 API)
void App_Doubao_Post(const char *user_prompt);
void App_Doubao_Post_Image(const char *image_data_uri, const char *prompt);

#endif
