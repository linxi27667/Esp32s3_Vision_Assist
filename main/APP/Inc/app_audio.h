#ifndef __APP_AUDIO_H__
#define __APP_AUDIO_H__

#include "bsp_audio.h"

/* --- 全局音频设备对象声明 --- */
extern audio_dev_t Audio_Device;

/* --- 应用层初始化入口 --- */
void App_Audio_System_Init(void);

#endif
