/**
 * @file    bsp_camera.c
 * @brief   摄像头底层驱动实现
 */
#include "bsp_camera.h"
#include "esp_log.h"

static const char *TAG = "BSP_CAMERA";

/* ================= 1. 核心设备初始化 ================= */
bool BSP_Camera_Init(camera_config_t *config) {
    if (config == NULL) return false;

    // 调用乐鑫官方底层的初始化函数
    esp_err_t err = esp_camera_init(config);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "摄像头初始化失败，底层错误码: 0x%x", err);
        return false;
    }

    // 获取并打印传感器信息
    sensor_t *s = esp_camera_sensor_get();
    if (s != NULL) {
        ESP_LOGI(TAG, "成功点亮摄像头! 传感器 PID: 0x%x", s->id.PID);
        s->set_vflip(s, 1);  // 开启画面翻转
        s->set_hmirror(s, 1); // 开启水平镜像 (Horizontal Mirror)
    }
    return true;
}
