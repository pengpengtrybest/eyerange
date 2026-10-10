#include "detect/yolo_detector.hpp"

#include <opencv2/imgproc.hpp>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <fstream>

#include "common/log.hpp"

namespace eye {
namespace {

constexpr int kLetterboxColor = 114;  // 与 rknn_model_zoo 一致

inline float dequant(int8_t q, int32_t zp, float scale) { return (static_cast<float>(q) - static_cast<float>(zp)) * scale; }

inline float dequant_u8(uint8_t q, int32_t zp, float scale) {
  return (static_cast<float>(q) - static_cast<float>(zp)) * scale;
}

inline int8_t quant_i8(float v, int32_t zp, float scale) {
  const float raw = v / scale + static_cast<float>(zp);
  return static_cast<int8_t>(std::max(-128.f, std::min(127.f, raw)));
}

// DFL 解码：把 dfl_len 个 logits 变成 box 的 4 个期望值
void compute_dfl(const float* tensor, int dfl_len, float* box) {
  for (int b = 0; b < 4; ++b) {
    float exp_sum = 0.f;
    float acc_sum = 0.f;
    std::vector<float> exp_t(static_cast<size_t>(dfl_len));
    for (int i = 0; i < dfl_len; ++i) {
      exp_t[i] = std::exp(tensor[i + b * dfl_len]);
      exp_sum += exp_t[i];
    }
    for (int i = 0; i < dfl_len; ++i) {
      acc_sum += exp_t[i] / exp_sum * static_cast<float>(i);
    }
    box[b] = acc_sum;
  }
}

float intersection_over_union(const float* a, const float* b) {
  const float x1 = std::max(a[0], b[0]);
  const float y1 = std::max(a[1], b[1]);
  const float x2 = std::min(a[0] + a[2], b[0] + b[2]);
  const float y2 = std::min(a[1] + a[3], b[1] + b[3]);
  const float w = std::max(0.f, x2 - x1);
  const float h = std::max(0.f, y2 - y1);
  const float inter = w * h;
  const float uni = a[2] * a[3] + b[2] * b[3] - inter;
  return uni <= 0.f ? 0.f : inter / uni;
}

double now_ms() {
  return std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now().time_since_epoch()).count();
}

}  // namespace

bool YoloDetector::load(const DetectConfig& cfg, std::string* err) {
  conf_ = cfg.conf;
  nms_ = cfg.nms;
  max_detections_ = cfg.max_detections;

  labels_.clear();
  std::ifstream ifs(cfg.labels);
  if (ifs) {
    std::string line;
    while (std::getline(ifs, line)) {
      while (!line.empty() && (line.back() == '\r' || line.back() == '\n')) {
        line.pop_back();
      }
      if (!line.empty()) {
        labels_.push_back(line);
      }
    }
  } else {
    EYE_LOG_WARN("标签文件打不开: " + cfg.labels);
  }

  if (!engine_.load(cfg.model, cfg.npu_core, err)) {
    return false;
  }
  if (engine_.model_channel() != 3) {
    if (err) {
      *err = "当前只支持 3 通道输入的 YOLO 模型";
    }
    return false;
  }
  if (!labels_.empty() && static_cast<int>(labels_.size()) < 80) {
    EYE_LOG_WARN("标签数量偏少: " + std::to_string(labels_.size()));
  }
  return true;
}

void YoloDetector::release() { engine_.release(); }

const std::string& YoloDetector::label(int id) const {
  static const std::string unknown = "class";
  if (id >= 0 && id < static_cast<int>(labels_.size())) {
    return labels_[id];
  }
  return unknown;
}

