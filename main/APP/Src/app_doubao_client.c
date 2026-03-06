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
#include "app_message.h"
#include "app_video_server.h"

static const char *TAG = "DOUBAO_AI";

// ==========================================
// ⚠️ 极其重要：填入你的火山引擎配置！
// ==========================================
#define DOUBAO_API_KEY      "57f01057-1e3a-4f27-96d2-d2af44f2ecf9" 
// 注意：Bearer 后面有一个空格！例如 "Bearer sk-1234567890abcdef"

// 填入你的模型接入点 (Endpoint ID)
#define DOUBAO_MODEL_EP     "ep-20260306160831-plh6x" 
#define DOUBAO_API_URL      "https://ark.cn-beijing.volces.com/api/v3/chat/completions"
// ==========================================

/* ================= 1. 定义对话历史结构 ================= */
#define MAX_HISTORY_COUNT 20  // 升级：保留 20 条历史记录 (10 轮对话)，让 AI 更长情
#define MAX_CONTENT_LEN   1024 // 升级：单条记录最大 1KB，哪怕老人唠叨也能记住

typedef struct {
    char *role;    // "user" or "assistant"
    char *content; // 对话内容
} ChatMessage;

static ChatMessage history[MAX_HISTORY_COUNT];
static int history_count = 0;

/* ================= 2. 历史记录管理函数 ================= */
static void History_Add(const char *role, const char *content) {
    // 如果满了，移除最早的一条 (FIFO)
    if (history_count >= MAX_HISTORY_COUNT) {
        if (history[0].role) free(history[0].role);
        if (history[0].content) free(history[0].content);
        
        // 整体前移
        for (int i = 1; i < MAX_HISTORY_COUNT; i++) {
            history[i-1] = history[i];
        }
        history_count--;
    }

    // 添加新记录
    history[history_count].role = strdup(role);
    // 截断过长的内容以节省内存
    if (strlen(content) > MAX_CONTENT_LEN) {
        char *truncated = (char *)malloc(MAX_CONTENT_LEN + 1);
        if (truncated) {
            strncpy(truncated, content, MAX_CONTENT_LEN);
            truncated[MAX_CONTENT_LEN] = '\0';
            history[history_count].content = truncated;
        }
    } else {
        history[history_count].content = strdup(content);
    }
    history_count++;
}

