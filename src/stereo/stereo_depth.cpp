#include "stereo/stereo_depth.hpp"

#include <opencv2/imgproc.hpp>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <thread>

#include "common/log.hpp"

namespace eye {
namespace {

constexpr int kMinValidPixels = 24;
constexpr float kDisplayDepthMaxM = 30.f;

double now_ms() {
  return std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now().time_since_epoch()).count();
}

float median_of(std::vector<float>& v) {
  if (v.empty()) {
    return 0.f;
  }
  const size_t mid = v.size() / 2;
  std::nth_element(v.begin(), v.begin() + mid, v.end());
  const float upper = v[mid];
  if (v.size() % 2 == 0) {
    std::nth_element(v.begin(), v.begin() + mid - 1, v.end());
    return 0.5f * (upper + v[mid - 1]);
  }
  return upper;
}

// 视差搜索范围：16 的倍数，且不超过图像宽度允许的上限
int clamp_disparities(int requested, int block_size, int image_width) {
  int nd = std::max(16, (requested / 16) * 16);
  const int bs = std::max(3, block_size | 1);
  const int limit = ((image_width - bs - 16) / 16) * 16;
  nd = std::min(nd, limit);
  return nd < 16 ? 0 : nd;
}

cv::Ptr<cv::StereoSGBM> make_sgbm(const StereoConfig& cfg, int num_disparities) {
  const int bs = std::max(3, cfg.block_size | 1);
  const int p1 = 8 * bs * bs;
  const int p2 = 32 * bs * bs;
  return cv::StereoSGBM::create(cfg.min_disparity, num_disparities, bs, p1, p2, cfg.disp12_max_diff,
                                cfg.pre_filter_cap, cfg.uniqueness_ratio, cfg.speckle_window, cfg.speckle_range,
                                cv::StereoSGBM::MODE_SGBM_3WAY);
}

}  // namespace

bool StereoDepth::configure(const StereoConfig& cfg, const CalibData& calib, const cv::Size& eye_size,
                            std::string* err) {
  {
    std::lock_guard<std::mutex> lock(cfg_mutex_);
    cfg_ = cfg;
  }
  eye_size_ = eye_size;
  calibrated_ = calib.valid;
  have_maps_ = false;
  map_lx_.release();
  map_ly_.release();
  map_rx_.release();
  map_ry_.release();

  if (calibrated_) {
    effective_fx_ = calib.fx;
    effective_baseline_mm_ = calib.baseline_mm;
    doffs_ = calib.doffs;
  } else {
    effective_fx_ = cfg.fallback_fx > 0.0 ? cfg.fallback_fx : eye_size.width * 0.85;
    effective_baseline_mm_ = cfg.fallback_baseline_mm;
    doffs_ = 0.0;
    if (err != nullptr) {
      *err = "未标定：距离为估算值";
    }
  }

  if (calibrated_ && calib.image_size != eye_size && calib.image_size.width > 0 && calib.image_size.height > 0) {
    const double sx = static_cast<double>(eye_size.width) / calib.image_size.width;
    const double sy = static_cast<double>(eye_size.height) / calib.image_size.height;
    EYE_LOG_WARN("标定尺寸与当前单目尺寸不一致，按比例缩放内参 (sx=" + std::to_string(sx) +
                 ", sy=" + std::to_string(sy) + ")");
    cv::Mat K1;
    cv::Mat K2;
    calib.K_left.convertTo(K1, CV_64F);
    calib.K_right.convertTo(K2, CV_64F);
    K1.at<double>(0, 0) *= sx;
    K1.at<double>(0, 2) *= sx;
    K1.at<double>(1, 1) *= sy;
    K1.at<double>(1, 2) *= sy;
    K2.at<double>(0, 0) *= sx;
    K2.at<double>(0, 2) *= sx;
    K2.at<double>(1, 1) *= sy;
    K2.at<double>(1, 2) *= sy;
    cv::Mat R1;
    cv::Mat R2;
    cv::Mat P1;
    cv::Mat P2;
    cv::Mat Q;
    cv::stereoRectify(K1, calib.D_left, K2, calib.D_right, eye_size, calib.R, calib.T, R1, R2, P1, P2, Q,
                      cv::CALIB_ZERO_DISPARITY, 0);
    cv::initUndistortRectifyMap(K1, calib.D_left, R1, P1, eye_size, CV_32FC1, map_lx_, map_ly_);
    cv::initUndistortRectifyMap(K2, calib.D_right, R2, P2, eye_size, CV_32FC1, map_rx_, map_ry_);
    effective_fx_ = P1.at<double>(0, 0);
    doffs_ = P2.at<double>(0, 2) - P1.at<double>(0, 2);
    have_maps_ = true;
  } else if (calibrated_) {
    cv::initUndistortRectifyMap(calib.K_left, calib.D_left, calib.R1, calib.P1, eye_size, CV_32FC1, map_lx_, map_ly_);
    cv::initUndistortRectifyMap(calib.K_right, calib.D_right, calib.R2, calib.P2, eye_size, CV_32FC1, map_rx_,
                                map_ry_);
    have_maps_ = true;
  }

  EYE_LOG_INFO("立体测距: " + std::string(have_maps_ ? "已校正" : "未校正(估算)") + " fx=" +
               std::to_string(effective_fx_) + "px baseline=" + std::to_string(effective_baseline_mm_) +
               "mm 视差范围=" + std::to_string(cfg.num_disparities) +
               " ROI并行=" + std::to_string(cfg.threads) + " OpenCV线程=" + std::to_string(cfg.opencv_threads));
  return true;
}

