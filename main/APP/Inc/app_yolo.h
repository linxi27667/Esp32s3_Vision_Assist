#ifndef __APP_YOLO_H__
#define __APP_YOLO_H__

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

// YOLO 识别目标的最大数量
#define YOLO_MAX_BOXES 10

// 定义单个检测框结构
typedef struct {
    int x;
    int y;
    int w;
    int h;
    int class_id;
    float confidence;
} yolo_box_t;

// 定义包含多个目标的全局结果结构
typedef struct {
    yolo_box_t boxes[YOLO_MAX_BOXES];
    int count;
} yolo_async_result_t;

/**
 * @brief 初始化 YOLO 模型及双核异步系统
 * @return true 成功, false 失败
 */
bool App_Yolo_Init(void);

/**
 * @brief (异步版) 触发一次 YOLO 推理 (非阻塞)
 * @param rgb565_data 要推理的图像数据
 * @param width 图像宽度
 * @param height 图像高度
 * @return true 成功投递任务, false 任务忙
 */
bool App_Yolo_Trigger_Async(uint8_t *rgb565_data, int width, int height);

/**
 * @brief (异步版) 读取最新的识别结果并在原图上极速画框
 * @param rgb565_data 当前要推流的图像数据
 * @param width 图像宽度
 * @param height 图像高度
 */
void App_Yolo_Draw_Latest_Boxes(uint8_t *rgb565_data, int width, int height);

#ifdef __cplusplus
}
#endif

#endif // __APP_YOLO_H__