// 接收大模型返回数据的全局缓存区 (使用 PSRAM 或内部 RAM)
// 升级：加大到 16KB，防止长对话被截断
#define MAX_HTTP_RECV_BUFFER 16384 
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
                    if (response_data == NULL) return ESP_FAIL;
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

    // 1. 将用户的新问题加入历史记录
    History_Add("user", user_prompt);

    // 2. 使用 cJSON 组装 OpenAI 格式的请求体
    cJSON *root = cJSON_CreateObject();
    cJSON_AddStringToObject(root, "model", DOUBAO_MODEL_EP);
    
    cJSON *messages = cJSON_CreateArray();
    
    // 2.1 系统人设设定 (System Prompt) - 始终放在第一条
    cJSON *msg_sys = cJSON_CreateObject();
    cJSON_AddStringToObject(msg_sys, "role", "system");
    cJSON_AddStringToObject(msg_sys, "content", "你是一个名叫'银发智护'的老年人陪伴管家。请用简短、温柔、通俗的中文回答问题。");
    cJSON_AddItemToArray(messages, msg_sys);

    // 2.2 填充历史对话记录
    for (int i = 0; i < history_count; i++) {
        cJSON *msg_hist = cJSON_CreateObject();
        cJSON_AddStringToObject(msg_hist, "role", history[i].role);
        cJSON_AddStringToObject(msg_hist, "content", history[i].content);
        cJSON_AddItemToArray(messages, msg_hist);
    }

    // 注意：因为我们在开头已经把 user_prompt 加入了 history，所以这里不需要再单独添加了
    // 否则会导致最后一条消息重复

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
        .timeout_ms = 60000,                        // 升级：60秒超时，等待长回复
        .keep_alive_enable = true,                  // 保留长连接
        .buffer_size = MAX_HTTP_RECV_BUFFER,        // 接收缓冲区
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
                        char *ans = content->valuestring;
                        ESP_LOGI(TAG, "豆包回复: %s", ans);
                        
                        // 3. 将豆包的回复加入历史记录 (实现记忆闭环)
                        History_Add("assistant", ans);

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

    // 1. 将用户的视觉问题加入历史记录 (保持上下文)
    // 优化默认提示词，强制要求给出判断结论
    const char *final_prompt = (prompt && prompt[0] != '\0') ? prompt : "请基于图片判断物品状态（如是否用完、是否存在），要给出明确结论， 并且要给出情景决策";
    History_Add("user", final_prompt);

    cJSON *root = cJSON_CreateObject();
    cJSON_AddStringToObject(root, "model", DOUBAO_MODEL_EP);

    cJSON *messages = cJSON_CreateArray();

    // 2.1 系统人设设定 (System Prompt) - 始终放在第一条
    cJSON *msg_sys = cJSON_CreateObject();
    cJSON_AddStringToObject(msg_sys, "role", "system");
    // 强化 System Prompt，强制 AI 给出明确判断，禁止废话
    cJSON_AddStringToObject(msg_sys, "content", "你是一个名叫'银发智护'的老年人陪伴管家。请必须基于图片回答用户的具体问题。1. 优先用温暖、亲切的口吻（如称呼'爷爷'）给出明确判断结论（如“可乐还没喝完呢”）；2. 结论后可加一句简短的关怀或建议3. 禁止机械地描述画面背景，要像家人一样对话。");
    cJSON_AddItemToArray(messages, msg_sys);

    // 2.2 填充历史对话记录 (纯文本历史)
    for (int i = 0; i < history_count - 1; i++) { // 注意：-1 是因为最新的一条是本次的视觉问题，需要单独特殊构造
        cJSON *msg_hist = cJSON_CreateObject();
        cJSON_AddStringToObject(msg_hist, "role", history[i].role);
        cJSON_AddStringToObject(msg_hist, "content", history[i].content);
        cJSON_AddItemToArray(messages, msg_hist);
    }

    // 2.3 构造本次的多模态消息 (Text + Image)
    cJSON *msg_usr = cJSON_CreateObject();
    cJSON_AddStringToObject(msg_usr, "role", "user");
    cJSON *content = cJSON_CreateArray();

    cJSON *text_item = cJSON_CreateObject();
    cJSON_AddStringToObject(text_item, "type", "text");
    cJSON_AddStringToObject(text_item, "text", final_prompt);
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
        .timeout_ms = 60000,                        // 升级：60秒超时，等待长回复
        .keep_alive_enable = true,                  // 保留长连接
        .buffer_size = MAX_HTTP_RECV_BUFFER,        // 接收缓冲区
        .buffer_size_tx = 4096,                    // 发送缓冲区
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
                        char *ans = content_resp->valuestring;
                        ESP_LOGI(TAG, "豆包回复: %s", ans);
                        
                        // 3. 将豆包的视觉回复加入历史记录 (形成闭环)
                        History_Add("assistant", ans);
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

/* ================= AI 大脑任务：统一处理所有智能决策 ================= */
static void doubao_message_task(void *pvParameters) {
    Input_Message_t msg;
    
    // 延迟 5 秒，确保 Wi-Fi 已经完全连接并且拿到了 IP
    vTaskDelay(pdMS_TO_TICKS(5000));
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
                    // 关键修复：传入用户的原始问题 (msg.content)
                    // 如果是 "img" 指令，msg.content 可能为空，App_Video_Capture_And_Recognize 内部会处理
                    App_Video_Capture_And_Recognize(msg.content); 
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

void App_Doubao_Task_Start(void) {
    // 启动 AI 大脑主任务 (创建独立的 FreeRTOS 任务)
    // 栈大小给 16384 (16KB)，优先级设为 5 (中等优先级)
    xTaskCreate(doubao_message_task, "doubao_task", 16384, NULL, 5, NULL);
}