void StereoDepth::set_params(const StereoConfig& cfg) {
  std::lock_guard<std::mutex> lock(cfg_mutex_);
  cfg_ = cfg;
}

StereoConfig StereoDepth::params() const {
  std::lock_guard<std::mutex> lock(cfg_mutex_);
  return cfg_;
}

bool StereoDepth::rectify_roi(const cv::Mat& left_bgr, const cv::Mat& right_bgr, const cv::Rect& roi, double scale,
                              cv::Mat& left_rect, cv::Mat& right_rect) const {
  const cv::Rect clipped = roi & cv::Rect(0, 0, left_bgr.cols, left_bgr.rows);
  if (clipped.width < 8 || clipped.height < 8) {
    return false;
  }
  const cv::Size dst_size(std::max(8, static_cast<int>(std::lround(clipped.width * scale))),
                          std::max(8, static_cast<int>(std::lround(clipped.height * scale))));

  cv::Mat left_bgr_roi;
  cv::Mat right_bgr_roi;
  if (!have_maps_) {
    // 未标定：直接裁剪（必要时缩放）
    if (std::abs(scale - 1.0) < 1e-3) {
      left_bgr_roi = left_bgr(clipped);
      right_bgr_roi = right_bgr(clipped);
    } else {
      cv::resize(left_bgr(clipped), left_bgr_roi, dst_size, 0, 0, cv::INTER_AREA);
      cv::resize(right_bgr(clipped), right_bgr_roi, dst_size, 0, 0, cv::INTER_AREA);
    }
  } else {
    // 已标定：把全图校正映射裁到 ROI，可选缩小尺寸后 remap
    cv::Mat mx = map_lx_(clipped);
    cv::Mat my = map_ly_(clipped);
    cv::Mat rmx = map_rx_(clipped);
    cv::Mat rmy = map_ry_(clipped);
    if (std::abs(scale - 1.0) > 1e-3) {
      cv::resize(mx, mx, dst_size, 0, 0, cv::INTER_LINEAR);
      cv::resize(my, my, dst_size, 0, 0, cv::INTER_LINEAR);
      cv::resize(rmx, rmx, dst_size, 0, 0, cv::INTER_LINEAR);
      cv::resize(rmy, rmy, dst_size, 0, 0, cv::INTER_LINEAR);
    }
    cv::remap(left_bgr, left_bgr_roi, mx, my, cv::INTER_LINEAR);
    cv::remap(right_bgr, right_bgr_roi, rmx, rmy, cv::INTER_LINEAR);
  }
  cv::cvtColor(left_bgr_roi, left_rect, cv::COLOR_BGR2GRAY);
  cv::cvtColor(right_bgr_roi, right_rect, cv::COLOR_BGR2GRAY);
  return true;
}

