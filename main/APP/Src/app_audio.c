#include "app_audio.h"
#include "driver/i2s_std.h" // ESP-IDF I2S 驱动
#include "driver/gpio.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "APP_AUDIO";

/* --- 引脚定义 (建议使用杜邦线连接这些引脚) --- */
// 避免使用 Camera 和 UART0 (43/44) 占用的引脚
#define I2S_BCK_PIN     GPIO_NUM_4
#define I2S_WS_PIN      GPIO_NUM_5
#define I2S_DOUT_PIN    GPIO_NUM_6  // 接 MAX98357A DIN
#define I2S_DIN_PIN     GPIO_NUM_7  // 接 INMP441 SD

/* --- I2S 句柄 --- */
static i2s_chan_handle_t tx_handle = NULL;
static i2s_chan_handle_t rx_handle = NULL;

/* ================= 1. 编写通用的硬件底层函数 (HW_ 前缀) ================= */
static bool HW_I2S_Init(uint32_t sample_rate, uint8_t channels) {
    /* 1. 创建 I2S 通道 (加大 DMA 缓冲区，防止爆音卡顿) */
    i2s_chan_config_t chan_cfg = I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM_AUTO, I2S_ROLE_MASTER);
    chan_cfg.dma_desc_num = 6;
    chan_cfg.dma_frame_num = 1024;
    chan_cfg.auto_clear = true;

    ESP_ERROR_CHECK(i2s_new_channel(&chan_cfg, &tx_handle, &rx_handle));

    /* 2. 配置标准模式 (大厂级魔法配置) */
    i2s_std_config_t std_cfg = {
        .clk_cfg = I2S_STD_CLK_DEFAULT_CONFIG(sample_rate),
        // 【核心优化1】：告诉软件，我们要的是 16-bit，单声道 (Mono) 数据！
        .slot_cfg = I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_16BIT, I2S_SLOT_MODE_MONO),
        .gpio_cfg = {
            .mclk = I2S_GPIO_UNUSED,
            .bclk = I2S_BCK_PIN,
            .ws = I2S_WS_PIN,
            .dout = I2S_DOUT_PIN,
            .din = I2S_DIN_PIN,
            .invert_flags = {
                .mclk_inv = false,
                .bclk_inv = false,
                .ws_inv = false,
            },
        },
    };

    // 【核心优化2】：硬件物理总线欺骗！
    // 强制把物理时钟槽设为 32-bit (满足 INMP441 的硬件要求)
    // 配合上面的 DATA_BIT_WIDTH_16BIT，ESP32的底层 DMA 会自动把前 16 个有效位取出来，扔掉后面的垃圾位！
    std_cfg.slot_cfg.slot_bit_width = I2S_SLOT_BIT_WIDTH_32BIT; 
    
    // 强制只读取左声道的数据 (请确保 INMP441 的 L/R 引脚接了 GND！)
    std_cfg.slot_cfg.slot_mask = I2S_STD_SLOT_LEFT;

    /* 3. 初始化 TX 和 RX 通道 */
    ESP_ERROR_CHECK(i2s_channel_init_std_mode(tx_handle, &std_cfg));
    ESP_ERROR_CHECK(i2s_channel_init_std_mode(rx_handle, &std_cfg));

    /* 4. 启用通道 */
    ESP_ERROR_CHECK(i2s_channel_enable(tx_handle));
    ESP_ERROR_CHECK(i2s_channel_enable(rx_handle));

    ESP_LOGI(TAG, "I2S 音频总线初始化成功 (16-bit Mono 净数据输出, 物理槽 32-bit)");
    return true;
}

static int HW_I2S_Write(const void *src, size_t size) {
    size_t bytes_written = 0;
    if (tx_handle == NULL) return -1;
    // 阻塞式写入，超时设为 portMAX_DELAY
    esp_err_t ret = i2s_channel_write(tx_handle, src, size, &bytes_written, portMAX_DELAY);
    return (ret == ESP_OK) ? bytes_written : -1;
}

static int HW_I2S_Read(void *dest, size_t size) {
    size_t bytes_read = 0;
    if (rx_handle == NULL) return -1;
    // 阻塞式读取
    esp_err_t ret = i2s_channel_read(rx_handle, dest, size, &bytes_read, portMAX_DELAY);
    return (ret == ESP_OK) ? bytes_read : -1;
}

/* ================= 2. 实例化对象并拼装引脚 ================= */
audio_dev_t Audio_Device = {
    .sample_rate = 16000,   // 16kHz 采样率 (语音识别标准)
    .channels = 2,          // 双声道配置 (I2S Standard)
    .format = AUDIO_FMT_PCM_32BIT,
    .Init = HW_I2S_Init,
    .Write = HW_I2S_Write,
    .Read = HW_I2S_Read,
    .Set_Volume = NULL
};

/* ================= 3. 回环测试任务 ================= */
// 缓冲区大小：1024 字节 (约 32ms 音频)
#define AUDIO_BUFFER_SIZE 1024

void Audio_Loopback_Task(void *pvParameters) {
    uint8_t *buffer = (uint8_t *)malloc(AUDIO_BUFFER_SIZE);
    if (buffer == NULL) {
        ESP_LOGE(TAG, "无法分配音频缓冲区");
        vTaskDelete(NULL);
    }

    ESP_LOGI(TAG, "麦克风回环测试任务已启动，请对着麦克风说话...");

    while (1) {
        // 1. 从麦克风读取数据
        int bytes_read = Audio_Record_Sound(&Audio_Device, buffer, AUDIO_BUFFER_SIZE);
        
        if (bytes_read > 0) {
            // 2. 直接写入扬声器 (回声)
            // 你可以在这里加一个简单的音量放大算法 (例如 * 2)，但要注意溢出
            Audio_Play_Sound(&Audio_Device, buffer, bytes_read);
        } else {
            vTaskDelay(pdMS_TO_TICKS(10));
        }
    }
}

/* ================= 4. 提供给 main.c 的统一切入点 ================= */
void App_Audio_System_Init(void) {
    // 1. 初始化硬件
    Audio_Init_Device(&Audio_Device);

    // 2. 启动回环测试任务
    xTaskCreate(Audio_Loopback_Task, "Audio_Loopback", 4096, NULL, 5, NULL);
}
