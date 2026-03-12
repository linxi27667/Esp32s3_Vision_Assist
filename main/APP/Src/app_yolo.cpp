#include "app_yolo.h"
#include "esp_log.h"
#include "esp_heap_caps.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include <string.h>

#include "human_face_detect.hpp" // 头文件名更正
#include "dl_image_process.hpp"

static const char *TAG = "APP_YOLO";

// 全局 AI 模型实例
static HumanFaceDetect *detect = nullptr;

// ================= FreeRTOS 异步共享资源 =================
static yolo_async_result_t g_yolo_result = {0}; // 保存最新结果的结构体
static SemaphoreHandle_t g_yolo_mutex = NULL;   // 保护结果的互斥锁
static SemaphoreHandle_t g_yolo_sem = NULL;     // 唤醒推理任务的信号量

// PSRAM 中用于异步推理的专属图像缓冲区
static uint8_t *g_yolo_img_buf = NULL;
static int g_img_w = 0;
static int g_img_h = 0;

// 红色 RGB565 宏
#define RGB565_RED 0xF800

// ================= 极速画框底层函数 =================
static void Draw_Rect_RGB565(uint16_t *pixels, int w, int h, int bx, int by, int bw, int bh, uint16_t color) {
    if (bx < 0) bx = 0;
    if (by < 0) by = 0;
    if (bx + bw > w) bw = w - bx;
    if (by + bh > h) bh = h - by;

    for (int i = 0; i < bw; i++) {
        if (by >= 0 && by < h) pixels[by * w + bx + i] = color;             // Top
        if (by + bh - 1 < h)   pixels[(by + bh - 1) * w + bx + i] = color;  // Bottom
    }
    for (int i = 0; i < bh; i++) {
        if (bx >= 0 && bx < w) pixels[(by + i) * w + bx] = color;           // Left
        if (bx + bw - 1 < w)   pixels[(by + i) * w + bx + bw - 1] = color;  // Right
    }
}

// ================= 后台任务：专职推理 (Core 1) =================
static void yolo_inference_task(void *arg) {
    ESP_LOGI(TAG, "YOLO 异步后台任务已在 Core 1 启动!");

    while (1) {
        // 1. 睡觉，直到有人摇铃 (获取信号量)
        if (xSemaphoreTake(g_yolo_sem, portMAX_DELAY) == pdTRUE) {
            
            // 2. 组装输入数据 (直接使用 PSRAM 中的独立缓冲)
            dl::image::img_t img;
            img.data = g_yolo_img_buf;
            img.width = g_img_w;
            img.height = g_img_h;
            img.pix_type = dl::image::DL_IMAGE_PIX_TYPE_RGB565; 

            // 3. 开始耗时的推理
            uint32_t start_time = xTaskGetTickCount();
            auto &results = detect->run(img);
            uint32_t elapsed = (xTaskGetTickCount() - start_time) * portTICK_PERIOD_MS;
            ESP_LOGI(TAG, "推理耗时: %lu ms, 发现目标: %d 个", elapsed, results.size());

        // 4. 获取互斥锁，更新全局结果
            if (xSemaphoreTake(g_yolo_mutex, portMAX_DELAY) == pdTRUE) {
                g_yolo_result.count = 0;
                for (const auto &res : results) {
                    if (g_yolo_result.count >= YOLO_MAX_BOXES) break;
                    g_yolo_result.boxes[g_yolo_result.count].x = res.box[0];
                    g_yolo_result.boxes[g_yolo_result.count].y = res.box[1];
                    g_yolo_result.boxes[g_yolo_result.count].w = res.box[2];
                    g_yolo_result.boxes[g_yolo_result.count].h = res.box[3];
                    g_yolo_result.boxes[g_yolo_result.count].class_id = res.category;
                    g_yolo_result.boxes[g_yolo_result.count].confidence = res.score;
                    
                    // ================= 添加下面这行日志打印 =================
                    ESP_LOGI(TAG, "🎯 抓到目标! 坐标 X:%d, Y:%d | 大小 宽:%d, 高:%d | 概率:%.2f", 
                             res.box[0], res.box[1], res.box[2], res.box[3], res.score);
                    // =======================================================

                    g_yolo_result.count++;
                }
                xSemaphoreGive(g_yolo_mutex);
            }
        }
    }
}

// ================= C 语言暴露接口 =================

bool App_Yolo_Init(void) {
    ESP_LOGI(TAG, "正在初始化人脸检测模型...");
    
    // 1. 初始化模型
    detect = new HumanFaceDetect();
    if (detect == nullptr) {
        ESP_LOGE(TAG, "Failed to create HumanFaceDetect instance");
        return false;
    }

    // 2. 创建互斥锁和信号量
    g_yolo_mutex = xSemaphoreCreateMutex();
    g_yolo_sem = xSemaphoreCreateBinary();

    // 3. 分配专用内存池 (假设固定 QVGA 320x240，大小为 150KB)
    g_yolo_img_buf = (uint8_t *)heap_caps_malloc(320 * 240 * 2, MALLOC_CAP_SPIRAM);
    if (!g_yolo_img_buf) {
        ESP_LOGE(TAG, "YOLO 专属 PSRAM 缓冲区分配失败!");
        return false;
    }

    // 4. 启动后台推理任务绑定到 Core 1
    // 参数：函数名，任务名，堆栈大小，传参，优先级，任务句柄，核心编号(1)
    BaseType_t ret = xTaskCreatePinnedToCore(
        yolo_inference_task, "Yolo_Task", 16384, NULL, 5, NULL, 1);
    
    if (ret != pdPASS) {
        ESP_LOGE(TAG, "YOLO 任务创建失败!");
        return false;
    }

    ESP_LOGI(TAG, "YOLO 初始化并绑定 Core 1 成功!");
    return true;
}

// 图传任务调用的接口：尝试唤醒后台计算
bool App_Yolo_Trigger_Async(uint8_t *rgb565_data, int width, int height) {
    if (!rgb565_data || !g_yolo_img_buf) return false;

    // 非阻塞方式检查信号量是否为空（表示后台算完了在睡觉）
    // 用 uxSemaphoreGetCount 更安全
    if (uxSemaphoreGetCount(g_yolo_sem) == 0) {
        // 后台闲着，把最新图像塞给它
        g_img_w = width;
        g_img_h = height;
        memcpy(g_yolo_img_buf, rgb565_data, width * height * 2);
        xSemaphoreGive(g_yolo_sem); // 摇铃！
        return true;
    }
    // 如果还没算完，直接丢弃这帧请求，绝不阻塞前台
    return false;
}

// 图传任务调用的接口：在当前画面上画框
void App_Yolo_Draw_Latest_Boxes(uint8_t *rgb565_data, int width, int height) {
    if (!rgb565_data) return;

    // 尝试拿锁，最多等 0 毫秒。拿不到就不画了，保证视频流流畅
    if (xSemaphoreTake(g_yolo_mutex, 0) == pdTRUE) {
        for (int i = 0; i < g_yolo_result.count; i++) {
            Draw_Rect_RGB565((uint16_t *)rgb565_data, width, height, 
                             g_yolo_result.boxes[i].x, 
                             g_yolo_result.boxes[i].y, 
                             g_yolo_result.boxes[i].w, 
                             g_yolo_result.boxes[i].h, 
                             RGB565_RED);
        }
        xSemaphoreGive(g_yolo_mutex);
    }
}