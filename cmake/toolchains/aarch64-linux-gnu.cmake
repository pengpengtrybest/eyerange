# RK3588 (OrangePi 5 Max) 交叉编译工具链
#
# 约定：
#   build/sysroot  = 从开发板同步来的目标机头文件/库（OpenCV 等），
#                    保证“本机编译用的 ABI”和“板上运行时的 ABI”完全一致。
#   /opt/qt5-arm   = 本机已有的 aarch64 Qt 5.15 (交叉编译头文件/库)，
#                    运行时由板上系统 Qt 5.15.3 提供（同 minor 二进制兼容）。

set(CMAKE_SYSTEM_NAME Linux)
set(CMAKE_SYSTEM_PROCESSOR aarch64)

set(AARCH64_TOOLCHAIN_ROOT "/opt/toolchains/gcc-linaro-7.5.0-2019.12-x86_64_aarch64-linux-gnu"
    CACHE PATH "aarch64 交叉工具链根目录")
set(AARCH64_TOOLCHAIN_PREFIX "${AARCH64_TOOLCHAIN_ROOT}/bin/aarch64-linux-gnu-"
    CACHE FILEPATH "aarch64 交叉工具链前缀")

set(CMAKE_C_COMPILER "${AARCH64_TOOLCHAIN_PREFIX}gcc")
set(CMAKE_CXX_COMPILER "${AARCH64_TOOLCHAIN_PREFIX}g++")
set(CMAKE_AR "${AARCH64_TOOLCHAIN_PREFIX}ar")
set(CMAKE_RANLIB "${AARCH64_TOOLCHAIN_PREFIX}ranlib")
set(CMAKE_STRIP "${AARCH64_TOOLCHAIN_PREFIX}strip")

# 目标机 sysroot（从板子同步）
get_filename_component(_eye_repo_root "${CMAKE_CURRENT_LIST_DIR}/../.." ABSOLUTE)
set(EYE_SYSROOT "${_eye_repo_root}/build/sysroot" CACHE PATH "目标机 sysroot 路径")
set(EYE_QT_ROOT "/opt/qt5-arm" CACHE PATH "aarch64 Qt5 交叉编译根目录")

set(CMAKE_SYSROOT "${EYE_SYSROOT}")
set(CMAKE_FIND_ROOT_PATH "${EYE_SYSROOT}" "${EYE_QT_ROOT}")

set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY BOTH)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE BOTH)
set(CMAKE_FIND_ROOT_PATH_MODE_PACKAGE BOTH)

# 目标机 glibc 的最低版本（Ubuntu 22.04 / glibc 2.35），避免误用本机新符号
set(EYE_TARGET_GLIBC "2.35" CACHE STRING "目标机 glibc 版本")
