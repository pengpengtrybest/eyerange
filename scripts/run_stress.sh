#!/usr/bin/env bash
# 在开发板上做一次带监控的压测，并把所有日志取回 logs/<时间戳>_<主题>/
#
# 用法: scripts/run_stress.sh <主题> [eye_range 参数...]
#   APP_TIMEOUT=300 scripts/run_stress.sh borderless --log-level warn
#   APP_TIMEOUT=180 scripts/run_stress.sh fullscreen --fullscreen
#   APP_TIMEOUT=300 scripts/run_stress.sh headless  --headless --duration 300
#
# 环境变量：
#   APP_TIMEOUT  界面上限时长（秒），默认 300
#   LOG_DIR      输出目录，默认 logs/$(date +%Y-%m-%d_%H%M%S)_<主题>
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
# shellcheck source=board_env.sh
source "${SCRIPT_DIR}/board_env.defaults.sh"
if [[ -f "${SCRIPT_DIR}/board_env.sh" ]]; then
  # 本地私有凭据（不入库）
  # shellcheck source=board_env.sh
  source "${SCRIPT_DIR}/board_env.sh"
fi

TOPIC="${1:?用法: run_stress.sh <主题> [eye_range 参数...]}"
shift || true

APP_TIMEOUT="${APP_TIMEOUT:-300}"
LOG_DIR="${LOG_DIR:-${PROJECT_ROOT}/logs/$(date +%Y-%m-%d_%H%M%S)_${TOPIC}}"
mkdir -p "${LOG_DIR}"

BOARD_STRESS="board_stress_test.sh"

echo "==> 日志目录: ${LOG_DIR}"
echo "==> 上传板端压测脚本"
python3 "${SCRIPT_DIR}/board.py" put "${SCRIPT_DIR}/${BOARD_STRESS}" "${BOARD_ROOT}/${BOARD_STRESS}"
python3 "${SCRIPT_DIR}/board.py" run "chmod +x ${BOARD_ROOT}/${BOARD_STRESS}"

echo "==> 采集测试前状态"
python3 "${SCRIPT_DIR}/board.py" run "uptime; dmesg | tail -200" > "${LOG_DIR}/dmesg_before.txt" 2>&1
python3 "${SCRIPT_DIR}/board.py" run "rm -f ${BOARD_ROOT}/monitor.log; touch ${BOARD_ROOT}/monitor.log" 

echo "==> 运行压测: ${APP_TIMEOUT}s, 参数: $*"
python3 "${SCRIPT_DIR}/board.py" run \
  "cd ${BOARD_ROOT} && APP_TIMEOUT=${APP_TIMEOUT} ./${BOARD_STRESS} 5 $*" 2>&1 | tee "${LOG_DIR}/run_stdout.txt"

echo "==> 取回日志"
python3 "${SCRIPT_DIR}/board.py" get "${BOARD_ROOT}/monitor.log" "${LOG_DIR}/monitor.log" || true
python3 "${SCRIPT_DIR}/board.py" run "uptime; last -x reboot -F | head -4; dmesg | tail -200" \
  > "${LOG_DIR}/dmesg_after.txt" 2>&1

if grep -q "板子仍然在线" "${LOG_DIR}/run_stdout.txt"; then
  echo "==> 结果: 未重启（板子在线）" | tee -a "${LOG_DIR}/summary.md"
else
  echo "==> 结果: 测试中断（疑似重启）" | tee -a "${LOG_DIR}/summary.md"
fi
echo "==> 全部日志: ${LOG_DIR}"
