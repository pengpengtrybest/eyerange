#!/usr/bin/env bash
# 本机交叉编译（RK3588 / aarch64），产物在 build/aarch64
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
# shellcheck source=board_env.sh
source "${SCRIPT_DIR}/board_env.defaults.sh"
if [[ -f "${SCRIPT_DIR}/board_env.sh" ]]; then
  # 本地私有凭据（不入库）
  # shellcheck source=board_env.sh
  source "${SCRIPT_DIR}/board_env.sh"
fi

cd "${PROJECT_ROOT}"

if [[ ! -d build/sysroot/usr/include/opencv4 ]]; then
  echo "sysroot 未就绪，先执行 scripts/bootstrap_sysroot.sh" >&2
  exit 1
fi

: "${BUILD_DIR:=build/aarch64}"

if [[ ! -f "${BUILD_DIR}/CMakeCache.txt" ]]; then
  cmake -S . -B "${BUILD_DIR}" -DCMAKE_TOOLCHAIN_FILE=cmake/toolchains/aarch64-linux-gnu.cmake
fi

cmake --build "${BUILD_DIR}" -j"$(nproc)"

echo
echo "产物:"
for bin in eye_range eye_range_calib eye_range_sysinfo; do
  if [[ -f "${BUILD_DIR}/${bin}" ]]; then
    printf "  %-20s %s\n" "${bin}" "$(file -b "${BUILD_DIR}/${bin}" | cut -c1-60)"
  fi
done
