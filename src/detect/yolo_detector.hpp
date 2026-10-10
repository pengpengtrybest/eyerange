#pragma once

#include <opencv2/core.hpp>

#include <string>
#include <vector>

#include "common/config.hpp"
#include "common/types.hpp"
#include "detect/rknn_engine.hpp"

namespace eye {

// 预处理（letterbox）记录，便于把框映射回原图
struct LetterboxInfo {
  float scale = 1.f;
  int pad_x = 0;
  int pad_y = 0;
  int resized_w = 0;
  int resized_h = 0;
  int in_w = 0;
  int in_h = 0;
};

// YOLOv8 目标检测（RKNN / NPU），解码逻辑对齐 rknn_model_zoo 官方实现
class YoloDetector {
 public:
  bool load(const DetectConfig& cfg, std::string* err);
  void release();
  bool loaded() const { return engine_.loaded(); }

  // 输入 BGR 原图，输出检测框（原图坐标）
  bool detect(const cv::Mat& bgr, std::vector<Detection>& out, std::string* err);

  void set_thresholds(float conf, float nms) {
    conf_ = conf;
    nms_ = nms;
  }
  float conf_threshold() const { return conf_; }
  float nms_threshold() const { return nms_; }

  int class_count() const { return static_cast<int>(labels_.size()); }
  const std::string& label(int id) const;
  int npu_core() const { return engine_.npu_core(); }
  const RknnEngine& engine() const { return engine_; }

  double last_preprocess_ms() const { return preprocess_ms_; }
  double last_infer_ms() const { return infer_ms_; }
  double last_postprocess_ms() const { return postprocess_ms_; }

  // 最近一次 letterbox 信息（调试用）
  const LetterboxInfo& last_letterbox() const { return letterbox_; }

 private:
  void preprocess(const cv::Mat& bgr, std::vector<uint8_t>& rgb, std::vector<float>& pads);
  int decode_quant(const std::vector<cv::Mat>& outputs, std::vector<float>& boxes, std::vector<float>& scores,
                   std::vector<int>& class_ids, float conf_thres);
  int decode_float(const std::vector<cv::Mat>& outputs, std::vector<float>& boxes, std::vector<float>& scores,
                   std::vector<int>& class_ids, float conf_thres);
  int nms_filter(std::vector<float>& boxes, std::vector<float>& scores, std::vector<int>& class_ids, float nms_thres);

  RknnEngine engine_;
  std::vector<std::string> labels_;
  float conf_ = 0.35f;
  float nms_ = 0.45f;
  int max_detections_ = 64;
  LetterboxInfo letterbox_;

  double preprocess_ms_ = 0.0;
  double infer_ms_ = 0.0;
  double postprocess_ms_ = 0.0;
};

}  // namespace eye
