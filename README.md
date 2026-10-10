# eye_range — RK3588 双目 YOLO 识别 + 立体测距

在 OrangePi 5 Max（RK3588）上跑的双目视觉工程：V4L2 采集左右拼接双目 → NPU 上
YOLOv8 识别目标 → 对每个目标做立体匹配测距 → Qt 界面实时显示画面、距离与视差/深度图。
本机（WSL）交叉编译，产物传到板上运行。

## 快速开始

```bash
# 一次性：从开发板同步 OpenCV 到本机交叉 sysroot
scripts/bootstrap_sysroot.sh

# 编译 + 部署 + 运行
scripts/build_aarch64.sh
scripts/deploy_board.sh
scripts/run_board.sh sysinfo          # 环境自检
scripts/run_board.sh headless 15      # 无界面跑 15 秒（截图写到板上 out/）
scripts/run_board.sh ui               # 在板子接的显示器上打开 Qt 界面
```

标定（让距离从“估算”变“可信”）：

```bash
scripts/run_board.sh calib-capture    # 举着棋盘格，识别到就自动存图
scripts/run_board.sh calib-solve      # 解算 → config/calib.yaml
```

压测与日志归档（每次测试自动把板端监控、内核日志、应用输出取回 `logs/<时间戳>_主题/`）：

```bash
APP_TIMEOUT=300 scripts/run_stress.sh borderless    # 无边框铺满跑 5 分钟
APP_TIMEOUT=180 scripts/run_stress.sh fullscreen --fullscreen
APP_TIMEOUT=300 scripts/run_stress.sh headless --headless --duration 300
```

## 实测性能（2560x960，每目 1280x960）

| 指标                            | 数值                                |
| ------------------------------- | ----------------------------------- |
| 采集                            | 30 fps                              |
| 识别（NPU，YOLOv8n int8）       | 30 fps，推理 18–23 ms/帧           |
| 测距（目标级 ROI SGBM，6 线程） | 24 ms（无目标）/ 44 ms（13 个目标） |
| 端到端延迟                      | 30–56 ms                           |

## 目录

| 目录                   | 内容                                                              |
| ---------------------- | ----------------------------------------------------------------- |
| `src/`               | C++ 源码（capture / detect / stereo / fusion / app / ui / tools） |
| `build/`             | 交叉编译产物、sysroot、部署 bundle、板端回传数据                  |
| `doc/`               | 过程文档 01–06（设计、编译、模块、手册、性能、排错）             |
| `logs/`              | 板端实测日志归档（监控采样、内核日志、应用输出、结论）            |
| `scripts/`           | 构建/部署/运行脚本 +`agents/` 智能体定义                        |
| `skills/`            | 沉淀的技能`eye-range-rk3588`（含板卡事实与构建要点）            |
| `model/` `config/` | RKNN 模型与标签、运行配置与标定文件                               |
| `reference/`         | 参考的开源双目/YOLO 项目                                          |

## 关键设计

- **单设备左右拼接双目**：按中点切分，无需两路同步。
- **NPU 直出量化结果**：不开 `want_float`，省掉每帧反量化。
- **目标级窄带 SGBM**：ROI 宽度 = 目标框宽 + 视差搜索范围，高度 ≤128 行，
  多目标并行 6 线程，OpenCV 内部线程压到 1。相比整幅稠密匹配提速 17 倍。
- **估算与标定双模式**：没有标定文件也能跑（界面标注“未标定/估算”）。

## 稳定性须知

早期“一跑界面就整机硬重启”的根因是**电源不足**（换电源前负载仅 1.5 也重启，
且没有任何内核日志 —— 典型的电源瞬态跌落导致 SoC 复位）。更换电源后满配连续
运行 5 分钟稳定（负载 5.17、67.5°C）。现场请用 5V/4A 以上 DC 供电，
不要用电脑 USB 口或手机充电头。详见 [doc/06](doc/06-问题排查记录.md) 第 8–10 节。

项目总览见 [doc/07-项目说明.md](doc/07-项目说明.md)。

## 依赖

板上：Ubuntu 22.04 / OpenCV 4.5.4（含 contrib）/ Qt 5.15.3 运行时 / librknnrt 2.3.2。
本机：Linaro aarch64 GCC 7.5、CMake 3.30、`/opt/qt5-arm`（Qt 5.15.2 交叉库）。
详见 `doc/02-环境探测与交叉编译.md`。

## 第三方参考项目

`reference/` 目录里是参考用的开源工程，**不随本仓库提交**（各自有独立许可证），
需要时按下表自行 clone：

| 目录                                                 | 来源                                                                                |
| ---------------------------------------------------- | ----------------------------------------------------------------------------------- |
| `Stereo-Detection`                                 | https://github.com/wsk12345/Stereo-Detection                                        |
| `Object-Detection-and-Tracking-with-Stereo-Vision` | https://github.com/trankietmashiro/Object-Detection-and-Tracking-with-Stereo-Vision |
| `stereo-vision-python`                             | https://github.com/present-cjn/stereo-vision-python                                 |
| `vehicle-distance-estimation`                      | https://github.com/semihsivrikaya11/vehicle-distance-estimation                     |
| `Stereo-Vision`                                    | https://github.com/LearnTechWithUs/Stereo-Vision                                    |
| `Evision`                                          | https://github.com/jiafeng5513/Evision                                              |

`model/yolov8.rknn` 来自 Rockchip 的 rknn_model_zoo（Apache-2.0），
`model/coco_80_labels_list.txt` 为其配套标签。

## 板卡连接配置

仓库不含任何密码。首次使用先建本地私有配置（已在 `.gitignore` 中）：

```bash
cat > scripts/board_env.sh <<'EOF'
export BOARD_HOST=“板卡ip地址”
export BOARD_USER=你的用户名
export BOARD_PASS=你的密码
export BOARD_ROOT=/home/orangepi/eye_range
EOF
```
