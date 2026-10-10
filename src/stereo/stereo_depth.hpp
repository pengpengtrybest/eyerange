#pragma once

#include <opencv2/calib3d.hpp>
#include <opencv2/core.hpp>

#include <atomic>
#include <mutex>
#include <string>
#include <vector>

#include "common/config.hpp"
#include "common/types.hpp"

namespace eye {

// 立体匹配 + 测距
//
// 关键设计：不做整幅稠密匹配，而是对每个检测框取一条“全宽水平带”做 SGBM。
//   * 全宽 -> 保留完整视差搜索范围（否则窄 ROI 的视差上限会严重限制测距距离）
//   * 只在带内做匹配 -> 计算量按带宽线性下降，RK3588 多核可并行多个目标
class StereoDepth {
 public:
  bool configure(const StereoConfig& cfg, const CalibData& calib, const cv::Size& eye_size, std::string* err);

  // 运行期热更新参数（仅影响后续计算）
  void set_params(const StereoConfig& cfg);
  StereoConfig params() const;

  // 为每个检测框计算视差中值并换算成距离（det.distance_m / disparity_px 会被填充）
  void measure_boxes(const cv::Mat& left_bgr, const cv::Mat& right_bgr, std::vector<Detection>& dets) const;

  // 供界面显示的低分辨率视差/深度伪彩图
  bool compute_display(const cv::Mat& left_bgr, const cv::Mat& right_bgr, cv::Mat& disparity_vis,
                       cv::Mat& depth_vis) const;

  float depth_from_disparity(float disparity_px) const;
  bool calibrated() const { return calibrated_; }
  bool rectified() const { return have_maps_; }
  double fx() const { return effective_fx_; }
  double baseline_mm() const { return effective_baseline_mm_; }
  const cv::Size& eye_size() const { return eye_size_; }
  double last_measure_ms() const { return last_measure_ms_.load(); }
  double last_display_ms() const { return last_display_ms_.load(); }

 private:
  struct BandResult {
    float disparity = 0.f;
    int valid_pixels = 0;
    std::string note;
  };

  // 在目标框附近做一次窄带 SGBM：
  //   水平裁剪到 [box.x - nd, box.x + box.w]，保证目标区域拥有完整视差搜索范围；
  //   垂直只取目标中心附近的若干行，计算量按带宽线性下降。
  BandResult measure_roi(const cv::Mat& left_bgr, const cv::Mat& right_bgr, const cv::Rect& box) const;

  // 整幅（按 display_scale 降采样）视差，单位为全分辨率像素
  bool compute_full_disparity(const cv::Mat& left_bgr, const cv::Mat& right_bgr, cv::Mat& disp_full_res,
                              int nd_cap) const;

  // 从视差图中采样某个框内的中值视差
  BandResult sample_region(const cv::Mat& disp_full_res, const cv::Rect& box) const;

  // 取 ROI 并做校正（可选缩放），输出灰度图；out_scale 为输出相对全分辨率的比例
  bool rectify_roi(const cv::Mat& left_bgr, const cv::Mat& right_bgr, const cv::Rect& roi, double scale,
                   cv::Mat& left_rect, cv::Mat& right_rect) const;

  StereoConfig cfg_;
  cv::Size eye_size_;
  bool calibrated_ = false;
  bool have_maps_ = false;
  double effective_fx_ = 0.0;
  double effective_baseline_mm_ = 0.0;
  double doffs_ = 0.0;

  cv::Mat map_lx_, map_ly_, map_rx_, map_ry_;  // CV_32FC1
  mutable std::mutex cfg_mutex_;
  mutable std::atomic<double> last_measure_ms_{0.0};
  mutable std::atomic<double> last_display_ms_{0.0};
};

}  // namespace eye
