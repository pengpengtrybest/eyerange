#include "fusion/distance.hpp"

#include <algorithm>
#include <cmath>
#include <numeric>

namespace eye {
namespace {

float iou(const cv::Rect2f& a, const cv::Rect2f& b) {
  const float x1 = std::max(a.x, b.x);
  const float y1 = std::max(a.y, b.y);
  const float x2 = std::min(a.x + a.width, b.x + b.width);
  const float y2 = std::min(a.y + a.height, b.y + b.height);
  const float w = std::max(0.f, x2 - x1);
  const float h = std::max(0.f, y2 - y1);
  const float inter = w * h;
  const float uni = a.area() + b.area() - inter;
  return uni <= 0.f ? 0.f : inter / uni;
}

}  // namespace

void DistanceFilter::reset() {
  tracks_.clear();
  next_id_ = 1;
}

void DistanceFilter::update(std::vector<Detection>& dets, double timestamp) {
  // 1) 先按 IoU 关联到已有轨迹
  //
  // 注意：匹配只针对进入本函数时已存在的轨迹（固定上界 existing）。
  // 本帧新建的轨迹从下一帧才开始参与匹配，避免在遍历过程中修改容器
  // 造成的迭代器/引用失效（GCC -O3 下曾因此出现空指针解引用）。
  const size_t existing = tracks_.size();
  std::vector<char> track_used(existing, 0);
  for (Detection& det : dets) {
    int best = -1;
    float best_iou = opt_.iou_threshold;
    for (size_t i = 0; i < existing; ++i) {
      if (track_used[i] || tracks_[i].cls_id != det.cls_id) {
        continue;
      }
      // 只跟最近的轨迹比
      if (timestamp - tracks_[i].last_ts > opt_.max_age_s) {
        continue;
      }
      const float score = iou(tracks_[i].box, det.box);
      if (score > best_iou) {
        best_iou = score;
        best = static_cast<int>(i);
      }
    }
    if (best >= 0) {
      track_used[best] = 1;
      det.track_id = tracks_[best].id;
      tracks_[best].box = det.box;
      tracks_[best].last_ts = timestamp;
      tracks_[best].hits += 1;
      if (det.distance_m > 0.f) {
        std::vector<float>& history = tracks_[best].history;
        history.push_back(det.distance_m);
        if (static_cast<int>(history.size()) > opt_.smooth_window) {
          history.erase(history.begin());
        }
        const float mean = std::accumulate(history.begin(), history.end(), 0.f) / static_cast<float>(history.size());
        tracks_[best].smooth_distance = tracks_[best].has_distance
                                            ? (1.f - opt_.alpha) * tracks_[best].smooth_distance + opt_.alpha * mean
                                            : mean;
        tracks_[best].has_distance = true;
      }
    } else {
      Track tr;
      tr.id = next_id_++;
      tr.cls_id = det.cls_id;
      tr.box = det.box;
      tr.last_ts = timestamp;
      tr.hits = 1;
      tr.has_distance = det.distance_m > 0.f;
      tr.smooth_distance = det.distance_m;
      if (tr.has_distance) {
        tr.history.push_back(det.distance_m);
      }
      tracks_.push_back(tr);
      det.track_id = tr.id;
    }
  }

  // 2) 用平滑后的距离回填
  for (Detection& det : dets) {
    for (const Track& tr : tracks_) {
      if (tr.id == det.track_id && tr.has_distance && det.distance_m > 0.f) {
        det.distance_m = tr.smooth_distance;
        break;
      }
    }
  }

  // 3) 清理过期轨迹
  tracks_.erase(std::remove_if(tracks_.begin(), tracks_.end(),
                               [&](const Track& t) { return timestamp - t.last_ts > opt_.max_age_s; }),
                tracks_.end());
}

}  // namespace eye
