#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_http_client.h"
#include "esp_crt_bundle.h" // 核心修复：替换错误的头文件
#include "esp_tls.h"
#include "cJSON.h"
#include "app_doubao_client.h"
#include "sdkconfig.h"

static const char *TAG = "DOUBAO_AI";

// ==========================================
// ⚠️ 极其重要：填入你的火山引擎配置！
// ==========================================
#define DOUBAO_API_KEY      "57f01057-1e3a-4f27-96d2-d2af44f2ecf9" 
// 注意：Bearer 后面有一个空格！例如 "Bearer sk-1234567890abcdef"

// 填入你的模型接入点 (Endpoint ID)
#define DOUBAO_MODEL_EP     "ep-20260305211914-c6dlg" 
#define DOUBAO_API_URL      "https://ark.cn-beijing.volces.com/api/v3/chat/completions"
// ==========================================

// 接收大模型返回数据的全局缓存区 (使用 PSRAM 或内部 RAM)
#define MAX_HTTP_RECV_BUFFER 4096
static char *response_data = NULL;
static int response_len = 0;

// HTTP 接收事件回调函数（负责把碎片化的数据拼成完整的 JSON）
esp_err_t _http_event_handler(esp_http_client_event_t *evt) {
    switch(evt->event_id) {
        case HTTP_EVENT_ON_DATA:
            if (!esp_http_client_is_chunked_response(evt->client)) {
                if (response_data == NULL) {
                    response_data = (char *)malloc(MAX_HTTP_RECV_BUFFER);
                    response_len = 0;
                }
                if (response_len + evt->data_len < MAX_HTTP_RECV_BUFFER) {
                    memcpy(response_data + response_len, evt->data, evt->data_len);
                    response_len += evt->data_len;
                    response_data[response_len] = '\0'; // 确保字符串结尾
                }
            }
            break;
        case HTTP_EVENT_ON_FINISH:
#if (DEBUG_MODE == 1)
            ESP_LOGI(TAG, "HTTPS 请求接收完毕!");
#endif
            break;
        case HTTP_EVENT_DISCONNECTED:
#if (DEBUG_MODE == 1)
            ESP_LOGI(TAG, "HTTPS 连接断开");
#endif
            break;
        default:
            break;
    }
    return ESP_OK;
}

// 向豆包发起聊天请求的核心函数
void App_Doubao_Post(const char *user_prompt) {
#if (DEBUG_MODE == 1)
    ESP_LOGI(TAG, "正在打包 JSON 请求...");
#endif

    // 1. 使用 cJSON 组装 OpenAI 格式的请求体
    cJSON *root = cJSON_CreateObject();
    cJSON_AddStringToObject(root, "model", DOUBAO_MODEL_EP);
    
    cJSON *messages = cJSON_CreateArray();
    
    // 系统人设设定 (System Prompt)
    cJSON *msg_sys = cJSON_CreateObject();
    cJSON_AddStringToObject(msg_sys, "role", "system");
    cJSON_AddStringToObject(msg_sys, "content", "你是一个名叫'银发智护'的老年人陪伴管家。请用简短、温柔、通俗的中文回答问题。");
    cJSON_AddItemToArray(messages, msg_sys);

    // 用户提问 (User Prompt)
    cJSON *msg_usr = cJSON_CreateObject();
    cJSON_AddStringToObject(msg_usr, "role", "user");
    cJSON_AddStringToObject(msg_usr, "content", user_prompt);
    cJSON_AddItemToArray(messages, msg_usr);

    cJSON_AddItemToObject(root, "messages", messages);

    // 将 cJSON 对象转为字符串准备发送
    char *post_data = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);

#if (DEBUG_MODE == 1)
    ESP_LOGI(TAG, "待发送的数据: %s", post_data);
#endif

    // 2. 配置 HTTP 客户端参数（兼容不同 ESP-IDF 版本）
#if (DEBUG_MODE == 1)
    ESP_LOGI(TAG, "正在初始化HTTP客户端...");
