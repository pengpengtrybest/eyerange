---
name: eye-range-rk3588
description: Build, deploy, and tune the RK3588 stereo-camera + YOLO NPU distance-measurement app (eye_range) on the OrangePi 5 Max — cross-compiling from WSL, syncing a board-derived sysroot, and optimizing NPU/SGBM throughput. Also use when adding Qt UI, V4L2 dual-lens capture, or RKNN inference to that project.
metadata:
  short-description: RK3588 双目 YOLO 测距工程（交叉编译/部署/调优）
---

# eye_range (RK3588 双目 YOLO 测距)

本技能描述 `eye_range` 工程的运作方式与踩过的坑。工程根目录：`/home/peng/eye_range`。

## 何时用

- 修改/扩展该工程：采集、识别、测距、Qt 界面、标定；
- 交叉编译并部署到开发板（本机 WSL → OrangePi 5 Max）；
- 排查板上的运行问题（NPU、相机、显示、性能）。

## 必须知道的约束

- **不要在板上重新编译**：本机交叉编译 + 只传自研可执行文件/模型/配置。
  OpenCV、Qt、librknnrt 都用板上的；传 Qt 库反而会与板上插件版本冲突。
- **sysroot 从板上来**：`build/sysroot` 由 `scripts/bootstrap_sysroot.sh` 生成
  （工具链 libc 打底 + 板上 OpenCV 头/库/依赖闭包）。换板子或板上升级 OpenCV 后必须重跑。
- **Qt**：编译期用本机 `/opt/qt5-arm`（5.15.2），运行期用板上 5.15.3，靠同 minor 二进制兼容。
  板上没有 Qt 开发头文件，不要去装。
- **链接**：交叉链接目标机共享库必须带 `-Wl,--allow-shlib-undefined`，
  否则 OpenCV 的传递依赖（png/jpeg/tiff/protobuf）会报未定义符号。
- **测距性能**：SGBM 只能在目标框附近的小 ROI 上做（ROI 宽度 = 框宽 + 视差范围，
  高度 ≤128 行），并且 `cv::setNumThreads(1)`，并行交给自己的线程池
  （默认 6 线程）。整幅匹配或线程超订会慢一个数量级。
- **视差范围决定最近可测距离**：`Z_min = fx·B / num_disparities`（本标定即 `62.36 / d`）：
  `num_disparities: 128` → 最近 487mm，**192 → 325mm**，256 → 244mm。目标近于该上限时
  SGBM 只会在被截断的范围里给出错误匹配（实测 46cm 的标定板：128 下报 31px/2.0m，
  192 下报 135.8px/459mm）。近距测试/近距场景先把 `num_disparities` 提到 192，
  代价约 20–25% 测距耗时（13 目标：13.0–13.8fps → 10.7–11.6fps）。
- **不要遍历容器的同时 push_back**：GCC 7.5 `-O3` 会因此生成错误代码
  （本项目在 `DistanceFilter::update` 上真实踩过）。
- **全屏是布局最小尺寸的函数**：板上显示器可能只有 1024x768，只要任一控件的
  `minimumSize` 偏大，窗口就会被撑到比屏幕还大，表现为“`showFullScreen()` 不生效”。
  改界面后要用 `xwininfo -root -tree | grep eye_range` 核对窗口几何是否等于屏幕尺寸。
- **不要用真正的 EWMH 全屏**：这块板的 X 没有可用 GL 驱动（`libGL: failed to load
  driver: rockchip`），xfwm4 合成器在 EWMH 全屏下整屏软件重绘会**把整机拖到硬重启**
  （无日志、无 panic、温度正常、看门狗未启用，只有全屏时复现）。用无边框窗口铺满
  `availableGeometry()` 代替；确认合成器状态：
  `xfconf-query -c xfwm4 -p /general/use_compositing`（板上有 `board_display_check.sh`）。
- `QLabel` 显示 pixmap 时 `minimumSizeHint` 会跟着图变大，形成“窗口越缩越大”的正反馈，
  必须设 `QSizePolicy::Ignored`，否则铺满/全屏尺寸会失控。

## 标准流程

```bash
cd /home/peng/eye_range
scripts/bootstrap_sysroot.sh     # 首次 / 换板 / 板上 OpenCV 变化后
scripts/build_aarch64.sh         # 交叉编译，产物在 build/aarch64
scripts/deploy_board.sh          # 上传到 /home/orangepi/eye_range
scripts/run_board.sh sysinfo     # 板上自检
scripts/run_board.sh headless 15 # 无界面跑 15 秒，截图与统计写到 out/
scripts/run_board.sh ui          # 在 HDMI 显示器上启动 Qt 界面
```

