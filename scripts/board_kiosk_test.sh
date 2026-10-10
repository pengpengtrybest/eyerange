#!/usr/bin/env bash
# 用 Qt 的 linuxfb 平台插件直接写 framebuffer 跑界面（完全绕开 X / 合成器），
# 同时记录内存、负载、温度，用于判断“界面把板子搞重启”是否来自 X 侧。
#
# 用法（在板子上执行）: ./board_kiosk_test.sh [秒数] [额外参数...]
set -u

cd "$(dirname "$0")"
DURATION="${1:-90}"
shift || true

PASS="${BOARD_PASS:?请先设置 BOARD_PASS（可写在 scripts/board_env.sh 里）}"
sudo_run() { echo "$PASS" | sudo -S bash -c "$1"; }

echo "=== 停止显示管理器 lightdm（X 让出 framebuffer）==="
sudo_run "systemctl stop lightdm" 2>/dev/null
sleep 3

echo "=== 监控 + 运行 QT_QPA_PLATFORM=linuxfb，${DURATION}s ==="
(
  for _ in $(seq 1 200); do
    app=$(ps -o rss= -C eye_range 2>/dev/null | tr -d ' ' | head -1)
    app=${app:-0}
    avail=$(awk '/MemAvailable/{print int($2/1024)}' /proc/meminfo)
    temp=$(cat /sys/class/thermal/thermal_zone0/temp 2>/dev/null || echo 0)
    load=$(cut -d' ' -f1 /proc/loadavg)
    printf '%s app=%sMB avail=%sMB soc=%.1fC load=%s\n' "$(date +%H:%M:%S)" "$((app / 1024))" "$avail" \
      "$(echo "$temp/1000" | bc -l)" "$load"
    sleep 3
  done
) > kiosk.log 2>&1 &
MONITOR=$!

QT_QPA_PLATFORM=linuxfb timeout "$DURATION" ./bin/eye_range --log-level info "$@"
echo "eye_range 退出码 $?"

kill "$MONITOR" 2>/dev/null
wait "$MONITOR" 2>/dev/null

echo "=== 采样（前 2 行 / 后 8 行）==="
head -2 kiosk.log
echo "..."
tail -8 kiosk.log

echo "=== 恢复显示管理器 ==="
sudo_run "systemctl start lightdm" 2>/dev/null
echo "板子仍在线"
