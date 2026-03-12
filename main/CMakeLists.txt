# 注册组件：包含 main.c 和搜索到的所有源文件，并指定头文件(.h)目录
idf_component_register(
    SRCS "main.c"
         "APP/Src/app_wifi.c"       
         "BSP/Src/bsp_wifi.c"   
         "APP/Src/app_video_server.c"
         "BSP/Src/bsp_camera.c"
         "APP/Src/app_doubao_client.c"
         "APP/Src/app_uart.c"
         "APP/Src/app_message.c" 
         "BSP/Src/bsp_uart.c"
         "APP/Src/app_audio.c"
         "BSP/Src/bsp_audio.c"
         "APP/Src/app_yolo.cpp"  # 👈 极其重要：必须改成 .cpp 才能调用深度学习库！
         
    INCLUDE_DIRS "."
                 "APP/Inc"      
                 "BSP/Inc"

    # 👈 极其重要：必须声明依赖！否则找不到头文件
    # dl 是深度学习库，esp32-camera 是摄像头库，nvs_flash 是 Wi-Fi 需要的
    REQUIRES nvs_flash esp_http_client esp32-camera esp-dl human_face_detect esp_wifi esp_http_server esp-tls json driver
)