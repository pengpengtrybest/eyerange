#pragma once

#include <opencv2/core.hpp>

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "capture/stereo_source.hpp"
#include "common/config.hpp"
#include "common/types.hpp"
#include "detect/yolo_detector.hpp"
#include "fusion/distance.hpp"
#include "stereo/stereo_depth.hpp"

namespace eye {

// 三段流水线：采集 -> NPU 识别 -> 立体测距（多核并行） -> 发布给界面
class Pipeline {
 public:
  explicit Pipeline(const AppConfig& cfg);
  ~Pipeline();

  // 打开相机与模型，启动线程（离线图片对模式另行调用 open_image_pair）
  bool start(std::string* err);
  bool start_image_pair(const std::string& left, const std::string& right, std::string* err);
  void stop();
  bool running() const { return running_.load(); }

  // 取最近一帧结果（共享指针，避免大图拷贝）
  std::shared_ptr<const ResultFrame> latest() const;

  PipelineStats stats() const;
  void report_display_fps(double fps);

  // ---- 运行期参数 ----
  void set_conf(float value);
  void set_nms(float value);
  float conf() const { return conf_.load(); }
  float nms() const { return nms_.load(); }

  void set_npu_core(int core);
  int npu_core() const { return npu_core_.load(); }

  void set_stereo_params(const StereoConfig& cfg);
  StereoConfig stereo_params() const;

  // ---- 状态描述 ----
  std::string status_text() const;
  std::string source_description() const;
  std::string model_description() const { return model_description_; }
  bool calibrated() const { return depth_.calibrated(); }
  double baseline_mm() const { return depth_.baseline_mm(); }
  double fx_px() const { return depth_.fx(); }
  std::string last_error() const;

 private:
  struct FrameBundle {
    cv::Mat left;
    cv::Mat right;
    double timestamp = 0.0;
    long long seq = 0;
  };

  struct DetectBundle {
    cv::Mat left;
    cv::Mat right;
    std::vector<Detection> detections;
    double timestamp = 0.0;
    long long seq = 0;
  };

  template <typename T>
  class LatestSlot {
   public:
    void set(T&& value) {
      {
        std::lock_guard<std::mutex> lock(mutex_);
        value_ = std::move(value);
        has_value_ = true;
        ++seq_;
      }
      cv_.notify_all();
    }
    bool wait_take(T& out, long long& last_seq, int timeout_ms) {
      std::unique_lock<std::mutex> lock(mutex_);
      const auto ready = [&] { return !running_ || (has_value_ && seq_ != last_seq); };
      if (!cv_.wait_for(lock, std::chrono::milliseconds(timeout_ms), ready)) {
        return false;
      }
      if (!running_ || !has_value_) {
        return false;
      }
      out = value_;
      last_seq = seq_;
      return true;
    }
    void shutdown() {
      {
        std::lock_guard<std::mutex> lock(mutex_);
        running_ = false;
      }
      cv_.notify_all();
    }
    void reset() {
      std::lock_guard<std::mutex> lock(mutex_);
      has_value_ = false;
      seq_ = 0;
      running_ = true;
    }

   private:
    std::mutex mutex_;
    std::condition_variable cv_;
    T value_{};
    bool has_value_ = false;
    bool running_ = true;
    long long seq_ = 0;
  };

  void capture_loop();
  void detect_loop();
  void stereo_loop();
  void publish(ResultFrame&& frame);
  void annotate(ResultFrame& frame) const;
  bool start_threads(std::string* err);

  AppConfig cfg_;
  CalibData calib_;
  StereoSource source_;
  YoloDetector detector_;
  StereoDepth depth_;
  DistanceFilter filter_;

  LatestSlot<FrameBundle> frame_slot_;
  LatestSlot<DetectBundle> pending_;

  mutable std::mutex result_mutex_;
  std::shared_ptr<const ResultFrame> result_;

  mutable std::mutex text_mutex_;
  std::string source_description_;
  std::string model_description_;
  std::string last_error_;
  std::string status_;

  std::atomic<bool> running_{false};
  std::atomic<float> conf_{0.35f};
  std::atomic<float> nms_{0.45f};
  std::atomic<int> npu_core_{-1};
  std::atomic<bool> reload_model_{false};
  std::atomic<int> display_interval_{2};

  std::thread capture_thread_;
  std::thread detect_thread_;
  std::thread stereo_thread_;

  // 统计
  std::atomic<long long> captured_{0};
  std::atomic<long long> detected_{0};
  std::atomic<long long> measured_{0};
  std::atomic<double> capture_fps_{0.0};
  std::atomic<double> detect_fps_{0.0};
  std::atomic<double> stereo_fps_{0.0};
  std::atomic<double> display_fps_{0.0};
  std::atomic<double> preprocess_ms_{0.0};
  std::atomic<double> infer_ms_{0.0};
  std::atomic<double> postprocess_ms_{0.0};
  std::atomic<double> stereo_ms_{0.0};
  std::atomic<double> pipeline_ms_{0.0};
  std::atomic<int> last_npu_core_{-1};
  long long block_counter_ = 0;
  long long capture_win_start_ = 0;
  long long detect_win_start_ = 0;
  long long stereo_win_start_ = 0;
  long long capture_win_count_ = 0;
  long long detect_win_count_ = 0;
  long long stereo_win_count_ = 0;
};

}  // namespace eye
