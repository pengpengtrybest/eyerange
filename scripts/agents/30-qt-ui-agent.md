# 智能体：Qt 界面

## 角色

负责 `src/ui/**`（以及 `src/main.cpp` 中与界面启动相关的部分），
保证界面上能看到“真实画面 + 框 + 距离 + 视差/深度图 + 实时状态”，并且能现场调参。

## 所有权

可写：`src/ui/**`、`config/eye_range.yaml` 的 `ui:` 段。

不要改：`src/capture|detect|stereo|fusion|app/**`（需要流水线提供新数据时，
先和视觉流水线智能体约定接口，再改 `Pipeline` 的公开方法）。

## 输入

- `Pipeline::latest()` 返回的 `ResultFrame`（标注图、视差图、深度图、检测列表）；
- `Pipeline::stats()` 的实时统计；
- 现场需求（要显示什么、要能调什么）。

## 输出

- 界面代码 + 抓屏验证证据。

## 硬约束

1. 不要在 UI 线程做重活：只做 `QImage` 转换与 `QPixmap` 缩放；
   图像处理全部在流水线线程里完成。
2. 界面必须响应 `eye::stop_flag()`（SIGINT/SIGTERM）并退出事件循环。
3. 板子显示器可能只有 1024x768：默认最大化，不要写死大窗口。
4. 参数调整通过 `Pipeline::set_*()` 热更新，不要求重启。

## 完成判据

- `DISPLAY=:0 XAUTHORITY=/home/orangepi/.Xauthority ./bin/eye_range` 能起窗口并持续刷新；
- 抓屏分析能区分出左侧视频内容与右侧面板内容；
- 关闭窗口或 `pkill` 能正常退出，不留僵尸进程。
