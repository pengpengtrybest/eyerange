#include "app/pipeline.hpp"

#include <opencv2/imgproc.hpp>
#include <opencv2/core/ocl.hpp>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>

#include "common/log.hpp"

namespace eye {
namespace {

double now_seconds() {
  return std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count();
}

long long now_ms() {
  return std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch())
      .count();
}

cv::Scalar class_color(int cls_id) {
  static const cv::Scalar palette[] = {
      {0, 200, 255}, {0, 255, 127}, {255, 160, 0}, {255, 0, 128}, {128, 255, 0},
      {255, 255, 0}, {0, 128, 255}, {200, 0, 255}, {0, 255, 255}, {255, 80, 80},
  };
  const int n = static_cast<int>(sizeof(palette) / sizeof(palette[0]));
  return palette[((cls_id % n) + n) % n];
}

void draw_text_bg(cv::Mat& img, const std::string& text, const cv::Point& origin, const cv::Scalar& color,
                  double scale) {
  int baseline = 0;
  const cv::Size size = cv::getTextSize(text, cv::FONT_HERSHEY_SIMPLEX, scale, 1, &baseline);
  cv::rectangle(img, cv::Rect(origin.x, origin.y - size.height - 4, size.width + 8, size.height + baseline + 6),
                color, cv::FILLED);
  cv::putText(img, text, cv::Point(origin.x + 4, origin.y), cv::FONT_HERSHEY_SIMPLEX, scale, cv::Scalar(0, 0, 0), 1,
              cv::LINE_AA);
}

}  // namespace

Pipeline::Pipeline(const AppConfig& cfg) : cfg_(cfg), filter_() {
  conf_.store(cfg.detect.conf);
  nms_.store(cfg.detect.nms);
  npu_core_.store(cfg.detect.npu_core);
}

Pipeline::~Pipeline() { stop(); }

bool Pipeline::start(std::string* err) {
  // 只让我们的线程池并行（OpenCV 内部再并行会与线程池抢核，反而更慢）
  cv::setNumThreads(std::max(1, cfg_.stereo.opencv_threads));
  // 板上 Mali 的 OpenCL 内核编译会失败（只刷错误日志），直接关掉
  cv::ocl::setUseOpenCL(false);
  calib_ = CalibData::load(cfg_.stereo.calib);
  if (calib_.valid) {
    EYE_LOG_INFO("已加载标定: " + cfg_.stereo.calib + " baseline=" + std::to_string(calib_.baseline_mm) +
                 "mm fx=" + std::to_string(calib_.fx));
  }

  std::string local_err;
  if (!source_.open(cfg_.capture, &local_err)) {
    if (err != nullptr) {
      *err = local_err;
    }
    return false;
  }
  if (!detector_.load(cfg_.detect, &local_err)) {
    if (err != nullptr) {
      *err = local_err;
    }
    return false;
  }
  std::string stereo_note;
  depth_.configure(cfg_.stereo, calib_, cv::Size(source_.eye_width(), source_.eye_height()), &stereo_note);

  {
    std::lock_guard<std::mutex> lock(text_mutex_);
    source_description_ = source_.description();
    model_description_ = cfg_.detect.model;
  }
  return start_threads(err);
}

bool Pipeline::start_image_pair(const std::string& left, const std::string& right, std::string* err) {
  cv::setNumThreads(std::max(1, cfg_.stereo.opencv_threads));
  calib_ = CalibData::load(cfg_.stereo.calib);
  std::string local_err;
  if (!source_.open_image_pair(left, right, &local_err)) {
    if (err != nullptr) {
      *err = local_err;
    }
    return false;
  }
  if (!detector_.load(cfg_.detect, &local_err)) {
    if (err != nullptr) {
      *err = local_err;
    }
    return false;
  }
  depth_.configure(cfg_.stereo, calib_, cv::Size(source_.eye_width(), source_.eye_height()), &local_err);
  {
    std::lock_guard<std::mutex> lock(text_mutex_);
    source_description_ = source_.description();
    model_description_ = cfg_.detect.model;
  }
  return start_threads(err);
}

bool Pipeline::start_threads(std::string* err) {
  frame_slot_.reset();
  pending_.reset();
  running_.store(true);
  capture_thread_ = std::thread([this] { capture_loop(); });
  detect_thread_ = std::thread([this] { detect_loop(); });
  stereo_thread_ = std::thread([this] { stereo_loop(); });
  EYE_LOG_INFO("流水线已启动");
  (void)err;
  return true;
}

