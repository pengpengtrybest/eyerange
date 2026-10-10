#!/usr/bin/env bash
# 板卡连接参数的**非敏感默认值**（随仓库提交）。
#
# 真实凭据请放在 scripts/board_env.sh（该文件在 .gitignore 中，不会入库）：
#   cat > scripts/board_env.sh <<'EOF'
#   export BOARD_HOST=192.168.228.113
#   export BOARD_USER=orangepi
#   export BOARD_PASS=你的密码
#   export BOARD_ROOT=/home/orangepi/eye_range
#   EOF
# 也可以直接用环境变量覆盖，例如 BOARD_PASS=xxx scripts/run_board.sh sysinfo

export BOARD_HOST="${BOARD_HOST:-192.168.228.113}"
export BOARD_USER="${BOARD_USER:-orangepi}"
export BOARD_PORT="${BOARD_PORT:-22}"
export BOARD_ROOT="${BOARD_ROOT:-/home/orangepi/eye_range}"
# 密码不给默认值：必须由环境变量或 board_env.sh 提供
export BOARD_PASS="${BOARD_PASS:-}"

# 本仓库根目录
export PROJECT_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
