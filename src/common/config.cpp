#include "common/config.hpp"

#include <opencv2/calib3d.hpp>

#include "common/log.hpp"

namespace eye {
namespace {

int read_int(const cv::FileNode& fs, const char* name, int fallback) {
  const cv::FileNode n = fs[name];
  return n.empty() ? fallback : static_cast<int>(n);
}

double read_double(const cv::FileNode& fs, const char* name, double fallback) {
  const cv::FileNode n = fs[name];
  return n.empty() ? fallback : static_cast<double>(n);
}

std::string read_string(const cv::FileNode& fs, const char* name, const std::string& fallback) {
  const cv::FileNode n = fs[name];
  return n.empty() ? fallback : static_cast<std::string>(n);
}

bool read_bool(const cv::FileNode& fs, const char* name, bool fallback) {
  const cv::FileNode n = fs[name];
  return n.empty() ? fallback : static_cast<int>(n) != 0;
}

}  // namespace

AppConfig AppConfig::load(const std::string& path) {
  AppConfig cfg;
  cv::FileStorage fs(path, cv::FileStorage::READ);
  if (!fs.isOpened()) {
    EYE_LOG_WARN("配置文件打不开，使用内置默认值: " + path);
    return cfg;
  }

  const cv::FileNode cap = fs["capture"];
  if (!cap.empty()) {
    cfg.capture.device = read_string(cap, "device", cfg.capture.device);
    cfg.capture.width = read_int(cap, "width", cfg.capture.width);
    cfg.capture.height = read_int(cap, "height", cfg.capture.height);
    cfg.capture.fps = read_int(cap, "fps", cfg.capture.fps);
    cfg.capture.buffer_count = read_int(cap, "buffer_count", cfg.capture.buffer_count);
    cfg.capture.swap_eyes = read_bool(cap, "swap_eyes", cfg.capture.swap_eyes);
  }

  const cv::FileNode det = fs["detect"];
  if (!det.empty()) {
    cfg.detect.model = read_string(det, "model", cfg.detect.model);
    cfg.detect.labels = read_string(det, "labels", cfg.detect.labels);
    cfg.detect.conf = static_cast<float>(read_double(det, "conf", cfg.detect.conf));
    cfg.detect.nms = static_cast<float>(read_double(det, "nms", cfg.detect.nms));
    cfg.detect.npu_core = read_int(det, "npu_core", cfg.detect.npu_core);
    cfg.detect.max_detections = read_int(det, "max_detections", cfg.detect.max_detections);
  }

  const cv::FileNode st = fs["stereo"];
  if (!st.empty()) {
    cfg.stereo.calib = read_string(st, "calib", cfg.stereo.calib);
    cfg.stereo.num_disparities = read_int(st, "num_disparities", cfg.stereo.num_disparities);
    cfg.stereo.min_disparity = read_int(st, "min_disparity", cfg.stereo.min_disparity);
    cfg.stereo.block_size = read_int(st, "block_size", cfg.stereo.block_size);
    cfg.stereo.uniqueness_ratio = read_int(st, "uniqueness_ratio", cfg.stereo.uniqueness_ratio);
    cfg.stereo.disp12_max_diff = read_int(st, "disp12_max_diff", cfg.stereo.disp12_max_diff);
    cfg.stereo.speckle_window = read_int(st, "speckle_window", cfg.stereo.speckle_window);
    cfg.stereo.speckle_range = read_int(st, "speckle_range", cfg.stereo.speckle_range);
    cfg.stereo.pre_filter_cap = read_int(st, "pre_filter_cap", cfg.stereo.pre_filter_cap);
    cfg.stereo.use_wls = read_bool(st, "use_wls", cfg.stereo.use_wls);
    cfg.stereo.roi_only = read_bool(st, "roi_only", cfg.stereo.roi_only);
    cfg.stereo.roi_scale = static_cast<float>(read_double(st, "roi_scale", cfg.stereo.roi_scale));
    cfg.stereo.roi_margin = read_int(st, "roi_margin", cfg.stereo.roi_margin);
    cfg.stereo.max_roi_side = read_int(st, "max_roi_side", cfg.stereo.max_roi_side);
    cfg.stereo.band_max_height = read_int(st, "band_max_height", cfg.stereo.band_max_height);
    cfg.stereo.display_scale = static_cast<float>(read_double(st, "display_scale", cfg.stereo.display_scale));
    cfg.stereo.display_num_disparities =
        read_int(st, "display_num_disparities", cfg.stereo.display_num_disparities);
    cfg.stereo.threads = read_int(st, "threads", cfg.stereo.threads);
    cfg.stereo.opencv_threads = read_int(st, "opencv_threads", cfg.stereo.opencv_threads);
    cfg.stereo.min_confidence = static_cast<float>(read_double(st, "min_confidence", cfg.stereo.min_confidence));
    cfg.stereo.fallback_baseline_mm = read_double(st, "fallback_baseline_mm", cfg.stereo.fallback_baseline_mm);
    cfg.stereo.fallback_fx = read_double(st, "fallback_fx", cfg.stereo.fallback_fx);
  }

  const cv::FileNode ui = fs["ui"];
  if (!ui.empty()) {
    cfg.ui.fullscreen = read_bool(ui, "fullscreen", cfg.ui.fullscreen);
    cfg.ui.borderless = read_bool(ui, "borderless", cfg.ui.borderless);
    cfg.ui.show_depth = read_bool(ui, "show_depth", cfg.ui.show_depth);
    cfg.ui.show_disparity = read_bool(ui, "show_disparity", cfg.ui.show_disparity);
    cfg.ui.fps = read_int(ui, "fps", cfg.ui.fps);
    cfg.ui.smooth_scale = read_bool(ui, "smooth_scale", cfg.ui.smooth_scale);
    cfg.ui.window_width = read_int(ui, "window_width", cfg.ui.window_width);
    cfg.ui.window_height = read_int(ui, "window_height", cfg.ui.window_height);
  }
  return cfg;
}

void AppConfig::save(const std::string& path) const {
  cv::FileStorage fs(path, cv::FileStorage::WRITE);
  if (!fs.isOpened()) {
    EYE_LOG_ERROR("无法写出配置文件: " + path);
    return;
  }
  fs << "capture"
     << "{" << "device" << capture.device << "width" << capture.width << "height" << capture.height << "fps"
     << capture.fps << "buffer_count" << capture.buffer_count << "swap_eyes" << static_cast<int>(capture.swap_eyes)
     << "}";
  fs << "detect"
     << "{" << "model" << detect.model << "labels" << detect.labels << "conf" << detect.conf << "nms" << detect.nms
     << "npu_core" << detect.npu_core << "max_detections" << detect.max_detections << "}";
  fs << "stereo" << "{" << "calib" << stereo.calib << "num_disparities" << stereo.num_disparities << "min_disparity"
     << stereo.min_disparity << "block_size" << stereo.block_size << "uniqueness_ratio" << stereo.uniqueness_ratio
     << "disp12_max_diff" << stereo.disp12_max_diff << "speckle_window" << stereo.speckle_window << "speckle_range"
     << stereo.speckle_range << "pre_filter_cap" << stereo.pre_filter_cap << "use_wls"
     << static_cast<int>(stereo.use_wls) << "roi_only" << static_cast<int>(stereo.roi_only) << "roi_scale"
     << stereo.roi_scale << "roi_margin" << stereo.roi_margin << "max_roi_side" << stereo.max_roi_side
     << "band_max_height" << stereo.band_max_height << "display_scale" << stereo.display_scale
     << "display_num_disparities" << stereo.display_num_disparities << "threads" << stereo.threads
     << "opencv_threads" << stereo.opencv_threads << "min_confidence"
     << stereo.min_confidence << "fallback_baseline_mm" << stereo.fallback_baseline_mm << "fallback_fx"
     << stereo.fallback_fx << "}";
  fs << "ui" << "{" << "fullscreen" << static_cast<int>(ui.fullscreen) << "borderless"
     << static_cast<int>(ui.borderless) << "show_depth" << static_cast<int>(ui.show_depth) << "show_disparity"
     << static_cast<int>(ui.show_disparity) << "window_width"
     << ui.window_width << "window_height" << ui.window_height << "fps" << ui.fps << "smooth_scale"
     << static_cast<int>(ui.smooth_scale) << "}";
  fs.release();
}

CalibData CalibData::load(const std::string& path) {
  CalibData calib;
  cv::FileStorage fs(path, cv::FileStorage::READ);
  if (!fs.isOpened()) {
    EYE_LOG_WARN("标定文件不存在或无法打开: " + path);
    return calib;
  }

  const cv::FileNode root = fs.root();
  const int w = read_int(root, "image_width", 0);
  const int h = read_int(root, "image_height", 0);
  calib.image_size = cv::Size(w, h);

  fs["M1"] >> calib.K_left;
  fs["D1"] >> calib.D_left;
  fs["M2"] >> calib.K_right;
  fs["D2"] >> calib.D_right;
  fs["R"] >> calib.R;
  fs["T"] >> calib.T;
  fs["R1"] >> calib.R1;
  fs["R2"] >> calib.R2;
  fs["P1"] >> calib.P1;
  fs["P2"] >> calib.P2;
  fs["Q"] >> calib.Q;
  calib.source = path;

  if (calib.K_left.empty() || calib.K_right.empty() || calib.T.empty()) {
    EYE_LOG_WARN("标定文件字段不完整: " + path);
    return calib;
  }

  // 没有 R1/P1 时现场算一次校正参数
  if (calib.R1.empty() || calib.P1.empty() || calib.Q.empty()) {
    if (calib.R.empty()) {
      calib.R = cv::Mat::eye(3, 3, CV_64F);
    }
    if (calib.D_left.empty()) {
      calib.D_left = cv::Mat::zeros(1, 5, CV_64F);
    }
    if (calib.D_right.empty()) {
      calib.D_right = cv::Mat::zeros(1, 5, CV_64F);
    }
    if (calib.image_size.width <= 0 || calib.image_size.height <= 0) {
      EYE_LOG_WARN("标定文件缺少 image_width/height，无法计算校正参数");
      return calib;
    }
    cv::stereoRectify(calib.K_left, calib.D_left, calib.K_right, calib.D_right, calib.image_size, calib.R, calib.T,
                      calib.R1, calib.R2, calib.P1, calib.P2, calib.Q, cv::CALIB_ZERO_DISPARITY, 0);
  }

  calib.baseline_mm = std::abs(calib.T.at<double>(0, 0));
  calib.fx = calib.P1.at<double>(0, 0);
  calib.cx = calib.P1.at<double>(0, 2);
  calib.cy = calib.P1.at<double>(1, 2);
  calib.doffs = calib.P2.at<double>(0, 2) - calib.P1.at<double>(0, 2);
  calib.valid = calib.baseline_mm > 0.0 && calib.fx > 0.0;
  return calib;
}

}  // namespace eye
