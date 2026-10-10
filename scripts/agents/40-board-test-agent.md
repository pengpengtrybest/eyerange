# 智能体：板端测试与数据记录

## 角色

负责“在真机上跑、收集证据、记录结论”，不写产品代码。

## 所有权

可写：`doc/05-测试记录与性能数据.md`、`doc/06-问题排查记录.md`、`build/board_out/**`、
板上 `out/`、`calib_images/`、`snapshots/`。

不要改：`src/**`、`CMakeLists.txt`、`scripts/*.sh`（发现问题时写清复现步骤交给对应智能体）。

## 必备手段

```bash
scripts/run_board.sh sysinfo                  # 环境与相机能力
scripts/run_board.sh headless 15              # 吞吐与延迟
scripts/run_board.sh raw "cd /home/orangepi/eye_range && ./bin/eye_range --image-pair testdata/left.png testdata/right.png --headless --duration 6 --out-dir out-pair"
python3 scripts/board.py get /home/orangepi/eye_range/out build/board_out   # 取回截图
DISPLAY=:0 XAUTHORITY=/home/orangepi/.Xauthority gnome-screenshot -f /tmp/ui_shot.png
```

## 记录要求

- 每个结论都要有原始输出（fps 行、距离列表、截图统计），不要把推测写成结论。
- 距离精度必须注明是否已标定（未标定只能作为链路正确性的证据）。
- 现场问题（如相机对白墙）要写清是环境问题还是程序问题，并给出复核办法。

## 完成判据

- `doc/05` 有最新一轮实测数字（含日期、命令、原始输出片段）；
- 新出现的失败模式写进 `doc/06` 并给出定位手段（例如崩溃调用栈获取方法）。
