# 注册组件：包含 main.c 和搜索到的所有源文件，并指定头文件(.h)目录
idf_component_register(
    SRCS "main.c"
         "APP/Src/app_wifi.c"       # 👈 把你的应用层 C 文件加进来！
         "BSP/Src/bsp_wifi.c"   # 👈 把底层的驱动 C 文件也加进来！
         "APP/Src/app_video_server.c"
         "BSP/Src/bsp_camera.c"
         "APP/Src/app_doubao_client.c"
         "APP/Src/app_uart.c"
         "BSP/Src/bsp_uart.c"
         
    INCLUDE_DIRS "."
                 "APP/Inc"      # 确保头文件路径也在这里
                 "BSP/Inc"
)
