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
        // 【核心修复】关闭垂直翻转和水平镜像，解决“拍天花板”问题
        s->set_vflip(s, 1);  // 垂直翻转
        s->set_hmirror(s, 0); // 水平镜像
        
        // 升级：开启自动曝光 (AEC) 和自动白平衡 (AWB)，并开启去频闪模式 (Banding Filter)
        //s->set_aec2(s, 1);    // 开启 AEC DSP
         //s->set_awb_gain(s, 1);// 开启 AWB 增益
        //s->set_lenc(s, 1);    // 开启镜头校正
        //s->set_dcw(s, 1);     // 开启 Downsize En (缩放优化)
    }
    return true;
}
