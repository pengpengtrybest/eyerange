#!/usr/bin/env bash
# 在开发板上运行
#   ./scripts/run_board.sh sysinfo                 环境自检
#   ./scripts/run_board.sh headless [秒数]         无界面跑一段时间并保存截图
#   ./scripts/run_board.sh ui                      启动 Qt 界面（板子接显示器）
#   ./scripts/run_board.sh calib-capture           采集标定图像
#   ./scripts/run_board.sh calib-solve             用已采集图像做标定
#   ./scripts/run_board.sh raw "<命令>"            在部署目录直接执行命令
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
# shellcheck source=board_env.sh
source "${SCRIPT_DIR}/board_env.defaults.sh"
if [[ -f "${SCRIPT_DIR}/board_env.sh" ]]; then
  # 本地私有凭据（不入库）
  # shellcheck source=board_env.sh
  source "${SCRIPT_DIR}/board_env.sh"
fi

mode="${1:-sysinfo}"
shift || true

run_remote() {
  python3 "${SCRIPT_DIR}/board.py" run "cd ${BOARD_ROOT} && $1"
}

case "${mode}" in
  sysinfo)
    run_remote "./bin/eye_range_sysinfo --model model/yolov8.rknn"
    ;;
  headless)
    seconds="${1:-15}"
    run_remote "mkdir -p out && ./bin/eye_range --headless --duration ${seconds} --out-dir out --save-interval 1 2>&1 | tail -60"
    ;;
  ui)
    # 板上跑着 Xorg(:0)，通过 SSH 启动界面会显示在 HDMI 显示器上。
    # 用 setsid 脱离会话：SSH 断开局面的进程也不会跟着死。
    run_remote "cd ${BOARD_ROOT} && DISPLAY=:0 XAUTHORITY=/home/${BOARD_USER}/.Xauthority \
setsid bash -c './bin/eye_range --log-level info > /tmp/eye_range_ui.log 2>&1' </dev/null >/dev/null 2>&1 & \
sleep 6; if pgrep -x eye_range >/dev/null; then echo '界面已启动（日志 /tmp/eye_range_ui.log，停止: pkill -x eye_range）'; \
grep -E '标定|立体测距|流水线' /tmp/eye_range_ui.log | tail -3; \
else echo '启动失败:'; tail -5 /tmp/eye_range_ui.log; fi"
    ;;
  calib-capture)
    run_remote "./bin/eye_range_calib capture --out-dir calib_images --count 20"
    ;;
  calib-solve)
    run_remote "./bin/eye_range_calib calibrate --dir calib_images --out config/calib.yaml"
    ;;
  raw)
    run_remote "$1"
    ;;
  *)
    echo "未知模式: ${mode}" >&2
    exit 2
    ;;
esac
