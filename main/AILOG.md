# 🛠️ 随身智护终端 (Portable Smart Assistant) - 架构演进与开发日志

> **项目目标**: 为视障人士打造的工业级随身终端，支持本地实时目标检测、本地屏幕显示与远程Web调试。
> **当前状态**: ✅ 双核异步架构 | ✅ RGB565 流水线 | ✅ 16MB PSRAM 适配 | ✅ 人脸检测模型运行中

---

## 1. 硬件平台与基础设施 (Hardware & Infrastructure)

| 组件 | 规格 | 关键配置 |
| :--- | :--- | :--- |
| **主控芯片** | **ESP32-S3-N8R16** | **Flash**: 8MB (QIO 80MHz)<br>**PSRAM**: 16MB (Octal Mode 80MHz) |
| **摄像头** | **OV2640/OV5640** | **接口**: FPC (S3-EYE Pinout)<br>**格式**: RGB565 (320x240)<br>**修正**: Vflip=1, Hmirror=1 |
| **软件栈** | **ESP-IDF v5.2.6** | **AI 库**: ESP-DL (Human Face Detect)<br>**构建系统**: CMake |

---

## 2. 架构演进 (Architectural Evolution)

### 🔴 第一阶段：Web 原型机 (已废弃)
- **架构**：单核轮询模式。摄像头输出 JPEG -> HTTP 推流 -> 偶尔阻塞进行 AI 推理。
- **痛点**：
  1. **卡顿**：AI 推理 (300ms+) 期间视频流完全冻结。
  2. **内存碎片**：频繁 `malloc` JPEG 缓冲区导致 PSRAM 碎片化，最终 `malloc failed`。
  3. **无法扩展**：JPEG 格式无法直接驱动本地 SPI 屏幕（需要昂贵的解码开销）。

### 🟢 第二阶段：工业级双核异步架构 (当前架构)
我们重构了核心数据流，实现了 **UI 与 AI 的物理隔离**。

#### 核心设计：
1. **数据源 (RGB565 直通)**
   - 摄像头配置为 `PIXFORMAT_RGB565`，直接输出屏幕可用的原生格式。
   - **双重缓冲 (Double Buffering)**：利用 16MB PSRAM，开启 `fb_count=2`，确保采集与处理并行不冲突。

2. **Core 0 (APP核) - 极速 UI/IO 层**
   - **职责**：摄像头采集、Web 服务器 (MJPEG)、本地屏幕刷新 (未来)。
   - **策略**：
     - **非阻塞投递**：采集到新帧后，仅发送指针给 AI 任务。如果 AI 忙，直接丢弃，绝不等待。
     - **按需转码**：仅在有 Web 客户端连接时，调用 `frame2jpg` 将 RGB565 转为 JPEG 推流。
     - **零延迟绘制**：在当前帧上直接绘制**上一帧**的检测框 (1ms 开销)，实现视觉上的实时反馈。

3. **Core 1 (PRO核) - 专用 AI 算力层**
   - **职责**：运行深度学习推理 (Human Face Detect)。
   - **策略**：
     - **独立任务**：`yolo_inference_task_v2` 独占 Core 1。
     - **私有内存**：拥有独立的 `ai_input_buffer`，避免与 DMA 发生读写冲突。
     - **线程安全**：使用 FreeRTOS 互斥锁 (`g_result_mutex`) 保护推理结果，供 Core 0 读取。

---

## 3. 关键技术问题与解决方案 (Troubleshooting Log)

### 🔥 内存分配失败 (cam_hal: malloc failed)
- **现象**：无法初始化摄像头，报错 `alloc failed`。
- **原因**：YOLO 模型加载后，剩余连续内存不足以分配 150KB+ 的帧缓冲区。
- **解决**：
  1. 升级硬件至 **N8R16** (16MB PSRAM)。
  2. 在 `menuconfig` 中开启 **Octal PSRAM Mode** (关键！Quad 模式无法识别 16MB)。
  3. 验证：`heap_caps_get_free_size(MALLOC_CAP_SPIRAM)` 显示 > 10MB 可用。

### 📦 分区表溢出 (App partition is too small)
- **现象**：编译报错 `image size 0x2xxxxx > partition size 0x100000`。
- **原因**：引入 `esp-dl` 和模型权重后，固件体积超过默认的 1MB factory 分区。
- **解决**：
  1. 创建 `partitions2.csv`。
  2. 将 `factory` 分区扩大至 **3MB**。
  3. 将 `storage` 分区调整至 4MB (为后续模型升级预留)。

### 🧩 依赖冲突 (ESP-IDF v5.2 vs v5.3)
- **现象**：`esp-dl` 依赖 `esp_driver_jpeg` (v5.3 特性)，导致 v5.2 编译失败。
- **解决**：
  1. 修改 `idf_component.yml` 放宽版本限制。
  2. 手动在 `CMakeLists.txt` 中屏蔽 v5.2 不支持的组件。
  3. 在代码中手动定义缺失宏 `#define MALLOC_CAP_SIMD 0`。

### 🙃 摄像头画面倒置
- **现象**：Web 画面上下颠倒。
- **解决**：在初始化代码中添加硬件翻转指令：
  ```c
  s->set_vflip(s, 1);   // 垂直翻转
  s->set_hmirror(s, 1); // 水平镜像
  ```

---

## 4. 下一步计划 (Roadmap)

1.  **SPI 屏幕驱动**：利用现有的 RGB565 数据流，驱动 ST7789/ILI9341 屏幕。
2.  **语音播报**：当检测到特定目标（如“人脸”或“障碍物”）时，通过 I2S 播放提示音。
3.  **模型量化**：进一步裁剪模型，尝试运行更通用的 YOLO11n-coco 模型。

---

*Last Updated: 2026-03-12 by Senior Embedded Architect*
