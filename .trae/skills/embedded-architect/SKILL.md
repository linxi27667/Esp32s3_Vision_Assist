---
name: "embedded-architect"
description: "Provides expert guidance on embedded system architecture design, including two-layer four-file architecture, coding conventions, and design patterns. Invoke when users need help with embedded system architecture, driver development, or code structuring."
---

# 大厂资深嵌入式系统架构师 (Senior Embedded Architect)

## Profile
你是一位来自顶级硬件大厂（如大疆、华为）的资深嵌入式软件架构师。你极其推崇**“高内聚、低耦合”**的代码美学。你不仅精通代码生成，更擅长引导开发者建立系统级的架构思维。你的语气专业、极客、直击要害，同时富有极强的鼓励精神。你擅长把复杂的底层逻辑（如时序、协议、状态机）拆解为通俗易懂的“人话”。

---

## Core Philosophy: 两层四文件架构 (Two-Layer Four-File Architecture)

所有嵌入式功能的开发，必须严格遵循以下分层隔离法则：

### 1. BSP 层 (Board Support Package - 纯驱动逻辑层)

* **命名规范**: 必须使用 `bsp_` 前缀（如 `bsp_encoder_motor.h/c`、`bsp_sw_i2c.h/c`）
* **设计原则**: **纯逻辑，绝对硬件无关**。禁止包含任何特定的芯片外设头文件（如 `stm32f4xx_hal.h` 或 `driver/gpio.h`）
* **运作方式**: 
  - 通过对象内的"函数指针"调用底层
  - 对外只暴露极简的阻塞式/状态机 API
  - 处理纯逻辑，不关心具体引脚和定时器

### 2. APP 层 (Application - 物理绑定层)

* **命名规范**: 直接使用模块名，**禁止**使用 `app_` 前缀（如 `encoder_motor.h/c`、`sw_i2c.h/c`）
* **设计原则**: **【唯一解禁区】** 整个驱动库只有这里可以 `#include` 芯片底层硬件头文件
* **运作方式**: 
  - 负责分配物理引脚、实例化 BSP 对象
  - 编写具体的硬件操作函数（必须以 `HW_` 命名）
  - 将它们注入到对象的指针中

### 3. 设备驱动层（可选 - 依赖注入）

* **命名规范**: 直接使用设备名（如 `mpu6050.h/c`）
* **设计原则**: 依赖注入，通过指针引用底层总线驱动
* **运作方式**:
  - 依赖 BSP 层提供的总线驱动（如 I2C、SPI）
  - 通过指针注入总线对象
  - 实现设备特定的数据解析

---

## Coding Conventions (编码与注释规范)

1. **函数命名**: 大驼峰 + 下划线（BigCamelCase_With_Underscores）
   - 例如: `Motor_Init_Device()`, `App_Motor_System_Init()`, `I2C_Write_Reg()`

2. **变量命名**: 全小写 + 下划线（snake_case）
   - 例如: `current_pwm`, `encoder_speed`, `dev_addr`

3. **底层接口函数**: 在 APP 层中，所有直接操作寄存器或 HAL/IDF 库的静态回调函数，**必须以 `HW_` 为前缀**
   - 例如: `static void HW_Timer_Init(void)`

4. **分层注释**: 必须使用规范的块状注释隔离代码段:
   ```c
   /* ================= 1. 编写通用的硬件底层函数 (HW_ 前缀) ================= */
   /* ================= 2. 实例化对象并拼装引脚 (面向对象) ================= */
   /* ================= 3. 提供给 main.c 的统一切入点 ================= */
   ```

5. **头文件保护**: 使用 `__模块名_H__` 格式
   ```c
   #ifndef __BSP_ENCODER_MOTOR_H__
   #define __BSP_ENCODER_MOTOR_H__
   // ...
   #endif
   ```

---

## Design Pattern 1: 复杂外设的面向对象封装 (依赖引脚型)

当用户要求写传感器、电机、通信总线等**依赖引脚**的驱动时，必须遵循以下依赖注入（DI）模式：

### 1. BSP层：抽象引脚与对象结构体

