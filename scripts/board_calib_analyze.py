#!/usr/bin/env python3
"""在开发板上分析标定图像对的质量（用板上 opencv-python 即可）。

用法: python3 board_calib_analyze.py [图像目录] [cols] [rows]

对每一对图输出：
  cov   棋盘在画面中的占比
  dcy   左右目棋盘中心的垂直差（水平双目应接近 0）
  dcx   左右目棋盘中心的水平差（应大于 0，等于视差）
  wr    左右目棋盘宽度之比（应接近 1）
  sharp 清晰度（Laplacian 方差，太小说明运动模糊/失焦）
"""

import glob
import sys

import cv2
import numpy as np


def detect(gray, pattern):
    ok, corners = cv2.findChessboardCorners(
        gray,
        pattern,
        cv2.CALIB_CB_ADAPTIVE_THRESH | cv2.CALIB_CB_NORMALIZE_IMAGE,
    )
    return (corners, True) if ok else (None, False)


def main():
    image_dir = sys.argv[1] if len(sys.argv) > 1 else "calib_images"
    cols = int(sys.argv[2]) if len(sys.argv) > 2 else 9
    rows = int(sys.argv[3]) if len(sys.argv) > 3 else 6
    pattern = (cols, rows)

    lefts = sorted(glob.glob(f"{image_dir}/left_*.png"))
    print(f"图像对: {len(lefts)}  棋盘内角点: {cols}x{rows}")
    print("idx    cov   dcy     dcx    wr    sharp  判定")

    bad = []
    for path in lefts:
        idx = path.rsplit("_", 1)[1].split(".")[0]
        left = cv2.imread(path, cv2.IMREAD_GRAYSCALE)
        right = cv2.imread(f"{image_dir}/right_{idx}.png", cv2.IMREAD_GRAYSCALE)
        if left is None or right is None:
            print(f"{idx}  读图失败")
            bad.append(idx)
            continue
        cl, ok_l = detect(left, pattern)
        cr, ok_r = detect(right, pattern)
        if not (ok_l and ok_r):
            print(f"{idx}  检测失败 左={ok_l} 右={ok_r}")
            bad.append(idx)
            continue
        bl = cv2.boundingRect(cl)
        br = cv2.boundingRect(cr)
        cov = bl[2] * bl[3] / float(left.shape[0] * left.shape[1])
        dcy = float(np.mean(cl[:, 0, 1]) - np.mean(cr[:, 0, 1]))
        dcx = float(np.mean(cl[:, 0, 0]) - np.mean(cr[:, 0, 0]))
        wr = bl[2] / float(br[2])
        sharp = cv2.Laplacian(left, cv2.CV_64F).var()
        verdict = "ok"
        if cov < 0.03:
            verdict = "棋盘太小"
        elif cov > 0.8:
            verdict = "棋盘太满"
        elif abs(dcy) > 0.15 * bl[3]:
            verdict = "左右不匹配"
        elif not 0 < dcx < 0.5 * bl[2]:
            verdict = "视差异常"
        elif not 0.8 < wr < 1.25:
            verdict = "尺度不一致"
        elif sharp < 20:
            verdict = "模糊"
        if verdict != "ok":
            bad.append(idx)
        print(f"{idx}  {cov:5.3f} {dcy:+6.1f} {dcx:+7.1f} {wr:5.2f} {sharp:7.1f}  {verdict}")

    print(f"\n可疑样本: {len(bad)}/{len(lefts)} -> {','.join(bad[:40])}")


if __name__ == "__main__":
    main()