界面默认全屏（`F11` 切换 / `Esc` 退出，命令行 `--fullscreen` / `--windowed` 覆盖）。

标定：`scripts/run_board.sh calib-capture` → `scripts/run_board.sh calib-solve`
（产出 `config/calib.yaml`，距离才从“估算”变成“实测内参”）。

标定的两条铁律：

1. **只用一种角点检测器**。`findChessboardCornersSB` 与经典 `findChessboardCorners`
   的角点排序约定不同，混用会让部分视角对应关系翻转，实测 RMS 从 0.4px 恶化到 24.5px。
   解算统一走 SB（检不到就跳过该视角），实时预览才用经典算法。
2. **距离与棋盘格真实边长成正比**。格长填错时不要重新采集图像，直接
   `calibrate --square <真实格长>` 重解即可。可用卷尺实测距离反推：
   `真实格长 = 假设格长 × 实测距离 / app 读数`。

远程辅助标定三板斧：

- `eye_range_calib capture --gui`：板子显示器上全屏取景器，实时显示检出状态与拒收原因；
- `scripts/board_calib_analyze.py`：逐对检查图像几何一致性（dcy/dcx/尺度/清晰度）；
- `scripts/board_calib_verify.py`：验证极线对齐误差（好标定 < 0.5px）并推算标定板距离。

> SSH 启动板上 GUI 时要 `setsid ... </dev/null >/dev/null 2>&1 &`：
> 普通 `nohup ... &` 会让后台进程占住 SSH 通道，命令迟迟不返回。

排查“界面把板子搞重启”的方法：用 `scripts/board_stress_test.sh` 带监控跑，
对比 headless / 窗口 / 无边框铺满 / 真全屏四种形态，并核对负载与温度
（若低负载也重启，就不是功耗问题，优先怀疑显示/合成器路径）。

### 已确认的坑：界面会让这块板整机硬重启

在 OrangePi 5 Max 实测：**无界面**跑满配可稳定 260s+（检测/测距正常），
**任何 Qt 界面**（xcb 全屏 ~20s、xcb 无边框 ~2min、linuxfb ~2min）都会硬重启。
排除项：应用/系统内存（RSS 与 MemAvailable 稳定）、温度（<64°C）、CPU/NPU 负载
（重启前仅 1.5）、X/合成器（linuxfb 同样重启）、内核 panic（panic=0 且无日志）。
硬重启且无日志 → 硬件级复位，优先怀疑**供电瞬时跌落**和**显示通路(VOP/HDMI)**。
排查工具：`board_stress_test.sh`、`board_kiosk_test.sh`、`board_display_check.sh`、
`board_boot_log.py`、`board.py stream`（实时跟 dmesg -w）。

**最终结论（2026-10-09）**：**换电源后问题消失**——满配界面连跑 300s / 真全屏 180s
全部稳定（负载 5.17~5.59、67°C），而换电源前负载仅 1.5 也重启。
所以这类"无日志硬重启"要**先查供电**（5V/4A 以上 DC，别用电脑 USB 口）。
中途"关合成器就好了""无边框就好了"都是把崩溃时间延后的假象，不要据此下结论，
必须跑到稳定态明显超出原崩溃时间（本例至少 5 分钟）。

## 参考

- 板卡/相机/软件版本等实测事实：[references/board-facts.md](references/board-facts.md)
- 交叉编译与部署细节、常见报错：[references/build-and-deploy.md](references/build-and-deploy.md)
- 工程内过程文档：`doc/01`–`doc/06`（设计、编译、模块、手册、性能、排错）

## 验证改动的方式

1. 没有相机时用离线图片对自测：
   `./bin/eye_range --image-pair testdata/left.png testdata/right.png --headless --duration 6`
   （板上 `testdata/` 已有参考双目图，13 个目标、距离 ~1.2–7m）。
2. 有相机时看 headless 打印的 `采集/识别/测距` 三个 fps 与端到端延迟。
3. 界面改动用 `gnome-screenshot`（`DISPLAY=:0 XAUTHORITY=/home/orangepi/.Xauthority`）
   抓屏后做灰度/分区统计来判断是否真的渲染出来了。
