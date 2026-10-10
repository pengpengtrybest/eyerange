cmake_minimum_required(VERSION 3.16)
project(eye_range LANGUAGES CXX)

# ---------------------------------------------------------------------------
# 双目 YOLO 识别 + 立体测距（RK3588 / OrangePi 5 Max）
#   交叉编译: cmake --toolchain cmake/toolchains/aarch64-linux-gnu.cmake
#   板上原生: cmake -S . -B build/native
# ---------------------------------------------------------------------------

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release CACHE STRING "" FORCE)
endif()

add_compile_options(-O3 -pthread)
add_compile_options(-Wall -Wextra -Wno-unused-parameter)

# 交叉链接时，目标机 OpenCV 的传递依赖（libpng/libjpeg/libtiff/protobuf ...）
# 由目标机运行时提供；这里放行共享库中的未定义符号，避免链接器强校验。
add_link_options(-Wl,--allow-shlib-undefined)

# --- RKNN (NPU) 运行时 -------------------------------------------------------
# 交叉编译时用 rknpu2 Linux aarch64 的 api 头 + librknnrt.so
set(EYE_RKNN_ROOT "/home/peng/rk3588npu/rknn-toolkit2-2.3.0/rknn-toolkit2-2.3.0/rknpu2/runtime/Linux/librknn_api"
    CACHE PATH "RKNN runtime 目录（含 include/ 与 aarch64/librknnrt.so）")
set(EYE_RKNN_INCLUDE "${EYE_RKNN_ROOT}/include" CACHE PATH "rknn_api.h 所在目录")
set(EYE_RKNN_LIB "${EYE_RKNN_ROOT}/aarch64/librknnrt.so" CACHE FILEPATH "librknnrt.so 路径")

if(NOT EXISTS "${EYE_RKNN_INCLUDE}/rknn_api.h")
  message(FATAL_ERROR "找不到 rknn_api.h: ${EYE_RKNN_INCLUDE}")
endif()
if(NOT EXISTS "${EYE_RKNN_LIB}")
  message(FATAL_ERROR "找不到 librknnrt.so: ${EYE_RKNN_LIB}")
endif()

# --- OpenCV ------------------------------------------------------------------
find_package(OpenCV REQUIRED COMPONENTS core imgproc imgcodecs calib3d ximgproc)
message(STATUS "OpenCV ${OpenCV_VERSION} : ${OpenCV_INCLUDE_DIRS}")

# --- Qt5 (可视化界面) --------------------------------------------------------
if(DEFINED EYE_QT_ROOT)
  list(APPEND CMAKE_PREFIX_PATH "${EYE_QT_ROOT}")
endif()
set(CMAKE_AUTOMOC ON)
find_package(Qt5 COMPONENTS Core Widgets QUIET)
if(Qt5Widgets_FOUND)
  message(STATUS "Qt ${Qt5Core_VERSION} : ${Qt5Widgets_DIR}")
else()
  message(WARNING "未找到 Qt5Widgets，将只构建无界面(命令行)版本")
endif()

# --- 公共库 ------------------------------------------------------------------
add_library(eye_core STATIC
  src/common/config.cpp
  src/common/log.cpp
  src/capture/v4l2_camera.cpp
  src/capture/stereo_source.cpp
  src/detect/rknn_engine.cpp
  src/detect/yolo_detector.cpp
  src/stereo/stereo_depth.cpp
  src/fusion/distance.cpp
  src/app/pipeline.cpp
)

target_include_directories(eye_core PUBLIC
  ${CMAKE_CURRENT_SOURCE_DIR}/src
  ${OpenCV_INCLUDE_DIRS}
  ${EYE_RKNN_INCLUDE}
)

target_link_libraries(eye_core PUBLIC
  ${OpenCV_LIBS}
  ${EYE_RKNN_LIB}
  Threads::Threads
)

find_package(Threads REQUIRED)

# --- 主程序（Qt 界面 / --headless 无界面）-----------------------------------
set(EYE_APP_SOURCES src/main.cpp)
if(Qt5Widgets_FOUND)
  list(APPEND EYE_APP_SOURCES src/ui/main_window.cpp src/ui/main_window.hpp)
endif()

add_executable(eye_range ${EYE_APP_SOURCES})
target_link_libraries(eye_range PRIVATE eye_core)
# 让崩溃回溯能显示函数名
target_link_options(eye_range PRIVATE -rdynamic)
if(Qt5Widgets_FOUND)
  target_link_libraries(eye_range PRIVATE Qt5::Widgets)
  target_compile_definitions(eye_range PRIVATE EYE_HAVE_QT=1)
else()
  target_compile_definitions(eye_range PRIVATE EYE_HAVE_QT=0)
endif()

# --- 标定工具 ----------------------------------------------------------------
set(EYE_CALIB_SOURCES src/tools/stereo_calib.cpp)
if(Qt5Widgets_FOUND)
  list(APPEND EYE_CALIB_SOURCES src/tools/calib_window.cpp src/tools/calib_window.hpp)
endif()
add_executable(eye_range_calib ${EYE_CALIB_SOURCES})
target_link_libraries(eye_range_calib PRIVATE eye_core)
target_link_options(eye_range_calib PRIVATE -rdynamic)
if(Qt5Widgets_FOUND)
  target_link_libraries(eye_range_calib PRIVATE Qt5::Widgets)
  target_compile_definitions(eye_range_calib PRIVATE EYE_HAVE_QT=1)
else()
  target_compile_definitions(eye_range_calib PRIVATE EYE_HAVE_QT=0)
endif()

# --- 环境自检 ----------------------------------------------------------------
add_executable(eye_range_sysinfo src/tools/sysinfo.cpp)
target_link_libraries(eye_range_sysinfo PRIVATE eye_core)
if(Qt5Widgets_FOUND)
  target_link_libraries(eye_range_sysinfo PRIVATE Qt5::Core)
  target_compile_definitions(eye_range_sysinfo PRIVATE EYE_HAVE_QT=1)
else()
  target_compile_definitions(eye_range_sysinfo PRIVATE EYE_HAVE_QT=0)
endif()

install(TARGETS eye_range eye_range_calib eye_range_sysinfo RUNTIME DESTINATION bin)
