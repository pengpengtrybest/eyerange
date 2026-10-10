# 智能体：构建与部署

## 角色

只负责“本机交叉编译 + 传到板子”这条链路，保证任何时候都能一键复现。

## 所有权

可写：`CMakeLists.txt`、`cmake/**`、`scripts/board_env.sh`、`scripts/board.py`、
`scripts/bootstrap_sysroot.sh`、`scripts/remote_pack_sysroot.sh`、
`scripts/build_aarch64.sh`、`scripts/deploy_board.sh`、`scripts/run_board.sh`。

不要改：`src/**`（除非只是加/删文件后同步 CMake 源列表）、`config/**`、`doc/**`。

## 输入

- 需要新增的源文件路径与目标可执行文件；
- 板上环境变化（OpenCV 版本、Qt 版本、NPU 运行时版本）。

## 输出

- 可用的 `build/aarch64/{eye_range,eye_range_calib,eye_range_sysinfo}`；
- 板上 `/home/orangepi/eye_range/{bin,model,config}`；
- 必要的 `doc/02-环境探测与交叉编译.md` 更新。

## 必须遵守

1. `add_link_options(-Wl,--allow-shlib-undefined)` 不能删。
2. sysroot 变更必须走 `bootstrap_sysroot.sh`（含依赖闭包与 `/lib`→`usr/lib` 归一化）。
3. Qt 只在本机链接（`/opt/qt5-arm`），部署包不带 Qt。
4. 上传后要 `chmod +x`。

## 完成判据

- `build_aarch64.sh` 退出码 0，`file build/aarch64/eye_range` 显示 aarch64；
- 板上 `ldd bin/eye_range` 无 “not found”；
- `run_board.sh sysinfo` 能打印 OpenCV/Qt/RKNN/相机信息。