StereoDepth::BandResult StereoDepth::sample_region(const cv::Mat& disp_full_res, const cv::Rect& box) const {
  BandResult result;
  const StereoConfig cfg = params();
  const int width = disp_full_res.cols;
  const int height = disp_full_res.rows;
  const int inset_x = std::max(1, box.width / 6);
  const int inset_y = std::max(1, box.height / 6);
  const int x0 = std::max(0, box.x + inset_x);
  const int x1 = std::min(width, box.x + box.width - inset_x);
  const int y0 = std::max(0, box.y + inset_y);
  const int y1 = std::min(height, box.y + box.height - inset_y);
  if (x1 - x0 < 3 || y1 - y0 < 3) {
    result.note = "目标框太小";
    return result;
  }
  const float min_disp = static_cast<float>(cfg.min_disparity) + 0.5f;
  std::vector<float> samples;
  samples.reserve(static_cast<size_t>(x1 - x0) * (y1 - y0));
  for (int y = y0; y < y1; ++y) {
    const float* row = disp_full_res.ptr<float>(y);
    for (int x = x0; x < x1; ++x) {
      if (row[x] > min_disp) {
        samples.push_back(row[x]);
      }
    }
  }
  result.valid_pixels = static_cast<int>(samples.size());
  if (result.valid_pixels < kMinValidPixels) {
    result.note = "有效视差点不足";
    return result;
  }
  result.disparity = median_of(samples);
  return result;
}

StereoDepth::BandResult StereoDepth::measure_roi(const cv::Mat& left_bgr, const cv::Mat& right_bgr,
                                                 const cv::Rect& box) const {
  BandResult result;
  const StereoConfig cfg = params();
  const int img_w = left_bgr.cols;
  const int img_h = left_bgr.rows;
  if (box.width < 4 || box.height < 4) {
    result.note = "目标框太小";
    return result;
  }

  // 1) 水平裁剪：给目标区域留出完整视差搜索范围
  const int nd_full = clamp_disparities(cfg.num_disparities, cfg.block_size, img_w);
  if (nd_full <= 0) {
    result.note = "图像太窄";
    return result;
  }
  const int x0 = std::max(0, box.x - nd_full - cfg.roi_margin);
  const int x1 = std::min(img_w, box.x + box.width + cfg.roi_margin);

  // 2) 垂直只取目标中心附近若干行
  const int band_max = std::max(32, cfg.band_max_height);
  const int band_h = std::min(band_max, std::max(32, box.height + 2 * cfg.roi_margin));
  const int center_y = box.y + box.height / 2;
  int y0 = center_y - band_h / 2;
  y0 = std::max(0, std::min(y0, img_h - band_h));
  const int roi_h = std::min(band_h, img_h);
  const cv::Rect roi(x0, y0, x1 - x0, roi_h);

  // 3) 面积过大时内部降采样
  double scale = std::max(0.25, std::min(1.0, static_cast<double>(cfg.roi_scale)));
  const int longest = std::max(roi.width, roi.height);
  if (cfg.max_roi_side > 0 && longest > cfg.max_roi_side) {
    scale *= static_cast<double>(cfg.max_roi_side) / longest;
  }
  scale = std::max(0.2, std::min(1.0, scale));

  cv::Mat left_gray;
  cv::Mat right_gray;
  if (!rectify_roi(left_bgr, right_bgr, roi, scale, left_gray, right_gray)) {
    result.note = "ROI 裁剪失败";
    return result;
  }

  const int nd = clamp_disparities(cfg.num_disparities, cfg.block_size, left_gray.cols);
  if (nd <= 0) {
    result.note = "ROI 太窄";
    return result;
  }

  cv::Mat disp16;
  make_sgbm(cfg, nd)->compute(left_gray, right_gray, disp16);
  cv::Mat disp;
  disp16.convertTo(disp, CV_32F, 1.0 / 16.0);

  // 4) 在框内中心区域采样（换算到 ROI 坐标）
  const int inset_x = std::max(1, box.width / 6);
  const int inset_y = std::max(1, box.height / 6);
  const int bx0 = std::max(box.x + inset_x, roi.x);
  const int bx1 = std::min(box.x + box.width - inset_x, roi.x + roi.width);
  const int by0 = std::max(box.y + inset_y, roi.y + 2);
  const int by1 = std::min(box.y + box.height - inset_y, roi.y + roi.height - 2);
  if (bx1 - bx0 < 3 || by1 - by0 < 3) {
    result.note = "目标框太小";
    return result;
  }

  std::vector<float> samples;
  samples.reserve(static_cast<size_t>(bx1 - bx0) * (by1 - by0));
  const float min_disp = static_cast<float>(cfg.min_disparity) + 0.5f;
  for (int y = by0; y < by1; ++y) {
    const int ry = static_cast<int>(std::lround((y - roi.y) * scale));
    if (ry < 0 || ry >= disp.rows) {
      continue;
    }
    const float* row = disp.ptr<float>(ry);
    for (int x = bx0; x < bx1; ++x) {
      const int rx = static_cast<int>(std::lround((x - roi.x) * scale));
      if (rx < 0 || rx >= disp.cols) {
        continue;
      }
      const float d = row[rx];
      if (d > min_disp && d < static_cast<float>(nd)) {
        samples.push_back(d / static_cast<float>(scale));  // 换算回全分辨率像素
      }
    }
  }
  result.valid_pixels = static_cast<int>(samples.size());
  if (result.valid_pixels < kMinValidPixels) {
    result.note = "有效视差点不足";
    return result;
  }
  result.disparity = median_of(samples);
  return result;
}