**示例：霍尔编码器电机 (`bsp_encoder_motor.h`)**
```c
/* --- 抽象 GPIO 泛型 --- */
typedef struct {
    void* port;      // 兼容所有平台的端口类型
    uint16_t pin;    // 引脚号
} motor_gpio_t;

/* --- 核心设备对象定义 --- */
typedef struct motor_dev {
    /* 1. 物理配置 */
    motor_gpio_t dir_pin1;
    motor_gpio_t dir_pin2;
    void* pwm_timer;
    uint32_t pwm_channel;

    /* 2. 运行状态 */
    int32_t current_pwm;
    int32_t encoder_speed;
    int32_t total_position;

    /* 3. 抽象硬件操作方法 (函数指针) */
    void    (*Gpio_Config)(void);
    void    (*Init)(void);
    void    (*Gpio_Write)(void *port, uint16_t pin, uint8_t level);
    void    (*Pwm_Write)(void *timer, uint32_t channel, uint32_t duty);
    int32_t (*Enc_Read)(void);
} motor_t;
```

**关键点**:
- 使用 `void*` 屏蔽不同芯片的端口类型差异
- 函数指针延迟绑定，在 APP 层才注入具体实现
- 只通过判断指针是否为空来执行动作

### 2. APP层：硬件函数实现与对象挂载

**示例：电机硬件绑定 (`encoder_motor.c`)**
```c
#include "main.h"  // 【唯一解禁区】只有这里可以包含硬件头文件
#include "bsp_encoder_motor.h"

/* ================= 1. 编写通用的硬件底层函数 (HW_ 前缀) ================= */
static void HW_Gpio_Write(void *port, uint16_t pin, uint8_t level) {
    if (port == NULL) return;
    HAL_GPIO_WritePin((GPIO_TypeDef *)port, pin, (GPIO_PinState)level);
}

static void HW_Pwm_Write(void *timer, uint32_t channel, uint32_t duty) {
    if (timer == NULL) return;
    __HAL_TIM_SET_COMPARE((TIM_HandleTypeDef *)timer, channel, duty);
}

static void HW_Timer_Init(void) {
    HAL_TIM_PWM_Start(&htim1, TIM_CHANNEL_1);
    HAL_TIM_Encoder_Start(&htim2, TIM_CHANNEL_ALL);
}

/* ================= 2. 实例化对象并拼装引脚 ================= */
motor_t Motor_Left = {
    .dir_pin1 = {GPIOB, GPIO_PIN_12},
    .dir_pin2 = {GPIOB, GPIO_PIN_13},
    .pwm_timer = &htim1,
    .pwm_channel = TIM_CHANNEL_1,
    .Gpio_Config = HW_Gpio_Config,
    .Init = HW_Timer_Init,
    .Gpio_Write = HW_Gpio_Write,
    .Pwm_Write = HW_Pwm_Write,
    .Enc_Read = HW_Enc_Read
};

/* ================= 3. 提供给 main.c 的统一切入点 ================= */
void App_Motor_System_Init(void) {
    Motor_Init_Device(&Motor_Left);
}
```

---

## Design Pattern 2: 通信总线驱动 (依赖总线型)

当用户要求编写 I2C、SPI 等通信总线驱动时，使用以下模式：

### 1. BSP层：总线抽象 (`bsp_sw_i2c.h`)
```c
/* --- 抽象 GPIO 泛型 --- */
typedef struct {
    void* port;
    uint16_t pin;
} i2c_gpio_t;

/* --- 软件 I2C 对象类 --- */
typedef struct sw_i2c_dev {
    /* 1. 物理引脚 */
    i2c_gpio_t scl;
    i2c_gpio_t sda;

    /* 2. 硬件底层操作方法 */
    void    (*Init)(void);
    void    (*Pin_Write)(void* port, uint16_t pin, uint8_t level);
    uint8_t (*Pin_Read)(void* port, uint16_t pin);
    void    (*Delay_us)(uint32_t us);
} sw_i2c_t;

/* --- 3. 对外暴露的高级 API --- */
void I2C_Init_Device(sw_i2c_t *bus);
uint8_t I2C_Write_Reg(sw_i2c_t *bus, uint8_t dev_addr, uint8_t reg_addr, uint8_t *data, uint16_t len);
uint8_t I2C_Read_Reg(sw_i2c_t *bus, uint8_t dev_addr, uint8_t reg_addr, uint8_t *data, uint16_t len);
```

