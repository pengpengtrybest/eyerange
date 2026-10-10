#pragma once

#include <opencv2/core.hpp>

#include <string>

namespace eye {

// 采集参数
struct CaptureConfig {
  std::string device = "/dev/video0";
  int width = 2560;   // 双目的左右拼接总宽
  int height = 960;   // 单目各自的高度
  int fps = 30;
  int buffer_count = 4;
  bool swap_eyes = false;  // 若左右相反，打开此开关
};

// NPU 推理参数
struct DetectConfig {
  std::string model = "model/yolov8.rknn";
  std::string labels = "model/coco_80_labels_list.txt";
  float conf = 0.35f;
  float nms = 0.45f;
  // NPU 核：-1=AUTO, 0/1/2=单核, -2=三核
  int npu_core = -1;
  int max_detections = 64;
};

// 立体匹配/测距参数
struct StereoConfig {
  std::string calib = "config/calib.yaml";
  int num_disparities = 128;   // 必须是 16 的倍数
  int min_disparity = 0;
  int block_size = 5;
  int uniqueness_ratio = 10;
  int disp12_max_diff = 1;
  int speckle_window = 100;
  int speckle_range = 2;
  int pre_filter_cap = 63;
  bool use_wls = false;

  // 只在目标框附近计算视差（大幅提速）
  bool roi_only = true;
  float roi_scale = 1.0f;    // ROI 内计算所用的缩放（1.0=原分辨率）
  int roi_margin = 16;       // ROI 外扩像素（上下）
  int max_roi_side = 640;    // ROI 最长边上限，超出则内部降采样
  int band_max_height = 128; // 目标带的最大高度（行数越多越慢）

  // 全图视差（用于显示）缩放
  float display_scale = 0.25f;
  int display_num_disparities = 64;  // 显示图用的视差范围（小一点更快）

  int threads = 4;          // ROI 并行线程数（RK3588 8 核；功耗敏感时可降到 2）
  int opencv_threads = 1;   // OpenCV 内部线程数（避免与上面的并行抢核）
  float min_confidence = 0.35f;

  // 未标定时的兜底内参（距离为估算值）
  double fallback_baseline_mm = 60.0;
  double fallback_fx = 0.0;  // <=0 时按 0.85*单目宽度 估算
};

// 界面参数
struct UiConfig {
  // 铺满屏幕有两种做法：
  //   borderless=1  无边框窗口铺满屏幕（推荐：不触发合成器的全屏 unredirect 路径）
  //   fullscreen=1  真正的 EWMH 全屏（部分板子开了 xfwm4 合成器时会把系统拖死）
  bool fullscreen = true;   // 默认真全屏（2026-10-09 换电源后实测稳定）
  bool borderless = false;  // 备用：无边框铺满（不覆盖 xfce 面板）
  bool show_depth = true;
  bool show_disparity = true;
  int fps = 20;             // 界面刷新率（渲染是纯软件缩放，降下来能明显降低峰值功耗）
  bool smooth_scale = false;  // 视频缩放是否用平滑插值（默认关，省 CPU）
  int window_width = 1600;
  int window_height = 900;
};

struct AppConfig {
  CaptureConfig capture;
  DetectConfig detect;
  StereoConfig stereo;
  UiConfig ui;

  // 从 YAML 读取（缺失字段保留默认值）
  static AppConfig load(const std::string& path);
  // 写出默认配置，便于人工修改
  void save(const std::string& path) const;
};

// 标定结果（双目，左右目各自的内参 + 立体外参 + 校正映射参数）
struct CalibData {
  bool valid = false;
  cv::Size image_size{0, 0};
  std::string source;

  cv::Mat K_left, D_left;    // 3x3, 1x5
  cv::Mat K_right, D_right;
  cv::Mat R, T;              // 3x3, 3x1
  cv::Mat R1, R2, P1, P2, Q; // 校正结果
  double baseline_mm = 0.0;  // |T.x|
  double fx = 0.0;           // P1(0,0)
  double cx = 0.0, cy = 0.0; // P1 主点
  double doffs = 0.0;        // 主点水平差 cx1-cx2

  static CalibData load(const std::string& path);
};

}  // namespace eye