void YoloDetector::preprocess(const cv::Mat& bgr, std::vector<uint8_t>& rgb, std::vector<float>& pads) {
  const int dst_w = engine_.model_width();
  const int dst_h = engine_.model_height();
  const int src_w = bgr.cols;
  const int src_h = bgr.rows;

  // 与 rknn_model_zoo convert_image_with_letterbox 相同的对齐规则
  const float scale_w = static_cast<float>(dst_w) / static_cast<float>(src_w);
  const float scale_h = static_cast<float>(dst_h) / static_cast<float>(src_h);
  float scale = std::min(scale_w, scale_h);
  int resize_w = dst_w;
  int resize_h = dst_h;
  if (scale_w < scale_h) {
    resize_h = static_cast<int>(src_h * scale);
  } else {
    resize_w = static_cast<int>(src_w * scale);
  }
  if (resize_w % 4 != 0) {
    resize_w -= resize_w % 4;
  }
  if (resize_h % 2 != 0) {
    resize_h -= resize_h % 2;
  }
  int pad_x = 0;
  int pad_y = 0;
  if (scale_w < scale_h) {
    pad_y = (dst_h - resize_h) / 2;
    if (pad_y % 2 != 0) {
      pad_y -= pad_y % 2;
    }
  } else {
    pad_x = (dst_w - resize_w) / 2;
    if (pad_x % 2 != 0) {
      pad_x -= pad_x % 2;
    }
  }
  // 实际用的比例
  scale = static_cast<float>(resize_w) / static_cast<float>(src_w);

  letterbox_.scale = scale;
  letterbox_.pad_x = pad_x;
  letterbox_.pad_y = pad_y;
  letterbox_.resized_w = resize_w;
  letterbox_.resized_h = resize_h;
  letterbox_.in_w = dst_w;
  letterbox_.in_h = dst_h;

  rgb.assign(static_cast<size_t>(dst_w) * dst_h * 3, static_cast<uint8_t>(kLetterboxColor));
  cv::Mat canvas(dst_h, dst_w, CV_8UC3, rgb.data());
  cv::Mat roi = canvas(cv::Rect(pad_x, pad_y, resize_w, resize_h));
  cv::Mat resized;
  cv::resize(bgr, resized, cv::Size(resize_w, resize_h), 0, 0, cv::INTER_LINEAR);
  cv::cvtColor(resized, roi, cv::COLOR_BGR2RGB);

  pads.clear();
  pads.push_back(static_cast<float>(pad_x));
  pads.push_back(static_cast<float>(pad_y));
}

int YoloDetector::decode_quant(const std::vector<cv::Mat>& outputs, std::vector<float>& boxes,
                               std::vector<float>& scores, std::vector<int>& class_ids, float conf_thres) {
  const auto& attrs = engine_.output_attrs();
  if (attrs.empty() || outputs.size() < 2) {
    return 0;
  }
  const int branches = 3;
  const int per_branch = static_cast<int>(outputs.size()) / branches;
  if (per_branch < 2) {
    return 0;
  }
  const int dfl_len = static_cast<int>(attrs[0].dims[1]) / 4;
  const int class_num = static_cast<int>(attrs[1].dims[1]);
  if (dfl_len <= 0 || class_num <= 0 || class_num > 1024) {
    return 0;
  }
  int valid = 0;

  for (int b = 0; b < branches; ++b) {
    const int box_idx = b * per_branch;
    const int score_idx = box_idx + 1;
    const int grid_h = static_cast<int>(attrs[box_idx].dims[2]);
    const int grid_w = static_cast<int>(attrs[box_idx].dims[3]);
    const int grid_len = grid_h * grid_w;
    const int stride = engine_.model_height() / grid_h;

    const int8_t* box_tensor = reinterpret_cast<const int8_t*>(outputs[box_idx].data);
    const int8_t* score_tensor = reinterpret_cast<const int8_t*>(outputs[score_idx].data);
    const int8_t* sum_tensor = nullptr;
    if (per_branch == 3) {
      sum_tensor = reinterpret_cast<const int8_t*>(outputs[box_idx + 2].data);
    }

    const int32_t box_zp = attrs[box_idx].zp;
    const float box_scale = attrs[box_idx].scale;
    const int32_t score_zp = attrs[score_idx].zp;
    const float score_scale = attrs[score_idx].scale;
    const int8_t score_thres = quant_i8(conf_thres, score_zp, score_scale);
    int8_t sum_thres = 0;
    if (sum_tensor) {
      sum_thres = quant_i8(conf_thres, attrs[box_idx + 2].zp, attrs[box_idx + 2].scale);
    }

    std::vector<float> before_dfl(static_cast<size_t>(dfl_len) * 4);
    for (int i = 0; i < grid_h; ++i) {
      for (int j = 0; j < grid_w; ++j) {
        const int cell = i * grid_w + j;
        if (sum_tensor && sum_tensor[cell] < sum_thres) {
          continue;
        }
        int8_t max_score = static_cast<int8_t>(-score_zp);
        int max_class = -1;
        int offset = cell;
        for (int c = 0; c < class_num; ++c) {
          const int8_t s = score_tensor[offset];
          if (s > score_thres && s > max_score) {
            max_score = s;
            max_class = c;
          }
          offset += grid_len;
        }
        if (max_class < 0 || max_score <= score_thres) {
          continue;
        }
        offset = cell;
        for (int k = 0; k < dfl_len * 4; ++k) {
          before_dfl[k] = dequant(box_tensor[offset], box_zp, box_scale);
          offset += grid_len;
        }
        float box[4];
        compute_dfl(before_dfl.data(), dfl_len, box);
        const float x1 = (-box[0] + j + 0.5f) * stride;
        const float y1 = (-box[1] + i + 0.5f) * stride;
        const float x2 = (box[2] + j + 0.5f) * stride;
        const float y2 = (box[3] + i + 0.5f) * stride;
        boxes.push_back(x1);
        boxes.push_back(y1);
        boxes.push_back(x2 - x1);
        boxes.push_back(y2 - y1);
        scores.push_back(dequant(max_score, score_zp, score_scale));
        class_ids.push_back(max_class);
        ++valid;
      }
    }
  }
  return valid;
}