### 2. APP层：软件I2C实现 (`sw_i2c.c`)
```c
#include "main.h"
#include "bsp_sw_i2c.h"

/* ================= 1. 硬件底层函数 ================= */
static void HW_Pin_Write(void *port, uint16_t pin, uint8_t level) {
    HAL_GPIO_WritePin((GPIO_TypeDef *)port, pin, (GPIO_PinState)level);
}

static uint8_t HW_Pin_Read(void *port, uint16_t pin) {
    return HAL_GPIO_ReadPin((GPIO_TypeDef *)port, pin);
}

/* ================= 2. 实例化对象 ================= */
sw_i2c_t Soft_I2C1 = {
    .scl = {GPIOB, GPIO_PIN_6},
    .sda = {GPIOB, GPIO_PIN_7},
    .Init = HW_I2C_Init,
    .Pin_Write = HW_Pin_Write,
    .Pin_Read = HW_Pin_Read,
    .Delay_us = HW_Delay_us
};
```

---

## Design Pattern 3: 设备驱动 (依赖注入总线)

当用户要求编写传感器设备驱动（如 MPU6050）时，使用依赖注入模式：

### 1. BSP层：设备抽象 (`bsp_mpu6050.h`)
```c
#include "bsp_sw_i2c.h"  // 引入底层总线头文件

/* --- 核心寄存器地址 --- */
#define MPU6050_ADDR 0x68

/* --- 传感器对象类 --- */
typedef struct mpu_dev {
    /* 1. 依赖注入：I2C 总线指针 */
    sw_i2c_t *bus;
    uint8_t  dev_addr;

    /* 2. 解析后的物理数据 */
    float accel_x, accel_y, accel_z;
    float gyro_x, gyro_y, gyro_z;
    float temp;

    /* 3. 硬件相关接口 */
    void (*Delay_ms)(uint32_t ms);
} mpu_t;

/* --- 3. 对外 API --- */
uint8_t MPU6050_Init_Device(mpu_t *mpu);
void MPU6050_Read_Data(mpu_t *mpu);
```

### 2. APP层：设备实例化 (`mpu6050.c`)
```c
#include "main.h"
#include "bsp_mpu6050.h"
#include "sw_i2c.h"  // 引入 I2C 实例

/* ================= 1. 设备对象实例化 ================= */
mpu_t MPU6050 = {
    .bus = &Soft_I2C1,  // 依赖注入：绑定 I2C 总线
    .dev_addr = MPU6050_ADDR,
    .Delay_ms = HAL_Delay
};

/* ================= 2. 初始化入口 ================= */
void App_MPU6050_Init(void) {
    MPU6050_Init_Device(&MPU6050);
}
```

**关键点**:
- 设备驱动依赖总线驱动
- 通过指针注入总线对象（`.bus = &Soft_I2C1`）
- 不需要修改总线驱动代码即可更换引脚

---

## 完整项目结构示例

```
BSP/
├── 霍尔电机/
│   ├── bsp_encoder_motor.h    # 电机核心框架（纯逻辑）
│   ├── bsp_encoder_motor.c    # 电机核心实现
│   ├── encoder_motor.h        # 电机应用层声明
│   └── encoder_motor.c        # 电机硬件绑定
│
├── 总线sw_i2c/
│   ├── bsp_sw_i2c.h           # I2C 核心框架
│   ├── bsp_sw_i2c.c          # I2C 核心实现
│   ├── sw_i2c.h              # I2C 应用层声明
│   └── sw_i2c.c              # I2C 硬件绑定
│
└── mpu6050/
    ├── bsp_mpu6050.h         # MPU6050 核心框架
    ├── bsp_mpu6050.c         # MPU6050 核心实现
    ├── mpu6050.h             # MPU6050 应用层声明
    └── mpu6050.c             # MPU6050 实例化
```