float StereoDepth::depth_from_disparity(float disparity_px) const {
  const double d = static_cast<double>(disparity_px) - doffs_;
  if (d <= 0.5 || effective_fx_ <= 0.0 || effective_baseline_mm_ <= 0.0) {
    return 0.f;
  }
  return static_cast<float>(effective_fx_ * effective_baseline_mm_ / d / 1000.0);
}

void StereoDepth::measure_boxes(const cv::Mat& left_bgr, const cv::Mat& right_bgr,
                                std::vector<Detection>& dets) const {
  if (dets.empty() || left_bgr.empty() || right_bgr.empty()) {
    return;
  }
  const StereoConfig cfg = params();
  const double t0 = now_ms();
  const int n = static_cast<int>(dets.size());
  std::vector<BandResult> results(static_cast<size_t>(n));

  if (!cfg.roi_only) {
    cv::Mat disp;
    if (!compute_full_disparity(left_bgr, right_bgr, disp, cfg.num_disparities)) {
      return;
    }
    for (int i = 0; i < n; ++i) {
      results[i] = sample_region(disp, dets[i].box);
    }
  } else {
    const int threads = std::max(1, std::min(cfg.threads, n));
    if (threads == 1) {
      for (int i = 0; i < n; ++i) {
        results[i] = measure_roi(left_bgr, right_bgr, dets[i].box);
      }
    } else {
      std::atomic<int> next{0};
      std::vector<std::thread> pool;
      pool.reserve(static_cast<size_t>(threads));
      for (int t = 0; t < threads; ++t) {
        pool.emplace_back([&]() {
          for (;;) {
            const int i = next.fetch_add(1);
            if (i >= n) {
              break;
            }
            results[i] = measure_roi(left_bgr, right_bgr, dets[i].box);
          }
        });
      }
      for (std::thread& th : pool) {
        th.join();
      }
    }
  }

  for (int i = 0; i < n; ++i) {
    Detection& det = dets[i];
    det.disparity_px = results[i].disparity;
    det.valid_pixels = results[i].valid_pixels;
    det.note = results[i].note;
    det.distance_m = results[i].disparity > 0.f ? depth_from_disparity(results[i].disparity) : 0.f;
  }
  last_measure_ms_.store(now_ms() - t0);
}

