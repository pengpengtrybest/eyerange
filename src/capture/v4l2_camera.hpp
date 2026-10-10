#pragma once

#include <opencv2/core.hpp>

#include <string>
#include <vector>

namespace eye {

// 直接使用 V4L2 MMAP 接口采集 MJPG 视频流，避免依赖 GStreamer。
// RK3588 上的 UVC 双目模块通常以“左右拼接”的 MJPG 帧输出。
class V4l2Camera {
 public:
  V4l2Camera() = default;
  ~V4l2Camera();

  V4l2Camera(const V4l2Camera&) = delete;
  V4l2Camera& operator=(const V4l2Camera&) = delete;

  bool open(const std::string& device, int width, int height, int fps, int buffer_count, std::string* err);
  void close();
  bool is_open() const { return fd_ >= 0; }

  // 阻塞/超时抓一帧并解码为 BGR；timeout_ms < 0 表示一直等待
  bool grab(cv::Mat& bgr, int timeout_ms);

  int width() const { return width_; }
  int height() const { return height_; }
  const std::string& device() const { return device_; }
  const std::string& format() const { return format_name_; }

  // 累计统计
  long long frame_count() const { return frame_count_; }
  double last_decode_ms() const { return last_decode_ms_; }

 private:
  struct Buffer {
    void* start = nullptr;
    size_t length = 0;
  };

  bool init_mmap(int buffer_count, std::string* err);
  bool queue_buffer(int index, std::string* err);

  int fd_ = -1;
  std::string device_;
  std::string format_name_;
  int width_ = 0;
  int height_ = 0;
  std::vector<Buffer> buffers_;
  long long frame_count_ = 0;
  double last_decode_ms_ = 0.0;
};

}  // namespace eye
