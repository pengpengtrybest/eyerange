# 交叉编译与部署细节

## sysroot 是怎么来的

`scripts/bootstrap_sysroot.sh`：

1. `cp -a <工具链>/aarch64-linux-gnu/libc/. build/sysroot/` —— 提供 glibc/内核头/crt；
2. 上传并执行 `scripts/remote_pack_sysroot.sh`（在板上跑），它把
   `usr/include/opencv4`、`usr/lib/aarch64-linux-gnu/cmake/opencv4`、`pkgconfig/opencv4.pc`、
   全部 `libopencv_*.so*`、以及 `ldd` 出来的依赖闭包，统一铺到
   `/tmp/eye_range_sysroot_stage/usr/lib/aarch64-linux-gnu/` 再打包；
3. 下载 tarball，用 `tar xzf ... --strip-components=1 -C build/sysroot` 解包。

要点与坑：

- 依赖闭包必须用 `ldd` 自动收集，手写会漏（本项目实际需要 libjpeg8/libpng16/libtiff5/openjp2/libprotobuf23 等）。
- 板上 `/lib` 是 `/usr/lib` 的符号链接，`ldd` 输出两种路径都有；必须统一到
  `usr/lib/aarch64-linux-gnu`，否则链接器搜不到。
- 符号链接与真实文件都要复制（`cp -a` 链接本身 + `cp -a $(readlink -f ...)` 真实文件），
  否则按 SONAME 找不到。
- 排除 `libc/libstdc++/libgcc/ld-linux`，沿用工具链版本，避免产物绑定更新 glibc 符号。

## 编译

```bash
scripts/build_aarch64.sh          # 内部: cmake -S . -B build/aarch64 -DCMAKE_TOOLCHAIN_FILE=...
```

关键 CMake 配置：

```cmake
add_link_options(-Wl,--allow-shlib-undefined)   # 必须
find_package(OpenCV REQUIRED COMPONENTS core imgproc imgcodecs calib3d ximgproc)
find_package(Qt5 COMPONENTS Core Widgets QUIET) # 通过 CMAKE_PREFIX_PATH=/opt/qt5-arm
```

## 部署

```bash
scripts/deploy_board.sh
```

生成 `build/bundle/{bin,model,config}` 并递归上传到 `/home/orangepi/eye_range`，
同时 `chmod +x bin/*`（paramiko 上传不带可执行位）。

## 板上运行

```bash
scripts/run_board.sh sysinfo | headless <秒> | ui | calib-capture | calib-solve | raw "<cmd>"
```

界面模式要带上 X 认证：

```bash
DISPLAY=:0 XAUTHORITY=/home/orangepi/.Xauthority ./bin/eye_range
```

## 常见报错与处理

| 报错 | 原因/处理 |
|---|---|
| `undefined reference to png_* / TIFF* / protobuf` | 缺 `-Wl,--allow-shlib-undefined` |
| 链接找不到 `libjpeg.so.8` 等 | sysroot 里依赖落在 `lib/` 而不是 `usr/lib/`；重跑 bootstrap |
| 运行报 `error while loading shared libraries: libopencv_*` | 板上有 OpenCV，正常不该出现；检查是否误传了本机库或 rpath |
| 运行报 Qt 插件/版本错误 | 不要传 `/opt/qt5-arm/lib`；让板上 5.15.3 提供 |
| `libGL: failed to load driver: rockchip` | 正常，QWidget 走软件渲染 |
| 进程 `pkill` 不掉 | 需要代码轮询停止标志并退出 Qt 事件循环（已实现） |
| `rknn_init` 失败 / `-12` | 模型与驱动优化等级不匹配；确认 `librknnrt` 版本（板上 2.3.2 与 2.3.0 工具链兼容） |

## 性能调参速查

| 目标 | 参数 |
|---|---|
| 测距更快 | 降 `num_disparities`（128→64）、降 `band_max_height`、提 `threads`(≤6)、`roi_only=1` |
| 测距更远/更准 | 提高 `num_disparities`、提高相机分辨率、减小 `max_roi_side` 让 ROI 不降采样 |
| 识别更快 | 保持 int8 模型、单核 NPU（`--npu-core 0`）降低功耗，或三核（`all`）提高吞吐 |
| 界面更流畅 | 降 `display_scale`、增大 `display_num_disparities` 之外的显示帧间隔 |

实测基线（2560x960、无目标）：采集 30fps、推理 ~20ms、测距 24ms、端到端 30–56ms。
13 目标离线图片对：测距 44ms/帧。
