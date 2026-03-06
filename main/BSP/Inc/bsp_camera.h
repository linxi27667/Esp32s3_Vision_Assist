/**
 * @file    bsp_camera.h
 * @brief   摄像头驱动隔离层
 */
#ifndef __BSP_CAMERA_H__
#define __BSP_CAMERA_H__

#include <stdbool.h>
#include "esp_camera.h"

/* --- 暴露给应用层的极简 API --- */
/**
 * @brief   初始化摄像头硬件
 * @param   config  指向配置好引脚和分辨率的结构体
 * @return  true 成功, false 失败
 */
bool BSP_Camera_Init(camera_config_t *config);

#endif