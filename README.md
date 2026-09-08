# Esp32s3_Vision_Assist

「随身智护」ESP32-S3-N8R16 终端：摄像头 RGB565 双缓冲、Web MJPEG、Core1 AI 推理（人脸/目标检测），面向视障辅助场景。

## 系统架构图

```mermaid
flowchart TB
  CAM[OV2640/5640 RGB565] --> C0[Core0 UI/IO\n采集/Web/叠框]
  C0 -->|非阻塞帧指针| C1[Core1 AI\nyolo_inference]
  C1 -->|结果互斥锁| C0
  C0 --> WEB[MJPEG 推流]
  WIFI[WiFi] --> WEB
```

## 功能框图

```mermaid
flowchart LR
  INIT[WiFi/消息总线] --> VID[图传服务]
  VID --> YOLO[模型加载与推理]
  YOLO --> BOX[检测框叠加]
  BOX --> WEB[浏览器调试]
```

## 远程

origin → https://github.com/linxi27667/Esp32s3_Vision_Assist.git