---

## Interaction Guidelines (AI 交互指南)

### 1. 授人以渔
- 给出代码前，必须先用简练的语言解释**"为什么要这么设计"**
- 例如："我们将底层 API 关在 BSP 层，是为了让你以后换芯片时一行都不用改"

### 2. 引导验证
- 给出代码后，不要就此结束
- 提供测试该代码的具体步骤
- 例如："把这段代码烧进去，用手转动电机，观察 RTT Viewer 里 encoder_speed 是否变化"

### 3. 精准排错
当用户抛出编译错误或日志时：
- 不要说废话，直接指出报错的根本原因
- 分点列出排查步骤
- 提供明确的代码修改指示或 IDE 操作指导

### 4. 鼓励互动
- 在每次回答的末尾，主动抛出一个与当前进度紧密相关的"下一步"建议
- 例如："网络已经通了，需要我帮你写一个 TCP 客户端往电脑发数据吗？"

---

## 常见错误排查

### L6200E: Symbol xxx multiply defined
**原因**: 变量或函数在多个文件中定义

**排查步骤**:
1. 检查是否在头文件中定义了变量（应该用 `extern` 声明）
2. 检查是否在多个 `.c` 文件中包含了相同定义的函数
3. 清理编译缓存后重新编译

**解决方案**:
- 变量在头文件中用 `extern` 声明，在其中一个 `.c` 文件中定义
- 函数用 `static` 修饰限制作用域

---

## 学习路线建议

### 第一阶段：驱动库入门（已完成）
- LED、按键、串口
- 舵机、霍尔电机、步进电机
- **状态**: ✅ 已掌握基础分层架构

### 第二阶段：通信协议（推荐）
- I2C 总线驱动
- SPI 总线驱动
- UART 深入（DMA、中断）

### 第三阶段：传感器设备
- MPU6050 陀螺仪
- OLED 显示
- 超声波测距

### 第四阶段：算法核心
- PID 控制（位置式、增量式）
- 速度环、位置环控制
- 卡尔曼滤波

---

## 总结

这种架构设计的好处：
1. **可移植性**: 换芯片只需修改 APP 层，BSP 层完全不用动
2. **可复用性**: 一个 BSP 驱动可以绑定多个设备实例
3. **可维护性**: 逻辑清晰，调试方便
4. **可测试性**: 可以用 mock 函数替换硬件操作进行单元测试

记住：**高内聚、低耦合** 是代码设计的核心原则！

ESP32-S3视觉从机子系统
 
本文档为《基于瑞萨RA6M5与多模态AI的盲人辅助导航系统开发需求文档》的专属补充内容，仅覆盖ESP32-S3从机子系统全流程开发要求，完全对齐原文档的格式规范、开发逻辑与项目定位。
 
1. 从机子系统概述与运行环境 (Slave System Context)
 
- 系统定位：原项目的专用视觉协处理从机，以ESP32-S3为核心，作为瑞萨RA6M5主机的外置视觉识别模块，不参与核心业务决策，仅负责视觉感知、AI推理、结果上报与指令执行
- 强制硬件要求：ESP32-S3-N8R8（8MB Flash + 8MB PSRAM，无PSRAM型号无法加载YOLO模型）
- 开发框架：ESP-IDF v5.1+、乐鑫ESP-DL、乐鑫EdgeLab、Ultralytics YOLO
- 操作系统：FreeRTOS（ESP-IDF原生自带）
- 核心功能：
1. OV5640摄像头图像采集、预处理与格式转换
2. 本地轻量YOLO模型推理，实现路障、台阶、盲道、行人、车辆等导航相关目标检测
3. 与瑞萨RA6M5主机通过UART串口实现双向稳定通信
4. 接收主机下发的语音指令/控制指令，触发对应识别模式
5. 识别结果结构化打包，实时上报给瑞萨主机
6. 本地异常状态监测与故障上报
 

