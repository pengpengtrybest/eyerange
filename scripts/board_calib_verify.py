#!/usr/bin/env python3
"""验证双目标定质量（在开发板上跑，用板上 opencv-python）。

用法: python3 board_calib_verify.py <calib.yaml> [图像目录] [cols] [rows]

检查项：
  * 校正后左右目角点的极线对齐误差（行差）：好标定应 < 0.5px
  * 角点视差分布，以及由 Z = fx*B/d 推算的标定板距离是否落在合理范围
"""

import glob
import sys

import cv2
import numpy as np


def main():
    calib_path = sys.argv[1] if len(sys.argv) > 1 else "config/calib.yaml"
    image_dir = sys.argv[2] if len(sys.argv) > 2 else "calib_images"
    cols = int(sys.argv[3]) if len(sys.argv) > 3 else 9
    rows = int(sys.argv[4]) if len(sys.argv) > 4 else 6
    pattern = (cols, rows)

    fs = cv2.FileStorage(calib_path, cv2.FILE_STORAGE_READ)
    K1, D1 = fs.getNode("M1").mat(), fs.getNode("D1").mat()
    K2, D2 = fs.getNode("M2").mat(), fs.getNode("D2").mat()
    R1, R2 = fs.getNode("R1").mat(), fs.getNode("R2").mat()
    P1, P2 = fs.getNode("P1").mat(), fs.getNode("P2").mat()
    T = fs.getNode("T").mat()
    size = (int(fs.getNode("image_width").real()), int(fs.getNode("image_height").real()))
    fs.release()
    fx, baseline = P1[0, 0], abs(T[0, 0])
    k = fx * baseline / 1000.0  # Z[m] = k / d[px]
    print(f"fx={fx:.1f}px baseline={baseline:.2f}mm  距离公式 Z[m] = {k:.2f} / 视差")

    m1 = cv2.initUndistortRectifyMap(K1, D1, R1, P1, size, cv2.CV_32FC1)
    m2 = cv2.initUndistortRectifyMap(K2, D2, R2, P2, size, cv2.CV_32FC1)

    dys, disps, dists = [], [], []
    for path in sorted(glob.glob(f"{image_dir}/left_*.png"))[:40]:
        idx = path.rsplit("_", 1)[1].split(".")[0]
        left = cv2.imread(path, cv2.IMREAD_GRAYSCALE)
        right = cv2.imread(f"{image_dir}/right_{idx}.png", cv2.IMREAD_GRAYSCALE)
        if left is None or right is None:
            continue
        lr = cv2.remap(left, m1[0], m1[1], cv2.INTER_LINEAR)
        rr = cv2.remap(right, m2[0], m2[1], cv2.INTER_LINEAR)
        ok1, c1 = cv2.findChessboardCornersSB(lr, pattern, cv2.CALIB_CB_EXHAUSTIVE)
        ok2, c2 = cv2.findChessboardCornersSB(rr, pattern, cv2.CALIB_CB_EXHAUSTIVE)
        if not (ok1 and ok2):
            continue
        dys.append(np.abs(c1[:, 0, 1] - c2[:, 0, 1]).mean())
        d = float(np.median(c1[:, 0, 0] - c2[:, 0, 0]))
        disps.append(d)
        if d > 1:
            dists.append(k / d)

    if not dys:
        print("没有可用于验证的图像对")
        return 1

    print(f"样本数: {len(dys)}")
    print(f"极线对齐误差 |dy|: 平均 {np.mean(dys):.3f}px, 最大 {np.max(dys):.3f}px  (好标定应 < 0.5px)")
    print(f"角点视差: 中位 {np.median(disps):.1f}px, 范围 {np.min(disps):.1f}~{np.max(disps):.1f}px")
    print(f"推算标定板距离: 中位 {np.median(dists):.2f}m, 范围 {np.min(dists):.2f}~{np.max(dists):.2f}m")
    print("（若这个范围与当时举着棋盘的实际距离明显不符，说明棋盘格边长填写有误，")
    print("  距离与格长成正比：真实格长 = 当前格长 × 实测距离 / 推算距离）")
    return 0


if __name__ == "__main__":
    sys.exit(main())
