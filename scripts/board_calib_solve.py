#!/usr/bin/env python3
"""在开发板上做稳健双目标定：逐视角误差分析 + 剔除离群视角 + 重解。

用法:
  python3 board_calib_solve.py [图像目录] [cols] [rows] [square_mm] [输出yaml]

输出：
  * 每个视角的左目重投影误差（用于找出角点顺序/检测异常的样本）
  * 剔除离群样本后的单目/双目 RMS、内参、基线
  * 可选写出 calib.yaml（字段与 C++ 侧一致）
"""

import glob
import sys

import cv2
import numpy as np


def detect(gray, pattern, use_sb=True):
    if use_sb:
        ok, corners = cv2.findChessboardCornersSB(gray, pattern, cv2.CALIB_CB_EXHAUSTIVE)
    else:
        ok, corners = cv2.findChessboardCorners(
            gray, pattern, cv2.CALIB_CB_ADAPTIVE_THRESH | cv2.CALIB_CB_NORMALIZE_IMAGE
        )
        if ok:
            corners = cv2.cornerSubPix(
                gray, corners, (11, 11), (-1, -1),
                (cv2.TERM_CRITERIA_EPS + cv2.TERM_CRITERIA_COUNT, 30, 0.01),
            )
    return corners if ok else None


def main():
    image_dir = sys.argv[1] if len(sys.argv) > 1 else "calib_images"
    cols = int(sys.argv[2]) if len(sys.argv) > 2 else 9
    rows = int(sys.argv[3]) if len(sys.argv) > 3 else 6
    square = float(sys.argv[4]) if len(sys.argv) > 4 else 25.0
    out_yaml = sys.argv[5] if len(sys.argv) > 5 else ""
    pattern = (cols, rows)

    objp = np.zeros((rows * cols, 3), np.float32)
    objp[:, :2] = np.mgrid[0:cols, 0:rows].T.reshape(-1, 2) * square

    objpoints, img_l, img_r, names = [], [], [], []
    for path in sorted(glob.glob(f"{image_dir}/left_*.png")):
        idx = path.rsplit("_", 1)[1].split(".")[0]
        left = cv2.imread(path, cv2.IMREAD_GRAYSCALE)
        right = cv2.imread(f"{image_dir}/right_{idx}.png", cv2.IMREAD_GRAYSCALE)
        if left is None or right is None:
            continue
        cl = detect(left, pattern)
        cr = detect(right, pattern)
        if cl is None or cr is None:
            print(f"  视角 {idx}: 检测失败，跳过")
            continue
        objpoints.append(objp)
        img_l.append(cl)
        img_r.append(cr)
        names.append(idx)

    print(f"可解算视角: {len(objpoints)} 个")
    if len(objpoints) < 8:
        print("样本太少，无法标定")
        return 1

    size = (cv2.imread(f"{image_dir}/left_{names[0]}.png", cv2.IMREAD_GRAYSCALE).shape[::-1])
    print(f"单目尺寸: {size[0]}x{size[1]}")

    keep = list(range(len(objpoints)))
    for iteration in range(4):
        objs = [objpoints[i] for i in keep]
        ls = [img_l[i] for i in keep]
        rs = [img_r[i] for i in keep]
        rms_l, K1, D1, rvecs, tvecs = cv2.calibrateCamera(objs, ls, size, None, None)
        errors = []
        for i, (obj, img, rv, tv) in enumerate(zip(objs, ls, rvecs, tvecs)):
            proj, _ = cv2.projectPoints(obj, rv, tv, K1, D1)
            err = float(np.sqrt(np.mean(np.sum((proj - img) ** 2, axis=2))))
            errors.append((err, keep[i]))
        errors.sort(reverse=True)
        median = float(np.median([e for e, _ in errors]))
        worst = errors[0]
        print(f"[第 {iteration + 1} 轮] 左目 RMS {rms_l:.3f}px 中位误差 {median:.3f}px 最差 {worst[0]:.1f}px (视角 {names[worst[1]]})")
        # 剔除明显离群（阈值取 max(1.0px, 5×中位)）
        limit = max(1.0, 5.0 * median)
        drop = [idx for err, idx in errors if err > limit]
        if not drop or len(drop) > len(keep) * 0.4:
            break
        print(f"          剔除 {len(drop)} 个离群视角: {[names[i] for i in drop[:12]]}")
        keep = [i for i in keep if i not in set(drop)]

    objs = [objpoints[i] for i in keep]
    ls = [img_l[i] for i in keep]
    rs = [img_r[i] for i in keep]
    rms_l, K1, D1, _, _ = cv2.calibrateCamera(objs, ls, size, None, None)
    rms_r, K2, D2, _, _ = cv2.calibrateCamera(objs, rs, size, None, None)
    rms, K1, D1, K2, D2, R, T, E, F = cv2.stereoCalibrate(
        objs, ls, rs, K1, D1, K2, D2, size,
        flags=cv2.CALIB_FIX_INTRINSIC,
        criteria=(cv2.TERM_CRITERIA_EPS + cv2.TERM_CRITERIA_COUNT, 100, 1e-6),
    )
    R1, R2, P1, P2, Q, _, _ = cv2.stereoRectify(
        K1, D1, K2, D2, size, R, T, flags=cv2.CALIB_ZERO_DISPARITY, alpha=0
    )

    print(f"\n最终使用 {len(keep)} 个视角")
    print(f"单目 RMS: 左 {rms_l:.3f}px 右 {rms_r:.3f}px")
    print(f"双目 RMS: {rms:.3f}px")
    print(f"基线: {abs(T[0, 0]):.2f} mm  (T = {np.round(T.ravel(), 2)})")
    print(f"左目 fx/fy/cx/cy: {K1[0,0]:.1f} {K1[1,1]:.1f} {K1[0,2]:.1f} {K1[1,2]:.1f}")
    print(f"右目 fx/fy/cx/cy: {K2[0,0]:.1f} {K2[1,1]:.1f} {K2[0,2]:.1f} {K2[1,2]:.1f}")
    print(f"校正后 fx {P1[0,0]:.1f} px, 主点差 {P1[0,2] - P2[0,2]:.2f}")
    if square > 0:
        print(f"1 像素视差对应距离误差: 在 1m 处约 {100.0 / (P1[0,0] * abs(T[0, 0]) / 1000.0) / 100.0:.1f} cm")

    if out_yaml:
        fs = cv2.FileStorage(out_yaml, cv2.FILE_STORAGE_WRITE)
        fs.write("image_width", size[0])
        fs.write("image_height", size[1])
        for name, mat in [("M1", K1), ("D1", D1), ("M2", K2), ("D2", D2), ("R", R), ("T", T),
                          ("R1", R1), ("R2", R2), ("P1", P1), ("P2", P2), ("Q", Q)]:
            fs.write(name, mat)
        fs.write("rms", float(rms))
        fs.write("square_mm", float(square))
        fs.write("pattern_cols", cols)
        fs.write("pattern_rows", rows)
        fs.write("pairs", len(keep))
        fs.release()
        print(f"已写出 {out_yaml}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