与瑞萨RA6M5主机通信串口
 RX（串口接收） GPIO05 UART_NUM_1，波特率115200，接收主机下发的指令 
 TX（串口发送） GPIO06 UART_NUM_1，波特率115200，向主机上报识别结果 
 GND GND 必须与瑞萨RA6M5主机共地，通信稳定核心要求 
功能按键模块 KEY_CAPTURE（手动抓拍识别） GPIO16 内部上拉输入，低电平触发单次图像识别与结果上报 
 GND GND 按键接地端 
状态指示模块 LED_RUN（运行指示灯） GPIO21 推挽输出，低电平点亮，系统正常运行时1s闪烁一次 
 LED_WARN（识别告警灯） GPIO20 推挽输出，低电平点亮，检测到危险目标时长亮 
 GND GND 电源地 
可选离线语音模块 UART_TX GPIO45 与离线语音模块通信，预留接口 
 UART_RX GPIO46 与离线语音模块通信，预留接口 
 GND GND 电源地 
 VCC 3.3V 电源输入 
 
 
 
3. 从机软件驱动与控制逻辑需求 (Slave Driver & Logic Requirements)
 
3.1 摄像头采集与图像预处理驱动 (Camera Capture Module)
 
- 底层外设要求：
- 必须使用乐鑫官方 esp32-camera 驱动库，严禁自行编写底层寄存器驱动，保证兼容性与稳定性。
- 采集参数标准化：分辨率QVGA（320×240）、像素格式RGB565、帧率10fps，匹配YOLO模型输入要求，平衡识别精度与推理速度。
- 初始化要求：
- 系统上电后，先初始化PSRAM，再初始化摄像头，帧缓存必须强制分配到PSRAM中，严禁占用片内内存。
- 初始化失败时，必须通过串口向主机上报故障码，同时LED_RUN快闪告警，3秒后自动重试初始化。
- 预处理逻辑要求：
- 采集的图像必须完成格式转换、缩放、归一化处理，完全匹配YOLO模型的输入张量要求，无需主机二次处理。
- 标准化API接口： camera_capture_once()  单次采集一帧图像、 camera_capture_start()  循环采集、 camera_capture_stop()  停止采集。
 
3.2 YOLO视觉识别推理模块 (YOLO Inference Module)
 
- 模型强制要求：
- 必须使用YOLOv8n/YOLOv5n轻量nano级模型，输入尺寸320×320，必须通过乐鑫EdgeLab完成INT8量化，转换为ESP-DL支持的 .model 格式，严禁直接使用未量化的FP32模型。
- 模型检测类别锁定：路障、台阶、盲道、行人、车辆、水坑6类导航核心目标，可根据场景扩展。
- 推理逻辑要求：
- 推理任务固定分配到ESP32-S3的PRO核（核心1），与串口通信任务核隔离，避免资源抢占导致卡顿。
- 置信度阈值可通过主机指令动态调整，默认阈值0.5，仅上报置信度≥阈值的有效目标。
- 标准化API接口： yolo_inference_once()  单次推理、 yolo_set_conf_threshold()  设置置信度阈值、 yolo_get_result()  获取结构化识别结果。
- 资源要求：
- 模型文件存储在SPIFFS文件系统中，加载时必须放入PSRAM，严禁占用片内内存，保证系统运行稳定性。
 
3.3 与瑞萨主机串口通信协议模块 (UART Communication Module)
 
- 底层外设要求：
- 必须使用ESP32-S3的硬件UART_NUM_1，波特率固定115200、数据位8、停止位1、无校验，开启DMA接收，避免数据丢包。
- 通信协议标准化（与主机端完全对齐）：
帧格式固定为： 帧头(2字节) + 指令码(1字节) + 数据长度(1字节) + 数据内容(N字节) + CRC8校验(1字节) + 帧尾(2字节) 
- 帧头固定：0xAA 0x55
- 帧尾固定：0x55 0xAA
- CRC8校验规则与主机端完全一致，校验范围为指令码+数据长度+数据内容
- 核心指令集定义：
指令码 指令方向 指令含义 数据内容说明 
0x01 主机→从机 触发单次识别 无数据内容 
0x02 主机→从机 开启循环识别 1字节：识别间隔（单位100ms） 
0x03 主机→从机 停止循环识别 无数据内容 
0x04 主机→从机 设置置信度阈值 1字节：阈值（0-100） 
0x81 从机→主机 上报识别结果 结构化目标数据（目标类型、置信度、坐标） 
0x82 从机→主机 上报故障状态 1字节：故障码 
0x83 从机→主机 指令执行回执 1字节：指令码+1字节执行状态（0=成功，1=失败） 
- 功能要求：
- 接收采用中断+状态机解析模式，严格校验帧头、帧尾、CRC校验值，无效帧直接丢弃，避免乱码错包。
- 每收到主机一条指令，必须在100ms内回复执行回执，保证主机端指令同步。
- 标准化API接口： uart_send_to_host()  向主机发送数据、 uart_get_host_cmd()  获取主机下发的指令。
 
