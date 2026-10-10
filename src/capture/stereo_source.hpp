#pragma once

#include <opencv2/core.hpp>

#include <string>

#include "capture/v4l2_camera.hpp"
#include "common/config.hpp"

namespace eye {

struct StereoPair {
  cv::Mat left;
  cv::Mat right;
  double timestamp = 0.0;
};

// 双目图像来源：V4L2 实时采集（左右拼接帧切分）或图片对（离线自测）
class StereoSource {
 public:
  bool open(const CaptureConfig& cfg, std::string* err);
  void close();
  bool is_open() const { return file_mode_ || camera_.is_open(); }
  bool file_mode() const { return file_mode_; }

  // 抓一对左右图；timeout_ms < 0 表示阻塞
  bool grab(StereoPair& out, int timeout_ms);

  // 离线模式：直接处理给定的左右图文件
  bool open_image_pair(const std::string& left_path, const std::string& right_path, std::string* err);

  int eye_width() const { return eye_width_; }
  int eye_height() const { return eye_height_; }
  const std::string& description() const { return description_; }

 private:
  bool split(const cv::Mat& frame, StereoPair& out) const;

  V4l2Camera camera_;
  bool file_mode_ = false;
  bool swap_eyes_ = false;
  cv::Mat file_left_;
  cv::Mat file_right_;
  int eye_width_ = 0;
  int eye_height_ = 0;
  std::string description_;
};

}  // namespace eye
