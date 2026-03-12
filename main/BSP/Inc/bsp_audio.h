#ifndef __BSP_AUDIO_H__
#define __BSP_AUDIO_H__

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

/* --- 音频格式枚举 --- */
typedef enum {
    AUDIO_FMT_PCM_16BIT,
    AUDIO_FMT_PCM_32BIT
} audio_format_t;

/* --- 音频设备抽象类 --- */
typedef struct {
    /* 1. 配置参数 */
    uint32_t sample_rate;
    audio_format_t format;
    uint8_t channels; // 1: Mono, 2: Stereo

    /* 2. 硬件底层操作方法 (依赖注入) */
    bool (*Init)(uint32_t sample_rate, uint8_t channels);
    int  (*Write)(const void *src, size_t size);
    int  (*Read)(void *dest, size_t size);
    void (*Set_Volume)(uint8_t volume); // 0-100
} audio_dev_t;

/* --- 对外暴露的高级 API --- */
bool Audio_Init_Device(audio_dev_t *dev);
int  Audio_Play_Sound(audio_dev_t *dev, const void *data, size_t len);
int  Audio_Record_Sound(audio_dev_t *dev, void *buffer, size_t len);

#endif