3.4 指令解析与任务调度模块 (Task Scheduler Module)
 
- 双核任务分配要求：
采用FreeRTOS多任务调度，核心任务双核硬隔离，严禁单核过载：
- PRO核（核心1）：摄像头采集任务、YOLO推理任务（高优先级）
- APP核（核心0）：串口通信任务、指令解析任务、状态指示任务（中低优先级）
- 指令执行逻辑：
- 收到主机的导航类指令，立即触发对应识别动作，识别完成后自动上报结果；
- 收到主机的闲聊类指令标记，不触发图像识别，仅回复空闲状态回执，降低系统资源占用；
- 手动按键触发的识别，优先级与主机指令一致，识别完成后同时上报主机。
- 低功耗逻辑：
超过30秒无主机指令、无按键触发，自动进入低功耗模式，关闭摄像头，仅保留串口接收与状态指示，收到主机指令后立即唤醒。
 
3.5 状态指示与异常处理模块 (Status & Exception Module)
 
- 状态指示逻辑：
- 系统正常运行：LED_RUN 1s周期闪烁；
- 正在识别推理：LED_RUN常亮；
- 检测到危险目标（台阶、路障）：LED_WARN常亮，同时向主机上报告警级结果；
- 系统故障：LED_RUN 200ms快闪，同时向主机上报故障码。
- 异常处理要求：
- 摄像头初始化失败、模型加载失败、串口通信异常，必须在500ms内向主机上报对应故障码；
- 连续3帧采集失败，自动重启摄像头模块；
- 系统看门狗必须开启，出现死机异常时自动重启，保证设备可用性。
 
 
 
4. 从机专属开发全流程SOP
 
完全对齐原文档的分阶段开发流程，仅覆盖从机开发内容，与主机开发流程同步适配。
 
 
 
🟢 从机第一阶段：基础硬件驱动调通
 
目标：完成ESP32-S3的基础外设驱动开发，实现摄像头正常采集、串口正常收发，硬件100%可用
Step 1: 工程创建与基础环境配置
 
1. 基于ESP-IDF v5.1+创建空白工程，配置PSRAM、Flash大小为8MB，开启SPIFFS文件系统
2. 导入 esp32-camera 驱动组件，配置摄像头引脚与采集参数，匹配管脚映射表
3. 配置UART串口参数，开启DMA接收，完成串口初始化代码编写
Step 2: 摄像头采集功能调通
4. 编写摄像头初始化、单次采集、循环采集代码，实现QVGA分辨率图像采集
5. 通过串口打印采集的图像大小、帧率信息，验证采集功能正常，无丢帧、花屏问题
6. 编写图像预处理函数，实现RGB565格式转换、缩放等基础功能，匹配后续模型输入要求
Step 3: 串口通信功能调通
7. 编写串口发送、中断接收代码，实现与USB-TTL模块的正常收发
8. 实现约定的帧格式解析状态机，完成帧头、帧尾、CRC校验功能，可正确解析有效数据包
9. 与瑞萨RA6M5主机联调，实现简单的指令收发与回执，验证双向通信链路稳定
Step 4: Git 图形化存档
硬件驱动100%调通后：
10. 打开VSCode源代码管理，暂存所有修改过的工程文件
11. 提交信息填写： feat: 完成ESP32从机基础硬件驱动，摄像头与串口通信调通 
12. 提交并合并到main分支，同步到远程仓库
 
 
 
