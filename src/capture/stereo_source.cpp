#include "capture/stereo_source.hpp"

#include <opencv2/imgcodecs.hpp>

#include <chrono>

#include "common/log.hpp"

namespace eye {
namespace {

double now_seconds() {
  return std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count();
}

}  // namespace

bool StereoSource::open(const CaptureConfig& cfg, std::string* err) {
  close();
  swap_eyes_ = cfg.swap_eyes;
  if (!camera_.open(cfg.device, cfg.width, cfg.height, cfg.fps, cfg.buffer_count, err)) {
    return false;
  }
  eye_width_ = camera_.width() / 2;
  eye_height_ = camera_.height();
  description_ = camera_.device() + " " + std::to_string(camera_.width()) + "x" + std::to_string(camera_.height()) +
                 " -> 每目 " + std::to_string(eye_width_) + "x" + std::to_string(eye_height_);
  EYE_LOG_INFO("双目来源: " + description_);
  return true;
}

void StereoSource::close() {
  camera_.close();
  file_mode_ = false;
  file_left_.release();
  file_right_.release();
}

bool StereoSource::open_image_pair(const std::string& left_path, const std::string& right_path, std::string* err) {
  close();
  file_left_ = cv::imread(left_path, cv::IMREAD_COLOR);
  file_right_ = cv::imread(right_path, cv::IMREAD_COLOR);
  if (file_left_.empty() || file_right_.empty()) {
    if (err) {
      *err = "读不到图像对: " + left_path + " / " + right_path;
    }
    return false;
  }
  file_mode_ = true;
  eye_width_ = file_left_.cols;
  eye_height_ = file_left_.rows;
  description_ = "image-pair " + left_path + " + " + right_path;
  EYE_LOG_INFO("离线双目来源: " + description_);
  return true;
}

bool StereoSource::split(const cv::Mat& frame, StereoPair& out) const {
  if (frame.cols < 2 || frame.rows < 2) {
    return false;
  }
  const int half = frame.cols / 2;
  cv::Mat left = frame(cv::Rect(0, 0, half, frame.rows));
  cv::Mat right = frame(cv::Rect(half, 0, frame.cols - half, frame.rows));
  if (swap_eyes_) {
    std::swap(left, right);
  }
  // 浅拷贝：保证上层使用时数据仍然有效（frame 由调用方持有）
  out.left = left;
  out.right = right;
  return true;
}

bool StereoSource::grab(StereoPair& out, int timeout_ms) {
  if (file_mode_) {
    if (file_left_.empty()) {
      return false;
    }
    out.left = file_left_.clone();
    out.right = file_right_.clone();
    out.timestamp = now_seconds();
    return true;
  }

  cv::Mat frame;
  if (!camera_.grab(frame, timeout_ms)) {
    return false;
  }
  if (!split(frame, out)) {
    return false;
  }
  out.timestamp = now_seconds();
  return true;
}

}  // namespace eye
