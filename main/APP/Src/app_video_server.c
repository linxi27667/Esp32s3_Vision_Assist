/**
 * @file    video_server.c
 * @brief   HTTP 图传服务器业务逻辑
 */
#include "app_video_server.h"
#include "bsp_camera.h"
#include "esp_http_server.h"
#include "esp_log.h"
#include "app_doubao_client.h"
#include "app_yolo.h" // 引入 YOLO 模块
#include "mbedtls/base64.h"
#include "img_converters.h" // 引入格式转换库 (frame2jpg)
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
    
    // 适配 ESP32-S3-N8R16 开发板 (Freenove/S3-EYE 标准引脚)
    config->pin_d0 = 11;
    config->pin_d1 = 9;
    config->pin_d2 = 8;
    config->pin_d3 = 10;
    config->pin_d4 = 12;
    config->pin_d5 = 18;
    config->pin_d6 = 17;
    config->pin_d7 = 16;
    config->pin_xclk = 15;
    config->pin_pclk = 13;
    config->pin_vsync = 6;
    config->pin_href = 7;
    config->pin_sccb_sda = 4;
    config->pin_sccb_scl = 5;
    config->pin_pwdn = -1;
    config->pin_reset = -1;

    // 核心配置
    config->xclk_freq_hz = 24000000; 
    config->frame_size   = FRAMESIZE_QVGA; // YOLO 最佳平衡点 (320x240)
    config->pixel_format = PIXFORMAT_RGB565; // 直接输出 RGB565，适配屏幕和 AI
    config->grab_mode    = CAMERA_GRAB_WHEN_EMPTY;
    config->fb_location  = CAMERA_FB_IN_PSRAM;  
    
    // N8R16 拥有 16MB PSRAM，即使是 RGB565 (150KB/帧) 也能轻松双缓冲
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
    
    // 定义一个计数器，控制推理频率
    uint32_t frame_count = 0;

    while(true){
        // 1. 从底层驱动拿到一张照片
        fb = esp_camera_fb_get();
        if (!fb) {
            ESP_LOGE(TAG, "获取照片失败！");
            res = ESP_FAIL;
        } else {
            // ============ 全新双核异步架构接入点 ============
            // 【核心修复】：必须保证这是一帧完整的图像 (宽*高*2字节)，残缺帧坚决不喂给AI！
            if (frame_count++ % 5 == 0 && fb->len == (fb->width * fb->height * 2)) { 
                App_Yolo_Trigger_Async(fb->buf, fb->width, fb->height);
            }
            
            // 无论有没有新结果，把最新的红框画在当前图像上 (极速)
            App_Yolo_Draw_Latest_Boxes(fb->buf, fb->width, fb->height);
            // ===================================================

            // 2. 组装 HTTP 视频流的头部 (注意：需要转码为 JPEG)
            uint8_t *jpg_buf = NULL;
            size_t jpg_len = 0;
            
            // 核心步骤：带红框的 RGB565 -> JPEG
            bool converted = frame2jpg(fb, 80, &jpg_buf, &jpg_len);
            
            if(!converted){
                ESP_LOGE(TAG, "JPEG compression failed");
                res = ESP_FAIL;
            } else {
                size_t hlen = snprintf((char *)part_buf, 64, _STREAM_PART, jpg_len);
                res = httpd_resp_send_chunk(req, (const char *)part_buf, hlen);
                if(res == ESP_OK){
                    res = httpd_resp_send_chunk(req, (const char *)jpg_buf, jpg_len);
                }
                if(res == ESP_OK){
                    res = httpd_resp_send_chunk(req, _STREAM_BOUNDARY, strlen(_STREAM_BOUNDARY));
                }
                free(jpg_buf); // 释放 frame2jpg 分配的内存
            }

            // 5. 核心修复：将照片内存还给底层，准备拍下一张。
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
    camera_ready = true; // 标记摄像头已就绪

    // 修正画面方向 (OV5640 在很多开发板上是倒置的)
    sensor_t *s = esp_camera_sensor_get();
    if (s) {
        s->set_vflip(s, 0);   // 垂直翻转
        s->set_hmirror(s, 0); // 水平镜像 (通常倒置时也需要镜像)
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
        ESP_LOGW(TAG, "摄像头尚未就绪，无法拍照！");
        return;
    }

    // 1. 咔嚓！拍照 (洗帧优化版)
    camera_fb_t *fb = NULL;
    
    for (int i = 0; i < 2; i++) {
        fb = esp_camera_fb_get();
        if (fb) esp_camera_fb_return(fb);
    }

    fb = esp_camera_fb_get();
    if (!fb || fb->len == 0) {
        ESP_LOGE(TAG, "抓图失败");
        if (fb) esp_camera_fb_return(fb);
        return;
    }
    
    // 2. 计算精准内存，强制分配在 PSRAM 中
    const char *prefix = "data:image/jpeg;base64,";
    size_t prefix_len = strlen(prefix);
    size_t base64_len = ((fb->len + 2) / 3) * 4 + 1;
    size_t data_uri_len = prefix_len + base64_len;

    char *data_uri = (char *)heap_caps_malloc(data_uri_len, MALLOC_CAP_SPIRAM);
    if (!data_uri) {
        ESP_LOGE(TAG, "PSRAM 内存分配失败！");
        esp_camera_fb_return(fb);
        return;
    }

    // 3. 内存拼图与 Base64 编码
    strcpy(data_uri, prefix); 
    size_t out_len = 0;
    int ret = mbedtls_base64_encode(
        (unsigned char *)(data_uri + prefix_len), 
        base64_len, 
        &out_len, 
        fb->buf, 
        fb->len
    );
    
    esp_camera_fb_return(fb); 

    if (ret != 0) {
        ESP_LOGE(TAG, "Base64 编码失败");
        free(data_uri);
        return;
    }

    ESP_LOGI(TAG, "Base64 打包完成 (豆包功能已临时屏蔽)");
    
    // 5. 释放 PSRAM 内存
    free(data_uri); 
}