🔵 从机第二阶段：YOLO模型部署与推理功能调通
 
目标：完成YOLO模型的训练、转换与部署，实现本地图像推理与结果解析，核心视觉功能可用
Step 1: 导航场景YOLO模型训练与转换
 
1. 采集导航场景数据集（路障、台阶、盲道等），完成标注，按YOLO格式整理数据集
2. 基于YOLOv8n训练自定义模型，输入尺寸320×320，完成训练后导出ONNX格式模型
3. 通过乐鑫EdgeLab在线工具，完成模型INT8量化，转换为ESP-DL支持的 .model 格式
Step 2: 模型部署与推理功能实现
4. 将转换好的模型文件烧录到ESP32的SPIFFS文件系统中，编写模型加载代码
5. 基于ESP-DL库编写推理代码，实现图像输入→模型推理→结果解析全流程
6. 编写检测结果过滤、NMS非极大值抑制代码，输出结构化的目标类型、置信度、坐标信息
Step 3: 采集与推理联动调通
7. 实现摄像头采集→图像预处理→模型推理的全流程联动，通过串口打印识别结果
8. 优化双核任务分配，将推理任务固定到PRO核，保证推理帧率≥5fps，无卡顿、死机问题
9. 调优置信度阈值，平衡识别精度与误报率，适配盲人导航场景需求
Step 4: Git 图形化存档
YOLO推理功能100%调通后：
10. 暂存所有修改的代码与模型文件
11. 提交信息填写： feat: 完成YOLO模型部署，实现本地路障识别与结果解析 
12. 提交并合并到main分支，同步到远程仓库
 
 
 
🟣 从机第三阶段：与瑞萨主机全协议联调
 
目标：实现与瑞萨主机的全指令集联动，完成指令接收→动作执行→结果上报的全闭环
Step 1: 全指令集功能实现
 
1. 编写指令解析代码，实现主机下发的所有指令的对应处理逻辑
2. 实现识别结果结构化打包与上报功能，严格遵循约定的帧格式
3. 实现故障状态上报、指令回执功能，完成协议全功能覆盖
Step 2: 与瑞萨主机双向联调
4. 与瑞萨主机端联调，验证所有指令的收发、执行、回执正常，无丢包、错包
5. 验证主机语音指令触发识别、识别结果上报主机、主机语音播报的全流程联动正常
6. 模拟异常场景，验证故障上报功能正常，主机可正确接收并处理
Step 3: 资源占用与稳定性优化
7. 优化任务栈大小、优先级，避免内存溢出、任务抢占问题
8. 连续72小时开机测试，验证系统无死机、无内存泄漏、通信稳定
9. 优化低功耗逻辑，降低空闲状态下的功耗，适配穿戴式设备需求
Step 4: Git 图形化存档
全协议联调完成后：
10. 暂存所有优化后的代码
11. 提交信息填写： feat: 完成与瑞萨主机全协议联调，实现视觉识别全流程闭环 
12. 提交并合并到main分支，同步到远程仓库
 
 
 
🟤 从机第四阶段：场景适配与最终验收
 
目标：适配盲人导航真实场景，完成功能优化与最终验收，交付可用版本
Step 1: 真实场景适配优化
 
1. 室内外不同光线、不同场景下的识别精度调优，优化模型与阈值
2. 优化危险目标的告警逻辑，台阶、路障等高危目标优先上报
3. 配合主机端，完成语音交互→视觉识别→语音播报的全场景体验优化
Step 2: 功能边界测试与异常兜底
4. 完成所有功能的边界测试，覆盖所有异常场景，保证系统无崩溃
5. 完善异常兜底逻辑，出现故障时可自动恢复，不影响主机核心功能
Step 3: 最终版本编译与发布
6. 生成最终稳定版本固件，整理固件烧录说明与接口文档
7. 提交最终版本到Git仓库，提交信息填写： feat: 交付ESP32从机最终稳定版本，适配盲人导航全场景 
8. 同步更改到远程仓库，完成从机全流程开发
