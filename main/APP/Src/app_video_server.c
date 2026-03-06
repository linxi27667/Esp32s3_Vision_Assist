/**
 * @file    video_server.c
 * @brief   HTTP 图传服务器业务逻辑
 */
#include "app_video_server.h"
#include "bsp_camera.h"
#include "esp_http_server.h"
#include "esp_log.h"
#include "app_doubao_client.h"
#include "mbedtls/base64.h"
#include <stdlib.h>

static const char *TAG = "VIDEO_SERVER";
static bool camera_ready = false;

#define PART_BOUNDARY "123456789000000000000987654321"
static const char* _STREAM_CONTENT_TYPE = "multipart/x-mixed-replace;boundary=" PART_BOUNDARY;
static const char* _STREAM_BOUNDARY = "\r\n--" PART_BOUNDARY "\r\n";
static const char* _STREAM_PART = "Content-Type: image/jpeg\r\nContent-Length: %u\r\n\r\n";

/* ================= 1. 编写硬件底层参数配置 (HW_ 前缀) ================= */
static void HW_Camera_Config_Get(camera_config_t *config) {
    config->ledc_channel = LEDC_CHANNEL_0;
    config->ledc_timer   = LEDC_TIMER_0;
    
    // 严格对应你刚接好的杜邦线引脚
    config->pin_d0 = 15;
    config->pin_d1 = 17;
    config->pin_d2 = 18;
    config->pin_d3 = 16;
    config->pin_d4 = 14;
    config->pin_d5 = 12;
    config->pin_d6 = 11;
    config->pin_d7 = 48;
    config->pin_xclk = -1;  // ！！！极其重要：你模块自带晶振，不需要 ESP32 给时钟！
    config->pin_pclk = 13;
    config->pin_vsync = 38;
    config->pin_href = 47;
    config->pin_sccb_sda = 40;
    config->pin_sccb_scl = 39;
    config->pin_pwdn = -1;  // 我们已经在物理上把它接 GND 了
    config->pin_reset = -1; // 我们已经在物理上把它接 3.3V 了

    // 核心配置
    config->xclk_freq_hz = 24000000; // 告诉底层驱动，外部晶振是 24MHz
    config->frame_size   = FRAMESIZE_VGA; 
    config->pixel_format = PIXFORMAT_JPEG; 
    config->grab_mode    = CAMERA_GRAB_WHEN_EMPTY;
    config->fb_location  = CAMERA_FB_IN_PSRAM; 
    config->jpeg_quality = 12; 
    config->fb_count     = 2;  
}

/* ================= 2. 图传核心业务：HTTP 视频流分发 ================= */
/**
 * @brief HTTP GET 请求的回调函数：将照片源源不断地推给浏览器
 */
static esp_err_t stream_handler(httpd_req_t *req) {
    camera_fb_t * fb = NULL;
    esp_err_t res = ESP_OK;
    char * part_buf[64];

    // 告诉浏览器：这是一段连续的视频流，不要挂断！
    res = httpd_resp_set_type(req, _STREAM_CONTENT_TYPE);
    if(res != ESP_OK){ return res; }

    ESP_LOGI(TAG, "有电脑客户端接入图传系统，开始疯狂推流！");

    while(true){
        // 1. 从底层驱动拿到一张照片
        fb = esp_camera_fb_get();
        if (!fb) {
            ESP_LOGE(TAG, "获取照片失败！");
            res = ESP_FAIL;
        } else {
            // 2. 组装 HTTP 视频流的头部
            size_t hlen = snprintf((char *)part_buf, 64, _STREAM_PART, fb->len);
            res = httpd_resp_send_chunk(req, (const char *)part_buf, hlen);
            // 3. 发送真实的 JPEG 图像数据
            if(res == ESP_OK){
                res = httpd_resp_send_chunk(req, (const char *)fb->buf, fb->len);
            }
            // 4. 发送流边界符号
            if(res == ESP_OK){
                res = httpd_resp_send_chunk(req, _STREAM_BOUNDARY, strlen(_STREAM_BOUNDARY));
            }
            // 5. 将照片内存还给底层，准备拍下一张
            esp_camera_fb_return(fb);
        }
        if(res != ESP_OK){
            break; // 浏览器关闭或者网络异常，退出死循环
        }
    }
    ESP_LOGI(TAG, "客户端断开连接，推流结束。");
    return res;
}

/* ================= 3. 统一切入点 ================= */
void Video_Server_System_Init(void) {
    // 1. 获取硬件配置并初始化摄像头 BSP
    camera_config_t camera_config;
    HW_Camera_Config_Get(&camera_config);
    if (!BSP_Camera_Init(&camera_config)) {
        ESP_LOGE(TAG, "致命错误：摄像头硬件初始化失败，图传服务器终止启动。");
        return;
    }

    // 2. 配置并启动 HTTP 服务器
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.server_port = 80;
    httpd_handle_t stream_httpd = NULL;

    if (httpd_start(&stream_httpd, &config) == ESP_OK) {
        httpd_uri_t stream_uri = {
            .uri       = "/stream",
            .method    = HTTP_GET,
            .handler   = stream_handler,
            .user_ctx  = NULL
        };
        httpd_register_uri_handler(stream_httpd, &stream_uri);
        ESP_LOGI(TAG, "图传服务器已启动! 请在浏览器输入: http://你的ESP32_IP地址/stream");
    }
}

void App_Video_Capture_And_Recognize(const char *prompt) {
    if (!camera_ready) {
        camera_config_t camera_config;
        HW_Camera_Config_Get(&camera_config);
        if (!BSP_Camera_Init(&camera_config)) {
            return;
        }
        camera_ready = true;
    }

    camera_fb_t *fb = esp_camera_fb_get();
    if (!fb || fb->len == 0) {
        if (fb) {
            esp_camera_fb_return(fb);
        }
        return;
    }

    size_t base64_len = ((fb->len + 2) / 3) * 4 + 1;
    char *base64_buf = (char *)malloc(base64_len);
    if (!base64_buf) {
        esp_camera_fb_return(fb);
        return;
    }

    size_t out_len = 0;
    int ret = mbedtls_base64_encode((unsigned char *)base64_buf, base64_len, &out_len, fb->buf, fb->len);
    esp_camera_fb_return(fb);
    if (ret != 0) {
        free(base64_buf);
        return;
    }
    base64_buf[out_len] = '\0';

    const char *prefix = "data:image/jpeg;base64,";
    size_t data_uri_len = strlen(prefix) + out_len + 1;
    char *data_uri = (char *)malloc(data_uri_len);
    if (!data_uri) {
        free(base64_buf);
        return;
    }
    snprintf(data_uri, data_uri_len, "%s%s", prefix, base64_buf);
    free(base64_buf);

    if (prompt == NULL || prompt[0] == '\0') {
        App_Doubao_Post_Image(data_uri, "请识别图片内容并用一句话描述");
    } else {
        App_Doubao_Post_Image(data_uri, prompt);
    }
    free(data_uri);
}