void Pipeline::stop() {
  if (!running_.exchange(false)) {
    return;
  }
  frame_slot_.shutdown();
  pending_.shutdown();
  if (capture_thread_.joinable()) {
    capture_thread_.join();
  }
  if (detect_thread_.joinable()) {
    detect_thread_.join();
  }
  if (stereo_thread_.joinable()) {
    stereo_thread_.join();
  }
  source_.close();
  EYE_LOG_INFO("流水线已停止");
}

void Pipeline::capture_loop() {
  long long seq = 0;
  while (running_.load()) {
    StereoPair pair;
    if (!source_.grab(pair, 200)) {
      continue;
    }
    FrameBundle bundle;
    bundle.left = pair.left.clone();
    bundle.right = pair.right.clone();
    bundle.timestamp = pair.timestamp;
    bundle.seq = ++seq;
    frame_slot_.set(std::move(bundle));

    ++capture_win_count_;
    captured_.fetch_add(1);
    const long long now = now_ms();
    if (capture_win_start_ == 0) {
      capture_win_start_ = now;
    } else if (now - capture_win_start_ >= 1000) {
      capture_fps_.store(capture_win_count_ * 1000.0 / static_cast<double>(now - capture_win_start_));
      capture_win_count_ = 0;
      capture_win_start_ = now;
    }
    if (source_.file_mode()) {
      // 离线图片对：限制到约 20fps，方便观察
      std::this_thread::sleep_for(std::chrono::milliseconds(45));
    }
  }
}

void Pipeline::detect_loop() {
  long long last_seq = 0;
  while (running_.load()) {
    FrameBundle frame;
    if (!frame_slot_.wait_take(frame, last_seq, 200)) {
      continue;
    }
    if (reload_model_.exchange(false)) {
      DetectConfig dcfg = cfg_.detect;
      dcfg.npu_core = npu_core_.load();
      std::string load_err;
      if (detector_.load(dcfg, &load_err)) {
        EYE_LOG_INFO("模型已按新的 NPU 核绑定重新加载: " + std::string(npu_core_name(npu_core_.load())));
      } else {
        std::lock_guard<std::mutex> lock(text_mutex_);
        last_error_ = load_err;
        EYE_LOG_ERROR("重新加载模型失败: " + load_err);
      }
    }
    detector_.set_thresholds(conf_.load(), nms_.load());

    std::vector<Detection> dets;
    std::string err;
    if (!detector_.detect(frame.left, dets, &err)) {
      std::lock_guard<std::mutex> lock(text_mutex_);
      last_error_ = err;
      EYE_LOG_WARN("识别失败: " + err);
      continue;
    }

    DetectBundle bundle;
    bundle.left = frame.left;
    bundle.right = frame.right;
    bundle.detections = std::move(dets);
    bundle.timestamp = frame.timestamp;
    bundle.seq = frame.seq;
    pending_.set(std::move(bundle));

    preprocess_ms_.store(detector_.last_preprocess_ms());
    infer_ms_.store(detector_.last_infer_ms());
    postprocess_ms_.store(detector_.last_postprocess_ms());
    last_npu_core_.store(detector_.npu_core());
    detected_.fetch_add(1);
    ++detect_win_count_;
    const long long now = now_ms();
    if (detect_win_start_ == 0) {
      detect_win_start_ = now;
    } else if (now - detect_win_start_ >= 1000) {
      detect_fps_.store(detect_win_count_ * 1000.0 / static_cast<double>(now - detect_win_start_));
      detect_win_count_ = 0;
      detect_win_start_ = now;
    }
  }
}

void Pipeline::stereo_loop() {
  long long last_seq = 0;
  while (running_.load()) {
    DetectBundle bundle;
    if (!pending_.wait_take(bundle, last_seq, 200)) {
      continue;
    }

    std::vector<Detection> dets = bundle.detections;
    for (Detection& det : dets) {
      if (det.score < conf_.load()) {
        det.note = "低置信度";
      }
    }
    depth_.measure_boxes(bundle.left, bundle.right, dets);
    filter_.update(dets, bundle.timestamp);

    auto frame = std::make_shared<ResultFrame>();
    frame->left_bgr = bundle.left;
    frame->right_bgr = bundle.right;
    frame->annotated_bgr = bundle.left.clone();
    frame->detections = dets;
    frame->timestamp = bundle.timestamp;
    publish(std::move(*frame));

    measured_.fetch_add(1);
    stereo_ms_.store(depth_.last_measure_ms());
    ++stereo_win_count_;
    const long long now = now_ms();
    if (stereo_win_start_ == 0) {
      stereo_win_start_ = now;
    } else if (now - stereo_win_start_ >= 1000) {
      stereo_fps_.store(stereo_win_count_ * 1000.0 / static_cast<double>(now - stereo_win_start_));
      stereo_win_count_ = 0;
      stereo_win_start_ = now;
    }
  }
}

