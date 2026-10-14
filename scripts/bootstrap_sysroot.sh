#!/usr/bin/env bash
# 搭建交叉编译 sysroot：build/sysroot
#
#   1) 用 Linaro aarch64 工具链自带的 libc sysroot 作为底（libc/内核头文件、crt 等）
#   2) 从开发板同步 OpenCV 4.5.4 的头文件 / 共享库 / CMake 配置
#
# 这样本机编译所依赖的 OpenCV ABI 与板上运行时完全一致，
# 板上不用再额外传 OpenCV。
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
# 先读本地私有配置，再用默认值兜底：
# 优先级 = 环境变量 > scripts/board_env.sh > scripts/board_env.defaults.sh
if [[ -f "${SCRIPT_DIR}/board_env.sh" ]]; then
  # 本地私有凭据（不入库）
  # shellcheck source=board_env.sh
  source "${SCRIPT_DIR}/board_env.sh"
fi
# shellcheck source=board_env.sh
source "${SCRIPT_DIR}/board_env.defaults.sh"

TOOLCHAIN_SYSROOT="${AARCH64_TOOLCHAIN_ROOT:-/opt/toolchains/gcc-linaro-7.5.0-2019.12-x86_64_aarch64-linux-gnu}/aarch64-linux-gnu/libc"
SYSROOT="${PROJECT_ROOT}/build/sysroot"
TARBALL="${PROJECT_ROOT}/build/eye_range_sysroot.tgz"
REMOTE_TARBALL="/tmp/eye_range_sysroot.tgz"

if [[ ! -d "${TOOLCHAIN_SYSROOT}" ]]; then
  echo "找不到工具链 sysroot: ${TOOLCHAIN_SYSROOT}" >&2
  exit 1
fi

echo "==> 1/3 用工具链 sysroot 打底: ${SYSROOT}"
mkdir -p "${SYSROOT}"
cp -a -n "${TOOLCHAIN_SYSROOT}/." "${SYSROOT}/"

echo "==> 2/3 从开发板打包 OpenCV + 依赖闭包 (${BOARD_HOST})"
python3 "${SCRIPT_DIR}/board.py" put "${SCRIPT_DIR}/remote_pack_sysroot.sh" "/tmp/remote_pack_sysroot.sh"
python3 "${SCRIPT_DIR}/board.py" run "bash /tmp/remote_pack_sysroot.sh ${REMOTE_TARBALL}"

echo "==> 3/3 下载并解包到 sysroot"
python3 "${SCRIPT_DIR}/board.py" get "${REMOTE_TARBALL}" "${TARBALL}"
tar xzf "${TARBALL}" -C "${SYSROOT}" --strip-components=1

echo
echo "sysroot 就绪:"
echo "  OpenCV 头文件 : $(ls -d "${SYSROOT}"/usr/include/opencv4 2>/dev/null || echo 缺失)"
echo "  OpenCV 库     : $(ls "${SYSROOT}"/usr/lib/aarch64-linux-gnu/libopencv_core.so* 2>/dev/null | head -1 || echo 缺失)"
echo "  CMake 配置    : $(ls -d "${SYSROOT}"/usr/lib/aarch64-linux-gnu/cmake/opencv4 2>/dev/null || echo 缺失)"