#endif
    esp_http_client_config_t config = {
        .url = DOUBAO_API_URL,
        .event_handler = _http_event_handler,
        // 兼容配置：不同版本的 CRT Bundle 附加函数写法
        #if ESP_IDF_VERSION >= ESP_IDF_VERSION_VAL(5, 0, 0)
        .crt_bundle_attach = esp_crt_bundle_attach, // ESP-IDF 5.x 版本
        #else
        .crt_bundle_attach = esp_crt_bundle_attach_all, // ESP-IDF 4.x 版本
        #endif
        .timeout_ms = 30000,                        // 保留30秒超时
        .keep_alive_enable = true,                  // 保留长连接
        .buffer_size = 4096,                       // 接收缓冲区
        .buffer_size_tx = 4096,                    // 发送缓冲区
    };
#if (DEBUG_MODE == 1)
    ESP_LOGI(TAG, "正在创建HTTP客户端句柄...");
#endif
    esp_http_client_handle_t client = esp_http_client_init(&config);
    if (client == NULL) {
#if (DEBUG_MODE == 1)
        ESP_LOGE(TAG, "HTTP客户端初始化失败");
#endif
        free(post_data);
        return;
    }
#if (DEBUG_MODE == 1)
    ESP_LOGI(TAG, "HTTP客户端初始化成功");
#endif

    // 设置 POST 方法和请求头
    esp_http_client_set_method(client, HTTP_METHOD_POST);
    esp_http_client_set_header(client, "Content-Type", "application/json");
    // 注意：Authorization 需要添加 "Bearer " 前缀
    char auth_header[128];
    snprintf(auth_header, sizeof(auth_header), "Bearer %s", DOUBAO_API_KEY);
    esp_http_client_set_header(client, "Authorization", auth_header);
    esp_http_client_set_post_field(client, post_data, strlen(post_data));

    // 3. 发起请求并阻塞等待结果
#if (DEBUG_MODE == 1)
    ESP_LOGI(TAG, "正在向豆包大模型发送请求，请耐心等待...");
#endif
    esp_err_t err = esp_http_client_perform(client);

    if (err == ESP_OK) {
        int status_code = esp_http_client_get_status_code(client);
#if (DEBUG_MODE == 1)
        ESP_LOGI(TAG, "HTTP 状态码 = %d", status_code);
#endif
        
        if (status_code == 200 && response_data != NULL) {
            ESP_LOGI(TAG, "HTTP 成功");
            // 4. 解析豆包返回的 JSON 结果
            cJSON *resp_json = cJSON_Parse(response_data);
            if (resp_json != NULL) {
                // 按照 OpenAI/豆包的格式逐层提取文本: choices[0]->message->content
                cJSON *choices = cJSON_GetObjectItem(resp_json, "choices");
                if (cJSON_IsArray(choices)) {
                    cJSON *choice = cJSON_GetArrayItem(choices, 0);
                    cJSON *message = cJSON_GetObjectItem(choice, "message");
                    cJSON *content = cJSON_GetObjectItem(message, "content");
                    
                    if (cJSON_IsString(content)) {
                        ESP_LOGI(TAG, "豆包回复: %s", content->valuestring);
                    } else {
#if (DEBUG_MODE == 1)
                        ESP_LOGE(TAG, "未提取到有效回答内容");
#endif
                    }
                } else {
#if (DEBUG_MODE == 1)
                    ESP_LOGE(TAG, "choices 不是数组");
#endif
                }
                cJSON_Delete(resp_json);
            } else {
#if (DEBUG_MODE == 1)
                ESP_LOGE(TAG, "JSON 解析失败! 原始响应: %s", response_data);
#endif
            }
        } else {
#if (DEBUG_MODE == 1)
            ESP_LOGE(TAG, "服务器返回错误状态码: %d, 内容: %s", status_code, response_data ? response_data : "NULL");
#endif
        }
    } else {
#if (DEBUG_MODE == 1)
        ESP_LOGE(TAG, "HTTP 请求彻底失败: %s", esp_err_to_name(err));
#endif
    }

    // 清理内存，防止内存泄漏
    esp_http_client_cleanup(client);
    free(post_data);
    if (response_data != NULL) {
        free(response_data);
        response_data = NULL;
    }
}

