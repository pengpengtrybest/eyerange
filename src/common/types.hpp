#pragma once

#include <opencv2/core.hpp>

#include <string>
#include <vector>

namespace eye {

// 单个检测目标（坐标位于左目原始图像上）
struct Detection {
  int cls_id = -1;
  std::string label;
  float score = 0.f;
  cv::Rect2f box;

  // 立体测距结果
  float disparity_px = 0.f;  // 全分辨率像素单位的视差中值
  float distance_m = 0.f;    // 距离（米），<=0 表示无效
  int valid_pixels = 0;      // 参与测距的有效视差点数
  int track_id = -1;
  std::string note;  // 无效原因等
};

// 一帧完整的处理结果
struct ResultFrame {
  cv::Mat left_bgr;         // 原始左目
  cv::Mat right_bgr;        // 原始右目
  cv::Mat annotated_bgr;    // 叠加框与距离后的左目图（给界面显示）
  cv::Mat disparity_vis;    // 视差伪彩色图（给界面显示）
  cv::Mat depth_vis;        // 深度伪彩色图
  std::vector<Detection> detections;
  double timestamp = 0.0;   // 采集时刻（秒）
  bool has_disparity = false;
};

// 运行统计
struct PipelineStats {
  double capture_fps = 0.0;
  double detect_fps = 0.0;
  double stereo_fps = 0.0;
  double display_fps = 0.0;
  double preprocess_ms = 0.0;
  double infer_ms = 0.0;
  double postprocess_ms = 0.0;
  double stereo_ms = 0.0;
  double pipeline_ms = 0.0;  // 从采集到出结果的端到端延迟
  double cpu_load = 0.0;
  int npu_core = -1;
  long long frames_captured = 0;
  long long frames_detected = 0;
  long long frames_stereo = 0;
};

inline const char* npu_core_name(int core) {
  switch (core) {
    case 0:
      return "NPU0";
    case 1:
      return "NPU1";
    case 2:
      return "NPU2";
    case -2:
      return "NPU0+1+2";
    case -1:
      return "AUTO";
    default:
      return "?";
  }
}

}  // namespace eye
