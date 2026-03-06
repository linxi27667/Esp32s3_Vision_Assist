# ESP32-S3 TIMER Project - AI Coding Guidelines

## Project Architecture
This is an ESP-IDF project targeting ESP32-S3 microcontroller, implementing timer-related functionality with hardware peripherals. 

### Key Components
- **main.c**: Entry point with FreeRTOS task creation (app_main)
- **Hardware/**: Peripheral abstractions (led.c/h, key.c/h, uart.c/h, timer.c/h)
- **Build System**: CMake-based via ESP-IDF framework

### Hardware Abstraction Pattern
Each peripheral follows a consistent structure:
- Header (.h) with pin defines, function declarations
- Implementation (.c) with GPIO config and control functions
- Example: `led.h` defines `LED_PIN 48`, `LedGpioConfig()`, `LED_On()/Off()`

## Development Workflows

### Build & Flash
```bash
idf.py build          # Compile project
idf.py flash          # Upload to ESP32-S3
idf.py monitor        # Serial monitor (115200 baud)
idf.py flash monitor  # Flash then monitor
```

### Configuration
- Use `idf.py menuconfig` for Kconfig options
- sdkconfig auto-generated; edit via menuconfig
- Common settings: UART baud, GPIO pins, FreeRTOS config

## Code Patterns

### Peripheral Initialization
Always call init functions in app_main before using peripherals:
```c
void app_main(void) {
    LedGpioConfig();  // Configure GPIO first
    uart_init();      // Then initialize driver
    // Create tasks...
}
```

### FreeRTOS Tasks
- Use `xTaskCreate()` for concurrent operations
- UART RX typically runs in dedicated task: `uart2_rx_task()`
- Task names prefixed with component (e.g., "MyTask")

### GPIO Configuration
Follow ESP-IDF gpio_config_t pattern:
```c
gpio_config_t config = {
    .pin_bit_mask = (1ULL << PIN_NUM),
    .mode = GPIO_MODE_OUTPUT,
    .pull_up_en = GPIO_PULLUP_ENABLE,
    .intr_type = GPIO_INTR_DISABLE
};
gpio_config(&config);
```

### UART Usage
- Primary UART: UART_NUM_2 on pins 9(TX)/10(RX)
- Config: 115200 baud, 8N1, no flow control
- Use `uart_read_bytes()` with timeout for RX
- Echo received data back for debugging

### LED Control
- Active LOW: gpio_set_level(LED_PIN, 0) turns ON
- Use IRAM_ATTR for ISR-safe functions

## File Organization
- **main/CMakeLists.txt**: Register all .c sources from Hardware/
- Include dirs: "." and "Hardware/"
- No separate components; all in main/

## Common Pitfalls
- Missing semicolon in vTaskDelay() calls
- GPIO pin conflicts (check sdkconfig)
- UART buffer overflows (use appropriate buffer sizes)
- FreeRTOS task stack size too small (1024+ recommended)

## Key Files
- [main/main.c](main/main.c): Application entry
- [main/Hardware/uart.c](main/Hardware/uart.c): Serial communication
- [main/Hardware/led.c](main/Hardware/led.c): GPIO LED control
- [sdkconfig](sdkconfig): ESP-IDF configuration</content>
<parameter name="filePath">e:\MCU\esp32\Myesps3Project\TIMER\.github\copilot-instructions.md