int YoloDetector::decode_float(const std::vector<cv::Mat>& outputs, std::vector<float>& boxes,
                               std::vector<float>& scores, std::vector<int>& class_ids, float conf_thres) {
  const auto& attrs = engine_.output_attrs();
  if (attrs.empty() || outputs.size() < 2) {
    return 0;
  }
  const int branches = 3;
  const int per_branch = static_cast<int>(outputs.size()) / branches;
  if (per_branch < 2) {
    return 0;
  }
  const int dfl_len = static_cast<int>(attrs[0].dims[1]) / 4;
  const int class_num = static_cast<int>(attrs[1].dims[1]);
  if (dfl_len <= 0 || class_num <= 0 || class_num > 1024) {
    return 0;
  }
  int valid = 0;

  for (int b = 0; b < branches; ++b) {
    const int box_idx = b * per_branch;
    const int score_idx = box_idx + 1;
    const int grid_h = static_cast<int>(attrs[box_idx].dims[2]);
    const int grid_w = static_cast<int>(attrs[box_idx].dims[3]);
    const int grid_len = grid_h * grid_w;
    const int stride = engine_.model_height() / grid_h;

    const float* box_tensor = reinterpret_cast<const float*>(outputs[box_idx].data);
    const float* score_tensor = reinterpret_cast<const float*>(outputs[score_idx].data);
    const float* sum_tensor = nullptr;
    if (per_branch == 3) {
      sum_tensor = reinterpret_cast<const float*>(outputs[box_idx + 2].data);
    }

    std::vector<float> before_dfl(static_cast<size_t>(dfl_len) * 4);
    for (int i = 0; i < grid_h; ++i) {
      for (int j = 0; j < grid_w; ++j) {
        const int cell = i * grid_w + j;
        if (sum_tensor && sum_tensor[cell] < conf_thres) {
          continue;
        }
        float max_score = -1.f;
        int max_class = -1;
        int offset = cell;
        for (int c = 0; c < class_num; ++c) {
          const float s = score_tensor[offset];
          if (s > conf_thres && s > max_score) {
            max_score = s;
            max_class = c;
          }
          offset += grid_len;
        }
        if (max_class < 0) {
          continue;
        }
        offset = cell;
        for (int k = 0; k < dfl_len * 4; ++k) {
          before_dfl[k] = box_tensor[offset];
          offset += grid_len;
        }
        float box[4];
        compute_dfl(before_dfl.data(), dfl_len, box);
        const float x1 = (-box[0] + j + 0.5f) * stride;
        const float y1 = (-box[1] + i + 0.5f) * stride;
        const float x2 = (box[2] + j + 0.5f) * stride;
        const float y2 = (box[3] + i + 0.5f) * stride;
        boxes.push_back(x1);
        boxes.push_back(y1);
        boxes.push_back(x2 - x1);
        boxes.push_back(y2 - y1);
        scores.push_back(max_score);
        class_ids.push_back(max_class);
        ++valid;
      }
    }
  }
  return valid;
}

