# AGENTS.md —— eye_range 项目总纲

> 任何智能体（或新来的协作者）进入本仓库：**先读本文件**，再按需读 `doc/`、`skills/`。
> 本文件统管三件事：**项目文件夹契约**、**项目工具清单**、**标准工作流程**。

---

## 1. 项目是什么

Orangepi 5 Max（RK3588）+ DECXIN 双目相机上的**双目 YOLO 识别 + 立体测距**工程：

```
双目采集(V4L2/MJPG) → NPU 跑 YOLOv8 识别 → 目标级窄带 SGBM 测距 → Qt 全屏界面
```

本机（WSL）交叉编译 → 产物部署到板子运行；板上只用系统自带的 OpenCV / Qt / RKNN 运行时。

## 2. 必须知道的事实

| 项 | 值 |
|---|---|
| 开发板 | `orangepi@192.168.228.56`（Ubuntu 22.04 / kernel 6.1.43-rockchip / 8 核 / 8GB）。**IP 走 DHCP 会变**，当前地址以 `scripts/board_env.sh` 为准 |
| 相机 | DECXIN UVC 双目，`/dev/video0` 输出左右拼接，MJPG 2560x960@30（每目 1280x960） |
| 板上软件 | OpenCV 4.5.4（含 contrib）/ Qt 5.15.3 运行时 / librknnrt 2.3.2 / Xorg 跑在 `:0` |
| 本机工具链 | `/opt/toolchains/gcc-linaro-7.5.0-...-aarch64-linux-gnu`（GCC 7.5） |
| 本机 Qt | `/opt/qt5-arm`（Qt 5.15.2 交叉库，仅编译期用） |
| 凭据 | 只放本地 `scripts/board_env.sh`（**已 gitignore，禁止入库**） |

> **动手前先核对板子身份**：板子可能被重刷或换成另一块。先跑 `scripts/run_board.sh sysinfo`，
> 确认内核 / 系统 / OpenCV / Qt 版本与上表一致；不一致就别改代码——典型症状是
> `cd: /home/orangepi/eye_range: No such file or directory`（部署目录不存在）
> 或产物 `ldd` 一堆 `not found`（新镜像没装 OpenCV/Qt）。处理见 `doc/06` 第 11 节。
> 连接参数优先级：**环境变量 > `scripts/board_env.sh` > `scripts/board_env.defaults.sh`**。

## 3. 目录契约

| 目录 / 文件 | 用途 | 入库 | 主要维护者 |
|---|---|---|---|
| `src/` | C++ 源码（capture / detect / stereo / fusion / app / ui / tools） | ✅ | 视觉流水线 agent、界面 agent |
| `build/` | 编译产物 + 交叉 sysroot + 部署 bundle + 板端回传数据 | ❌（可重建） | 构建部署 agent |
| `doc/` | 过程文档 01~07 | ✅ | 全体（改完要更新对应文档） |
| `logs/` | 板端实测日志（每次测试一个子目录） | ✅ | 板端测试 agent |
| `scripts/` | 构建/部署/运行/诊断脚本 + `agents/` 智能体定义 | ✅ | 构建部署 agent |
| `skills/eye-range-rk3588/` | 沉淀的技能（板卡事实、构建要点、踩坑） | ✅ | 全体（踩坑后补） |
| `model/` `config/` | RKNN 模型与标签、运行配置与标定文件 | ✅ | 视觉流水线 agent |
| `reference/` | 第三方开源参考工程（各自有许可证） | ❌（见 README 的 clone 地址） | — |
| `agent.md` / `AGENTS.md` | 本文件（项目总纲） | ✅ | 全体 |

**写文件的所有权**：同一时刻一个文件只允许一个智能体修改；跨文件依赖先约定接口再动手。

## 4. 项目工具清单（`scripts/`）

| 工具 | 作用 | 何时用 |
|---|---|---|
| `board_env.defaults.sh` / `board_env.sh` | 连接参数默认值 / 本地私有凭据 | 首次配置 |
| `board.py` | SSH/SFTP 助手（`run` / `stream` / `put` / `get` / `sudo`） | 与板子交互的底座 |
| `bootstrap_sysroot.sh` | 从板上同步 OpenCV 头/库/依赖闭包到 `build/sysroot` | 首次、换板、板上 OpenCV 变化后 |
| `build_aarch64.sh` | 交叉编译（产物 `build/aarch64/`） | 每次改完代码 |
| `deploy_board.sh` | 打包 `build/bundle` 并上传到板 | 编译成功后 |
| `run_board.sh` | 板上运行：`sysinfo`/`ui`/`headless`/`calib-capture`/`calib-solve`/`raw` | 日常运行 |
| `run_stress.sh` | 压测 + 自动把监控/内核/应用日志取回 `logs/<时间戳>_主题/` | 性能与稳定性验证 |
| `board_stress_test.sh` | 板端：带监控跑 `eye_range`（频率/温度/ADC/负载） | 由 run_stress 调用 |
| `board_kiosk_test.sh` | 板端：停 X 用 linuxfb 跑（绕开 X 验证） | 怀疑显示通路时 |
| `board_display_check.sh` | 板端：显示/合成器/GL 状态检查（`check`/`fix`/`restore`） | 显示异常时 |
| `board_boot_log.py` | 解析上次启动的内核日志 | 异常重启后排障 |
| `board_calib_analyze.py` / `_solve.py` / `_verify.py` | 标定图像质量检查 / 稳健解算 / 极线对齐自检 | 标定流程 |
| `manage.sh` | **项目管理入口**：`overview` / `tools` / `check` / `workflows` | 接手项目、交接、体检 |

