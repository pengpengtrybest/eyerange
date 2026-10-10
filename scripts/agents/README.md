# scripts/agents —— 智能体编制

> **先读根目录 [`AGENTS.md`](../../AGENTS.md)**（项目总纲：目录契约 / 工具清单 / 工作流程 / 红线），
> 本目录只放各角色的**职责边界**，供派发子智能体时作提示词使用。

## 编制

| 文件 | 角色 | 可写范围 | 何时派发 |
|---|---|---|---|
| [00-orchestrator.md](00-orchestrator.md) | 总协调：拆任务、定边界、守验收 | 全局（只调度，不长期占文件） | 任务跨多个模块时 |
| [10-build-deploy-agent.md](10-build-deploy-agent.md) | 构建与部署链路 | `CMakeLists.txt`、`cmake/`、`scripts/` | 改构建/依赖/部署/板卡环境 |
| [20-vision-pipeline-agent.md](20-vision-pipeline-agent.md) | 采集/识别/测距正确性与性能 | `src/{capture,detect,stereo,fusion,app,common}` | 检测/测距/帧率问题 |
| [30-qt-ui-agent.md](30-qt-ui-agent.md) | 界面显示与交互 | `src/ui/`、`config` 的 `ui:` 段 | 界面显示/布局/交互 |
| [40-board-test-agent.md](40-board-test-agent.md) | 板端实测与数据记录 | `logs/`、`doc/05`、`doc/06` | 需要真机数据与结论 |

## 派发规则

1. **只投喂对应角色**：把该 agent 文件（必要时加 `AGENTS.md`）作为提示词，
   并在任务里写明「可写范围」与「完成判据」（见 `AGENTS.md` 第 8 节）。
2. **文件所有权唯一**：同一时刻一个文件只允许一个执行者写；
   跨模块改动先由总协调约定接口，再并行。
3. **并行建议**：视觉流水线（20）与界面（30）可同时推进；
   构建部署（10）在两者定稿后再跑；板端测试（40）最后统一出数据。
4. **谁踩坑谁记录**：新问题写进 `doc/06` 与 `skills/eye-range-rk3588/`，
   工具用法变更同步到 `AGENTS.md` 第 4 节。

## 常用命令（给智能体）

```bash
scripts/manage.sh overview|tools|check|workflows|logs   # 项目管理入口
scripts/build_aarch64.sh && scripts/deploy_board.sh
scripts/run_board.sh sysinfo|ui|headless <秒>|calib-capture|calib-solve|raw "<cmd>"
APP_TIMEOUT=300 scripts/run_stress.sh borderless        # 压测 + 日志归档
```