int YoloDetector::nms_filter(std::vector<float>& boxes, std::vector<float>& scores, std::vector<int>& class_ids,
                             float nms_thres) {
  const int count = static_cast<int>(scores.size());
  std::vector<int> order(count);
  for (int i = 0; i < count; ++i) {
    order[i] = i;
  }
  std::stable_sort(order.begin(), order.end(),
                   [&scores](int a, int b) { return scores[a] > scores[b]; });

  std::vector<char> removed(count, 0);
  std::vector<int> keep;
  for (int i = 0; i < count; ++i) {
    const int idx = order[i];
    if (removed[idx]) {
      continue;
    }
    keep.push_back(idx);
    for (int j = i + 1; j < count; ++j) {
      const int other = order[j];
      if (removed[other] || class_ids[other] != class_ids[idx]) {
        continue;
      }
      if (intersection_over_union(&boxes[idx * 4], &boxes[other * 4]) > nms_thres) {
        removed[other] = 1;
      }
    }
  }

  std::vector<float> out_boxes;
  std::vector<float> out_scores;
  std::vector<int> out_classes;
  out_boxes.reserve(keep.size() * 4);
  for (int idx : keep) {
    out_boxes.insert(out_boxes.end(), boxes.begin() + idx * 4, boxes.begin() + idx * 4 + 4);
    out_scores.push_back(scores[idx]);
    out_classes.push_back(class_ids[idx]);
  }
  boxes.swap(out_boxes);
  scores.swap(out_scores);
  class_ids.swap(out_classes);
  return static_cast<int>(scores.size());
}

bool YoloDetector::detect(const cv::Mat& bgr, std::vector<Detection>& out, std::string* err) {
  out.clear();
  if (!engine_.loaded()) {
    if (err) {
      *err = "检测模型未加载";
    }
    return false;
  }
  if (bgr.empty() || bgr.type() != CV_8UC3) {
    if (err) {
      *err = "输入图像无效";
    }
    return false;
  }

  const double t0 = now_ms();
  std::vector<uint8_t> rgb;
  std::vector<float> pads;
  preprocess(bgr, rgb, pads);
  const double t1 = now_ms();

  std::vector<cv::Mat> outputs;
  if (!engine_.run(rgb.data(), rgb.size(), outputs, err)) {
    return false;
  }
  const double t2 = now_ms();

  std::vector<float> boxes;
  std::vector<float> scores;
  std::vector<int> class_ids;
  if (engine_.quantized()) {
    decode_quant(outputs, boxes, scores, class_ids, conf_);
  } else {
    decode_float(outputs, boxes, scores, class_ids, conf_);
  }
  nms_filter(boxes, scores, class_ids, nms_);

  const int src_w = bgr.cols;
  const int src_h = bgr.rows;
  const int count = std::min(static_cast<int>(scores.size()), max_detections_);
  for (int i = 0; i < count; ++i) {
    float x1 = (boxes[i * 4 + 0] - letterbox_.pad_x) / letterbox_.scale;
    float y1 = (boxes[i * 4 + 1] - letterbox_.pad_y) / letterbox_.scale;
    float x2 = x1 + boxes[i * 4 + 2] / letterbox_.scale;
    float y2 = y1 + boxes[i * 4 + 3] / letterbox_.scale;
    x1 = std::max(0.f, std::min(x1, static_cast<float>(src_w - 1)));
    y1 = std::max(0.f, std::min(y1, static_cast<float>(src_h - 1)));
    x2 = std::max(0.f, std::min(x2, static_cast<float>(src_w - 1)));
    y2 = std::max(0.f, std::min(y2, static_cast<float>(src_h - 1)));
    if (x2 - x1 < 2.f || y2 - y1 < 2.f) {
      continue;
    }
    Detection det;
    det.cls_id = class_ids[i];
    det.label = label(class_ids[i]);
    det.score = scores[i];
    det.box = cv::Rect2f(x1, y1, x2 - x1, y2 - y1);
    out.push_back(det);
  }
  const double t3 = now_ms();

  preprocess_ms_ = t1 - t0;
  infer_ms_ = t2 - t1;
  postprocess_ms_ = t3 - t2;
  return true;
}

}  // namespace eye