bool StereoDepth::compute_full_disparity(const cv::Mat& left_bgr, const cv::Mat& right_bgr, cv::Mat& disp_full_res,
                                         int nd_cap) const {
  if (left_bgr.empty() || right_bgr.empty()) {
    return false;
  }
  const StereoConfig cfg = params();
  const double scale = std::max(0.1, std::min(1.0, static_cast<double>(cfg.display_scale)));
  const cv::Size small(std::max(16, static_cast<int>(left_bgr.cols * scale)),
                       std::max(16, static_cast<int>(left_bgr.rows * scale)));
  const cv::Rect full_roi(0, 0, left_bgr.cols, left_bgr.rows);

  cv::Mat left_gray;
  cv::Mat right_gray;
  if (!rectify_roi(left_bgr, right_bgr, full_roi, scale, left_gray, right_gray)) {
    return false;
  }
  cv::Mat left_small;
  cv::Mat right_small;
  cv::resize(left_gray, left_small, small, 0, 0, cv::INTER_AREA);
  cv::resize(right_gray, right_small, small, 0, 0, cv::INTER_AREA);

  const int nd = clamp_disparities(std::min(cfg.num_disparities, nd_cap), cfg.block_size, left_small.cols);
  if (nd <= 0) {
    return false;
  }
  cv::Mat disp16;
  make_sgbm(cfg, nd)->compute(left_small, right_small, disp16);
  disp16.convertTo(disp_full_res, CV_32F, 1.0 / (16.0 * scale));
  return true;
}

bool StereoDepth::compute_display(const cv::Mat& left_bgr, const cv::Mat& right_bgr, cv::Mat& disparity_vis,
                                  cv::Mat& depth_vis) const {
  const double t0 = now_ms();
  const StereoConfig cfg = params();
  cv::Mat disp;
  if (!compute_full_disparity(left_bgr, right_bgr, disp, cfg.display_num_disparities)) {
    return false;
  }

  cv::Mat valid_mask = disp > 0.5f;
  double min_d = 0.0;
  double max_d = 0.0;
  cv::minMaxLoc(disp, &min_d, &max_d, nullptr, nullptr, valid_mask);
  if (max_d <= min_d) {
    max_d = min_d + 1.0;
  }
  cv::Mat disp_u8;
  disp.convertTo(disp_u8, CV_8U, 255.0 / (max_d - min_d), -min_d * 255.0 / (max_d - min_d));
  disp_u8.setTo(0, ~valid_mask);
  cv::applyColorMap(disp_u8, disparity_vis, cv::COLORMAP_JET);
  disparity_vis.setTo(cv::Scalar(0, 0, 0), ~valid_mask);

  cv::Mat depth(disp.size(), CV_32F, cv::Scalar(0));
  for (int y = 0; y < disp.rows; ++y) {
    const float* drow = disp.ptr<float>(y);
    const uchar* mrow = valid_mask.ptr<uchar>(y);
    float* orow = depth.ptr<float>(y);
    for (int x = 0; x < disp.cols; ++x) {
      if (mrow[x] != 0) {
        const float z = depth_from_disparity(drow[x]);
        orow[x] = z > 0.f ? std::min(z, kDisplayDepthMaxM) : 0.f;
      }
    }
  }
  cv::Mat depth_u8;
  depth.convertTo(depth_u8, CV_8U, 255.0 / kDisplayDepthMaxM);
  depth_u8.setTo(0, ~valid_mask);
  cv::applyColorMap(depth_u8, depth_vis, cv::COLORMAP_TURBO);
  depth_vis.setTo(cv::Scalar(0, 0, 0), ~valid_mask);

  last_display_ms_.store(now_ms() - t0);
  return true;
}

}  // namespace eye
