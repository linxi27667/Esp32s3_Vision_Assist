#include "bsp_audio.h"
#include <stddef.h>

/* ================= 1. 设备初始化 ================= */
bool Audio_Init_Device(audio_dev_t *dev) {
    if (dev == NULL || dev->Init == NULL) return false;
    return dev->Init(dev->sample_rate, dev->channels);
}

/* ================= 2. 播放音频 ================= */
int Audio_Play_Sound(audio_dev_t *dev, const void *data, size_t len) {
    if (dev == NULL || dev->Write == NULL) return -1;
    // 这里可以加软件音量调节算法，暂时直接透传
    return dev->Write(data, len);
}

/* ================= 3. 录制音频 ================= */
int Audio_Record_Sound(audio_dev_t *dev, void *buffer, size_t len) {
    if (dev == NULL || dev->Read == NULL) return -1;
    return dev->Read(buffer, len);
}
