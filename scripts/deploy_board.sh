#!/usr/bin/env bash
# 打包运行所需文件并上传到开发板
#   build/bundle/{bin,model,config} -> orangepi@192.168.228.113:/home/orangepi/eye_range
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

: "${BUILD_DIR:=build/aarch64}"
BUNDLE="build/bundle"

for bin in eye_range eye_range_calib eye_range_sysinfo; do
  if [[ ! -x "${BUILD_DIR}/${bin}" ]]; then
    echo "缺少 ${BUILD_DIR}/${bin}，先运行 scripts/build_aarch64.sh" >&2
    exit 1
  fi
done

rm -rf "${BUNDLE}"
mkdir -p "${BUNDLE}/bin" "${BUNDLE}/model" "${BUNDLE}/config"
cp "${BUILD_DIR}"/eye_range "${BUILD_DIR}"/eye_range_calib "${BUILD_DIR}"/eye_range_sysinfo "${BUNDLE}/bin/"
cp model/*.rknn model/*.txt "${BUNDLE}/model/"
cp config/eye_range.yaml "${BUNDLE}/config/"
if [[ -f config/calib.yaml ]]; then
  cp config/calib.yaml "${BUNDLE}/config/"
fi

echo "==> 上传到 ${BOARD_USER}@${BOARD_HOST}:${BOARD_ROOT}"
python3 "${SCRIPT_DIR}/board.py" run "mkdir -p ${BOARD_ROOT}"
python3 "${SCRIPT_DIR}/board.py" put "${BUNDLE}/bin" "${BOARD_ROOT}/bin"
python3 "${SCRIPT_DIR}/board.py" put "${BUNDLE}/model" "${BOARD_ROOT}/model"
python3 "${SCRIPT_DIR}/board.py" put "${BUNDLE}/config" "${BOARD_ROOT}/config"

python3 "${SCRIPT_DIR}/board.py" run "chmod +x ${BOARD_ROOT}/bin/*"
python3 "${SCRIPT_DIR}/board.py" run "ls -la ${BOARD_ROOT}/bin ${BOARD_ROOT}/model ${BOARD_ROOT}/config"
