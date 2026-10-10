#!/usr/bin/env bash
# 在开发板上执行：打包本机交叉编译所需的 OpenCV 头文件/库/依赖闭包
# 用法: remote_pack_sysroot.sh <输出 tarball 路径>
#
# 板上的 /lib 是 /usr/lib 的符号链接，ldd 输出的路径会混用两者；
# 这里统一整理到一个暂存树里（含符号链接与真实文件），保证本机 sysroot
# 只依赖 usr/lib/aarch64-linux-gnu 这一个检索路径。
set -euo pipefail

OUT="${1:?用法: $0 <tarball 路径>}"
TRIPLET_DIR="/usr/lib/aarch64-linux-gnu"
STAGE_NAME="eye_range_sysroot_stage"
STAGE="/tmp/${STAGE_NAME}"
LIB_DST="${STAGE}${TRIPLET_DIR}"

rm -rf "${STAGE}"
mkdir -p "${STAGE}/usr/include" "${LIB_DST}/cmake" "${LIB_DST}/pkgconfig"

# 1) OpenCV 本体
cp -a /usr/include/opencv4 "${STAGE}/usr/include/"
cp -a "${TRIPLET_DIR}/cmake/opencv4" "${LIB_DST}/cmake/"
cp -a "${TRIPLET_DIR}/pkgconfig/opencv4.pc" "${LIB_DST}/pkgconfig/"

opencv_paths=$(ls ${TRIPLET_DIR}/libopencv_*.so*)

# 2) 依赖闭包（libjpeg/libpng/libtiff/openjpeg/protobuf 等）
#    排除 libc/libstdc++/libgcc/动态加载器：这些沿用交叉工具链自带版本，
#    避免链接期绑定比目标机更新的 glibc 符号。
deps=$(ldd ${TRIPLET_DIR}/libopencv_*.so.4.5.4d 2>/dev/null \
  | awk '/=> \// {print $3}' \
  | sort -u \
  | grep -v -E 'libc\.so|libm\.so|libpthread|libdl\.so|librt\.so|ld-linux|libgcc_s|libstdc\+\+' || true)

echo "OpenCV 库文件: $(echo "${opencv_paths}" | wc -l)"
echo "依赖闭包文件: $(echo "${deps}" | wc -l)"

for f in ${opencv_paths} ${deps}; do
  cp -a "${f}" "${LIB_DST}/" 2>/dev/null || true
  real=$(readlink -f "${f}")
  if [ "${real}" != "${f}" ]; then
    cp -a "${real}" "${LIB_DST}/" 2>/dev/null || true
  fi
done

rm -f "${OUT}"
tar czf "${OUT}" -C /tmp "${STAGE_NAME}"
ls -l "${OUT}"
