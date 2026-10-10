#pragma once

#include <opencv2/core.hpp>

#include <vector>

#include "common/types.hpp"

namespace eye {

// 跨帧关联 + 距离平滑：避免距离数字剧烈跳动，便于人工观察
class DistanceFilter {
 public:
  struct Options {
    float iou_threshold = 0.25f;
    double max_age_s = 0.6;
    float alpha = 0.45f;   // EMA 系数，越大越跟随当前帧
    int smooth_window = 5; // 距离滑动平均窗口
  };

  DistanceFilter() = default;
  explicit DistanceFilter(const Options& opt) : opt_(opt) {}

  void update(std::vector<Detection>& dets, double timestamp);
  void reset();
  int track_count() const { return static_cast<int>(tracks_.size()); }

 private:
  struct Track {
    int id = -1;
    int cls_id = -1;
    cv::Rect2f box;
    double last_ts = 0.0;
    int hits = 0;
    float smooth_distance = 0.f;
    bool has_distance = false;
    std::vector<float> history;
  };

  Options opt_;
  std::vector<Track> tracks_;
  int next_id_ = 1;
};

}  // namespace eye
