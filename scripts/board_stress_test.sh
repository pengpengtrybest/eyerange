#!/usr/bin/env bash
# 在开发板上做带实时监控的压测：每秒打印 CPU/GPU/NPU 频率、温度、负载，
# 同时启动 eye_range。如果板子因为过载/掉电重启，SSH 流会中断，
# 最后收到的几行就是崩溃前的现场数据。
#
# 用法: board_stress_test.sh <运行秒数> [eye_range 参数...]
#   board_stress_test.sh 20 --headless --duration 20
#   board_stress_test.sh 25 --windowed --npu-core 0 --stereo-threads 2
set -uo pipefail

DURATION="${1:-20}"
shift || true

cd "$(dirname "$0")"

sample() {
  local big litt gpu npu soc load v3
  big=$(($(cat /sys/devices/system/cpu/cpufreq/policy4/scaling_cur_freq 2>/dev/null || echo 0) / 1000))
  litt=$(($(cat /sys/devices/system/cpu/cpufreq/policy0/scaling_cur_freq 2>/dev/null || echo 0) / 1000))
  gpu=$(cat /sys/class/devfreq/fb000000.gpu/cur_freq 2>/dev/null || echo 0)
  npu=$(cat /sys/class/devfreq/fdab0000.npu/cur_freq 2>/dev/null || echo 0)
  soc=$(cat /sys/class/thermal/thermal_zone0/temp 2>/dev/null || echo 0)
  load=$(cut -d' ' -f1-3 /proc/loadavg)
  # ch3 疑似 5V 输入分压后的 ADC 读数（scale 0.439453125 mV/LSB）
  v3=$(cat /sys/bus/iio/devices/iio:device0/in_voltage3_raw 2>/dev/null || echo 0)
  printf '%s cpu_big=%sMHz cpu_lit=%sMHz gpu=%sHz npu=%sHz soc=%.1fC adc3=%smV load=%s\n' \
    "$(date +%H:%M:%S)" "$big" "$litt" "$gpu" "$npu" "$(echo "$soc/1000" | bc -l)" \
    "$(echo "$v3*0.439453125/1" | bc)" "$load"
}

echo "=== 监控开始（每 2 秒采样），${DURATION}s 后启动 eye_range，参数: $* ==="
(
  stop=$((DURATION + 60))
  for _ in $(seq 1 "$stop"); do
    sample | tee -a /home/orangepi/eye_range/monitor.log
    sync
    sleep 2
  done
) &
MONITOR=$!

sleep "$DURATION"
echo "=== 启动 eye_range: $* ==="
DISPLAY=:0 XAUTHORITY="/home/${USER}/.Xauthority" timeout "${APP_TIMEOUT:-600}" ./bin/eye_range "$@" \
  || echo "eye_range 退出码 $?"
echo "=== eye_range 已结束 ==="
sleep 4
kill "$MONITOR" 2>/dev/null || true
wait "$MONITOR" 2>/dev/null || true
echo "=== 测试结束，板子仍然在线 ==="