void Pipeline::publish(ResultFrame&& frame) {
  // 显示用的视差图按节流间隔更新，节省 CPU
  const bool need_display = (block_counter_++ % std::max(1, display_interval_.load())) == 0;
  auto previous = latest();
  if (need_display) {
    cv::Mat disp_vis;
    cv::Mat depth_vis;
    if (depth_.compute_display(frame.left_bgr, frame.right_bgr, disp_vis, depth_vis)) {
      frame.disparity_vis = disp_vis;
      frame.depth_vis = depth_vis;
      frame.has_disparity = true;
    }
  } else if (previous && previous->has_disparity) {
    frame.disparity_vis = previous->disparity_vis;
    frame.depth_vis = previous->depth_vis;
    frame.has_disparity = true;
  }

  annotate(frame);
  pipeline_ms_.store((now_seconds() - frame.timestamp) * 1000.0);

  auto shared = std::make_shared<const ResultFrame>(std::move(frame));
  {
    std::lock_guard<std::mutex> lock(result_mutex_);
    result_ = shared;
  }
}

void Pipeline::annotate(ResultFrame& frame) const {
  if (frame.annotated_bgr.empty()) {
    return;
  }
  cv::Mat& img = frame.annotated_bgr;
  for (const Detection& det : frame.detections) {
    const cv::Rect box(static_cast<int>(det.box.x), static_cast<int>(det.box.y),
                       std::max(1, static_cast<int>(det.box.width)), std::max(1, static_cast<int>(det.box.height)));
    const cv::Scalar color = class_color(det.cls_id);
    cv::rectangle(img, box, color, 2);

    char text[160];
    if (det.distance_m > 0.f) {
      std::snprintf(text, sizeof(text), "%s %.2f %.2fm #%d", det.label.c_str(), det.score, det.distance_m,
                    det.track_id);
    } else {
      std::snprintf(text, sizeof(text), "%s %.2f --m #%d", det.label.c_str(), det.score, det.track_id);
    }
    const int y = std::max(18, box.y);
    draw_text_bg(img, text, cv::Point(box.x, y), color, 0.5);
  }

  // HUD
  char hud[256];
  const char* tag = depth_.calibrated() ? "" : " [未标定/估算]";
  std::snprintf(hud, sizeof(hud), "cap %.1f | det %.1f | stereo %.1f fps | infer %.1fms | stereo %.1fms%s",
                capture_fps_.load(), detect_fps_.load(), stereo_fps_.load(), infer_ms_.load(), stereo_ms_.load(), tag);
  draw_text_bg(img, hud, cv::Point(8, 24), cv::Scalar(40, 40, 40), 0.55);
}

std::shared_ptr<const ResultFrame> Pipeline::latest() const {
  std::lock_guard<std::mutex> lock(result_mutex_);
  return result_;
}

PipelineStats Pipeline::stats() const {
  PipelineStats s;
  s.capture_fps = capture_fps_.load();
  s.detect_fps = detect_fps_.load();
  s.stereo_fps = stereo_fps_.load();
  s.display_fps = display_fps_.load();
  s.preprocess_ms = preprocess_ms_.load();
  s.infer_ms = infer_ms_.load();
  s.postprocess_ms = postprocess_ms_.load();
  s.stereo_ms = stereo_ms_.load();
  s.pipeline_ms = pipeline_ms_.load();
  s.npu_core = last_npu_core_.load();
  s.frames_captured = captured_.load();
  s.frames_detected = detected_.load();
  s.frames_stereo = measured_.load();
  return s;
}

void Pipeline::report_display_fps(double fps) { display_fps_.store(fps); }

void Pipeline::set_conf(float value) { conf_.store(std::max(0.01f, std::min(0.99f, value))); }

void Pipeline::set_nms(float value) { nms_.store(std::max(0.01f, std::min(0.99f, value))); }

void Pipeline::set_npu_core(int core) {
  if (npu_core_.load() == core) {
    return;
  }
  npu_core_.store(core);
  reload_model_.store(true);
}

void Pipeline::set_stereo_params(const StereoConfig& cfg) { depth_.set_params(cfg); }

StereoConfig Pipeline::stereo_params() const { return depth_.params(); }

std::string Pipeline::status_text() const {
  std::lock_guard<std::mutex> lock(text_mutex_);
  return status_;
}

std::string Pipeline::source_description() const {
  std::lock_guard<std::mutex> lock(text_mutex_);
  return source_description_;
}

std::string Pipeline::last_error() const {
  std::lock_guard<std::mutex> lock(text_mutex_);
  return last_error_;
}

}  // namespace eye
