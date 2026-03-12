# The following lines of boilerplate have to be in your project's
# CMakeLists in this exact order for cmake to work correctly
cmake_minimum_required(VERSION 3.16)

# Add esp-dl components
list(APPEND EXTRA_COMPONENT_DIRS components/esp-dl/esp-dl components/esp-dl/models/human_face_detect)

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(wifi_connect_yolo)