void App_Doubao_Post_Image(const char *image_data_uri, const char *prompt) {
    if (image_data_uri == NULL) {
        return;
    }

    cJSON *root = cJSON_CreateObject();
    cJSON_AddStringToObject(root, "model", DOUBAO_MODEL_EP);

    cJSON *messages = cJSON_CreateArray();

    cJSON *msg_sys = cJSON_CreateObject();
    cJSON_AddStringToObject(msg_sys, "role", "system");
    cJSON_AddStringToObject(msg_sys, "content", "你是一个名叫'银发智护'的老年人陪伴管家。请用简短、温柔、通俗的中文回答问题。");
    cJSON_AddItemToArray(messages, msg_sys);

    cJSON *msg_usr = cJSON_CreateObject();
    cJSON_AddStringToObject(msg_usr, "role", "user");
    cJSON *content = cJSON_CreateArray();

    cJSON *text_item = cJSON_CreateObject();
    cJSON_AddStringToObject(text_item, "type", "text");
    cJSON_AddStringToObject(text_item, "text", (prompt && prompt[0] != '\0') ? prompt : "请识别图片内容并用一句话描述");
    cJSON_AddItemToArray(content, text_item);

    cJSON *img_item = cJSON_CreateObject();
    cJSON_AddStringToObject(img_item, "type", "image_url");
    cJSON *img_url = cJSON_CreateObject();
    cJSON_AddStringToObject(img_url, "url", image_data_uri);
    cJSON_AddItemToObject(img_item, "image_url", img_url);
    cJSON_AddItemToArray(content, img_item);

    cJSON_AddItemToObject(msg_usr, "content", content);
    cJSON_AddItemToArray(messages, msg_usr);
    cJSON_AddItemToObject(root, "messages", messages);

    char *post_data = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);

    esp_http_client_config_t config = {
        .url = DOUBAO_API_URL,
        .event_handler = _http_event_handler,
        #if ESP_IDF_VERSION >= ESP_IDF_VERSION_VAL(5, 0, 0)
        .crt_bundle_attach = esp_crt_bundle_attach,
        #else
        .crt_bundle_attach = esp_crt_bundle_attach_all,
        #endif
        .timeout_ms = 30000,
        .keep_alive_enable = true,
        .buffer_size = 4096,
        .buffer_size_tx = 4096,
    };
    esp_http_client_handle_t client = esp_http_client_init(&config);
    if (client == NULL) {
        free(post_data);
        return;
    }

    esp_http_client_set_method(client, HTTP_METHOD_POST);
    esp_http_client_set_header(client, "Content-Type", "application/json");
    char auth_header[128];
    snprintf(auth_header, sizeof(auth_header), "Bearer %s", DOUBAO_API_KEY);
    esp_http_client_set_header(client, "Authorization", auth_header);
    esp_http_client_set_post_field(client, post_data, strlen(post_data));

    esp_err_t err = esp_http_client_perform(client);

    if (err == ESP_OK) {
        int status_code = esp_http_client_get_status_code(client);
        if (status_code == 200 && response_data != NULL) {
            ESP_LOGI(TAG, "HTTP 成功");
            cJSON *resp_json = cJSON_Parse(response_data);
            if (resp_json != NULL) {
                cJSON *choices = cJSON_GetObjectItem(resp_json, "choices");
                if (cJSON_IsArray(choices)) {
                    cJSON *choice = cJSON_GetArrayItem(choices, 0);
                    cJSON *message = cJSON_GetObjectItem(choice, "message");
                    cJSON *content_resp = cJSON_GetObjectItem(message, "content");
                    if (cJSON_IsString(content_resp)) {
                        ESP_LOGI(TAG, "豆包回复: %s", content_resp->valuestring);
                    }
                }
                cJSON_Delete(resp_json);
            }
        }
    }

    esp_http_client_cleanup(client);
    free(post_data);
    if (response_data != NULL) {
        free(response_data);
        response_data = NULL;
    }
}

// 独立的 FreeRTOS 任务
void doubao_task(void *pvParameters) {
    // 延迟 5 秒，确保 Wi-Fi 已经完全连接并且拿到了 IP
    vTaskDelay(pdMS_TO_TICKS(5000));
    
#if (DEBUG_MODE == 1)
    ESP_LOGI(TAG, "网络就绪，开始测试 AI 对话...");
#endif
    
    // 发起你的第一次大模型对话！
    App_Doubao_Post("你好！我是广州商学院的一名大学生，我正在用ESP32单片机和你对话。你能用一句鼓励的话回应我吗？");
    
    // 测试完成后，任务自我销毁
    vTaskDelete(NULL);
}

void App_Doubao_Task_Start(void) {
    // 增大任务栈空间到16KB（HTTPS握手+JSON解析需要更大栈）
    xTaskCreate(doubao_task, "doubao_task", 16384, NULL, 5, NULL);
}