## 5. 标准工作流

### 5.1 接手项目（首次）
```bash
scripts/manage.sh overview      # 看目录与产物是否齐全
scripts/manage.sh check         # 本机工具链/sysroot/板子连通性体检
scripts/bootstrap_sysroot.sh    # 若 build/sysroot 缺失
```

### 5.2 改代码 → 编译 → 部署 → 验证（日常主线）
```bash
scripts/build_aarch64.sh                       # 编译
scripts/deploy_board.sh                        # 部署
scripts/run_board.sh sysinfo                   # 环境自检
scripts/run_board.sh headless 20               # 无界面跑 20 秒（最快验证链路）
```
**无相机时**用离线图片对自测（板上已有 `testdata/`）：
`./bin/eye_range --image-pair testdata/left.png testdata/right.png --headless --duration 6`

### 5.3 板上运行界面
```bash
scripts/run_board.sh ui                # 默认真全屏（合成器开着也稳定）
scripts/run_board.sh ui --low-power    # 供电/散热余量紧张时
```

### 5.4 标定（距离从"估算"变"可信"）
```bash
scripts/run_board.sh calib-capture     # 举棋盘格，识别到自动存图（--gui 有取景器）
scripts/run_board.sh calib-solve       # 稳健解算 → config/calib.yaml
# 校验：极线对齐误差应 < 0.5px；格长填错只需 --square 重解，不必重采
```

### 5.5 性能/稳定性验证与日志归档
```bash
APP_TIMEOUT=300 scripts/run_stress.sh borderless              # 5 分钟
APP_TIMEOUT=180 scripts/run_stress.sh fullscreen --fullscreen # 真全屏
APP_TIMEOUT=300 scripts/run_stress.sh headless --headless --duration 300
```
日志一律落到 `logs/<时间戳>_<主题>/`（监控采样 + 前后内核日志 + 应用输出 + `summary.md`）。

### 5.6 排障入口
- 板子**无日志硬重启** → 先查供电（见 `doc/06` 第 8~10 节），用 `board_stress_test.sh` 复现取证
- 界面异常/性能异常 → `board_display_check.sh` + `run_stress.sh`
- 检测/测距异常 → `run_board.sh headless`，再看 `logs/` 里最近一次采样

## 6. 智能体编制（`scripts/agents/`）

| 文件 | 角色 | 所有范围 |
|---|---|---|
| `00-orchestrator.md` | 总协调：拆任务、定边界、守验收 | 全局 |
| `10-build-deploy-agent.md` | 构建与部署链路 | `CMakeLists.txt`、`cmake/`、`scripts/` |
| `20-vision-pipeline-agent.md` | 采集/识别/测距正确性与性能 | `src/{capture,detect,stereo,fusion,app,common}` |
| `30-qt-ui-agent.md` | 界面显示与交互 | `src/ui/`、`config` 的 `ui:` 段 |
| `40-board-test-agent.md` | 板端实测与数据记录 | `logs/`、`doc/05`、`doc/06` |

派发子智能体时：只把**对应角色的文件**投喂给它，并在任务里写明"可写范围"与"完成判据"。

## 7. 红线（违反会造成真实损失）

1. **凭据不入库**：板卡密码只放 `scripts/board_env.sh`（gitignore）；不要在代码/文档里写死密码。
2. **不传本机 Qt/OpenCV 库到板上**：链接期用本机 `/opt/qt5-arm`，运行期用板上 5.15.3；
   传过去反而会和板上插件版本冲突。
3. **不在板上编译**：板上只运行；编译一律本机交叉编译。
4. **遍历容器时不要 `push_back`**：GCC 7.5 `-O3` 会生成错误代码（本项目已真实踩过）。
5. **测距不要退回整幅稠密匹配**：必须保持"目标级窄带 ROI + 每目标并行"，
   否则从 44ms 退化到 770ms。
6. **改界面必须核对窗口几何**：`xwininfo` 尺寸要等于屏幕可用区域，否则会被布局撑爆。
7. **无日志硬重启优先怀疑供电**，不要先改代码（历史教训见 `doc/06`）。

## 8. 完成判据（Definition of Done）

- 代码改动：`scripts/build_aarch64.sh` 通过 → `deploy_board.sh` 成功 → 板上跑通一次（headless 或 ui）
- 性能相关改动：给出**改动前后同一场景的实测数字**（采集/识别/测距 fps、耗时）
- 标定相关改动：给出双目 RMS 与极线对齐误差
- 稳定性相关改动：用 `run_stress.sh` 跑到**明显超过原崩溃时间**（至少 5 分钟）
- 文档同步：`doc/` 对应章节 + 本文相关表格
- 踩到新坑：补进 `doc/06` 与 `skills/eye-range-rk3588/`

## 9. 命令速查

```bash
scripts/manage.sh overview|tools|check|workflows   # 项目管理入口
scripts/bootstrap_sysroot.sh                       # 一次性：建交叉 sysroot
scripts/build_aarch64.sh && scripts/deploy_board.sh
scripts/run_board.sh sysinfo|ui|headless <sec>|calib-capture|calib-solve|raw "<cmd>"
APP_TIMEOUT=300 scripts/run_stress.sh borderless   # 压测 + 日志归档
python3 scripts/board.py run|stream|put|get|sudo "<...>"
```
