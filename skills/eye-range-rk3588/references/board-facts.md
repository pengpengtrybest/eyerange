# 板卡实测事实

## 目标板

| 项 | 值 |
|---|---|
| 主机名/IP | `orangepi5max` / `192.168.228.113`（用户 `orangepi`；密码只放本地 `scripts/board_env.sh`，不入库） |
| 系统 | Ubuntu 22.04.5 LTS, kernel 6.1.43-rockchip-rk3588, aarch64 |
| CPU/内存 | 8 核（4×A76 + 4×A55），约 7.9GB |
| 磁盘 | 根分区 112G，剩余 ~96G |
| NPU | `/usr/lib/librknnrt.so`（运行时 2.3.2，驱动 0.9.6） |
| OpenCV | 4.5.4，**含 contrib**（有 `libopencv_ximgproc`、头文件、`cmake/opencv4`） |
| Qt | 运行时 5.15.3 + `libqxcb.so`，**无 qtbase5-dev**（无头文件，`qmake` 不存在） |
| 显示 | `Xorg :0`（lightdm, vt7），`/dev/fb0`、`/dev/dri/card0|card1|renderD128|129`，显示器 1024x768 |
| GPU | Mali，`libGL: failed to load driver: rockchip` —— 对 QWidget 软件渲染无影响 |

## 相机

```
/dev/video0  DECXIN Camera (uvcvideo, usb-xhci-hcd.14.auto-1)
  MJPG: 1600x600 / 2560x960 / 3840x1080 / 2560x1080 / 1280x480 / 640x240 / 2560x720  全部 30fps
  YUYV: 同类分辨率但 ≤10fps（不要用）
/dev/video1  同一设备的第二个采集节点（本项目未使用）
```

- **单设备左右拼接**：整幅宽 = 左右两目之和，按中点切分。
- 工程默认 `2560x960` → 每目 `1280x960`，MJPG 30fps。
- 若发现左右颠倒，用 `capture.swap_eyes: 1` 或命令行 `--swap-eyes`。
- 实测当前环境相机对着白墙（灰度均值 237），此场景下检测目标数为 0 属正常。

## 本机（WSL）资源

| 项 | 路径 |
|---|---|
| 交叉工具链 | `/opt/toolchains/gcc-linaro-7.5.0-2019.12-x86_64_aarch64-linux-gnu`（GCC 7.5，自带 libc sysroot） |
| Qt aarch64 | `/opt/qt5-arm`（5.15.2） |
| RKNN 头/库 | `/home/peng/rk3588npu/rknn-toolkit2-2.3.0/.../rknpu2/runtime/Linux/librknn_api/{include,aarch64}` |
| 模型来源 | `/home/peng/rk3588npu/rknn_model_zoo/examples/yolov8/`（yolov8.rknn + coco 标签） |
| CMake | 3.30.5 |

注意 GCC 只有 7.5：`std::filesystem` 不可用（用 `<sys/stat.h>` 的 `mkdir` 等替代），
并且对“遍历容器时修改容器”的优化很激进。

## 模型

```
model/yolov8.rknn   输入 640x640x3 (NHWC uint8)，输出 9 个 int8 张量：
  [1,64,80,80] [1,80,80,80] [1,1,80,80]  (stride 8)
  [1,64,40,40] [1,80,40,40] [1,1,40,40]  (stride 16)
  [1,64,20,20] [1,80,20,20] [1,1,20,20]  (stride 32)
类别分数 zp=-128，scale≈0.0031；前缀 mean=0/std=255，letterbox 填充色 114
```
