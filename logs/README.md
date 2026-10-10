# logs —— 调试与实测日志

这里存放**板端实测的原始日志**，供复现与对比。每次测试一个子目录，命名
`YYYY-MM-DD_主题/`。

## 当前内容

| 目录 | 内容 |
|---|---|
| `2026-10-09_psu-retest/` | 更换电源后的界面稳定性复测（含监控采样、内核日志、结论） |
| `2026-10-08_before-psu/` | 换电源前的故障复现记录（界面导致整机硬重启） |

## 每个目录里通常有

| 文件 | 说明 |
|---|---|
| `dmesg_before.txt` / `dmesg_after.txt` | 测试前后的内核日志（对比有无异常） |
| `monitor_*.log` | 每 2 秒采样的 CPU/NPU/GPU 频率、温度、输入电压 ADC、负载 |
| `ui_*.log` | 板端 `eye_range` 的 stdout/stderr |
| `summary.md` | 本次测试的结论 |

## 怎么自己生成

```bash
# 带监控的压测（默认配置，300 秒）
APP_TIMEOUT=300 scripts/run_stress.sh borderless --log-level warn

# 真全屏（需要先关合成器才安全）
APP_TIMEOUT=180 scripts/run_stress.sh fullscreen --fullscreen

# 无界面基线
APP_TIMEOUT=300 scripts/run_stress.sh headless --headless --duration 300
```

`scripts/run_stress.sh` 会自动：上传并启动板端监控脚本 → 运行 `eye_range` →
把监控数据、应用日志、前后内核日志一并取回本目录。
