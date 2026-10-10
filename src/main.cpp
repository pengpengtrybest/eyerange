#include <opencv2/imgcodecs.hpp>

#include <atomic>
#include <chrono>
#include <csignal>
#include <cstdio>
#include <cstring>
#include <execinfo.h>
#include <fstream>
#include <iostream>
#include <string>
#include <thread>
#include <unistd.h>

#include "app/pipeline.hpp"
#include "common/config.hpp"
#include "common/log.hpp"
#include "common/stop_flag.hpp"

#if EYE_HAVE_QT
#include "ui/main_window.hpp"
#endif

namespace {

std::atomic<bool> g_stop{false};

void on_signal(int) { g_stop.store(true); }

// 崩溃时打印调用栈（配合 -rdynamic）
void on_crash(int sig) {
  void* frames[64];
  const int n = backtrace(frames, 64);
  char header[128];
  std::snprintf(header, sizeof(header), "\n=== 捕获信号 %d，调用栈 ===\n", sig);
  const ssize_t ignored = ::write(2, header, std::strlen(header));
  (void)ignored;
  backtrace_symbols_fd(frames, n, 2);
  ::signal(sig, SIG_DFL);
  ::raise(sig);
}

void print_usage() {
  std::printf(
      "eye_range - 双目 YOLO 识别 + 立体测距 (RK3588)\n"
      "\n"
      "用法: eye_range [选项]\n"
      "  --config <yaml>        配置文件（默认 config/eye_range.yaml）\n"
      "  --device </dev/videoN> 采集设备\n"
      "  --size <WxH>           左右拼接总分辨率，如 2560x960\n"
      "  --fps <n>              采集帧率\n"
      "  --swap-eyes            交换左右目\n"
      "  --model <rknn>         RKNN 模型路径\n"
      "  --labels <txt>         类别标签文件\n"
      "  --calib <yaml>         双目标定文件\n"
      "  --conf <f>             置信度阈值\n"
      "  --nms <f>              NMS 阈值\n"
      "  --npu-core <auto|0|1|2|all>  NPU 核绑定\n"
      "  --roi <0|1>            只算目标附近的视差（默认 1）\n"
      "  --stereo-threads <n>   测距并行线程数\n"
      "  --image-pair <l> <r>   离线图片对（自测，不打开相机）\n"
      "  --borderless           界面铺满屏幕（无边框窗口，默认，最稳）\n"
      "  --fullscreen           界面用真正的 EWMH 全屏（板子开着合成器时可能死机）\n"
      "  --windowed             界面窗口模式\n"
      "  --low-power            低功耗预设（NPU 单核/2 线程/15fps 采集/12fps 界面）\n"
      "  --headless             无界面模式\n"
      "  --duration <sec>       运行时长（headless，0=一直跑）\n"
      "  --out-dir <dir>        headless 输出目录\n"
      "  --save-interval <sec>  截图间隔（默认 2s）\n"
      "  --log-level <debug|info|warn|error>\n"
      "  --help\n");
}

bool parse_args(int argc, char** argv, eye::AppConfig& cfg, bool& headless, double& duration,
                std::string& out_dir, double& save_interval, std::string& pair_left, std::string& pair_right,
                std::string& config_path) {
  for (int i = 1; i < argc; ++i) {
    const std::string arg = argv[i];
    auto next = [&](const char* name) -> std::string {
      if (i + 1 >= argc) {
        std::fprintf(stderr, "缺少参数: %s\n", name);
        std::exit(2);
      }
      return argv[++i];
    };
    if (arg == "--config") {
      config_path = next("--config");
    } else if (arg == "--device") {
      cfg.capture.device = next("--device");
    } else if (arg == "--size") {
      const std::string size = next("--size");
      const auto pos = size.find('x');
      if (pos == std::string::npos) {
        std::fprintf(stderr, "分辨率格式应为 WxH\n");
        return false;
      }
      cfg.capture.width = std::atoi(size.substr(0, pos).c_str());
      cfg.capture.height = std::atoi(size.substr(pos + 1).c_str());
    } else if (arg == "--fps") {
      cfg.capture.fps = std::atoi(next("--fps").c_str());
    } else if (arg == "--swap-eyes") {
      cfg.capture.swap_eyes = true;
    } else if (arg == "--model") {
      cfg.detect.model = next("--model");
    } else if (arg == "--labels") {
      cfg.detect.labels = next("--labels");
    } else if (arg == "--calib") {
      cfg.stereo.calib = next("--calib");
    } else if (arg == "--conf") {
      cfg.detect.conf = std::atof(next("--conf").c_str());
    } else if (arg == "--nms") {
      cfg.detect.nms = std::atof(next("--nms").c_str());
    } else if (arg == "--npu-core") {
      const std::string v = next("--npu-core");
      if (v == "auto") {
        cfg.detect.npu_core = -1;
      } else if (v == "all") {
        cfg.detect.npu_core = -2;
      } else {
        cfg.detect.npu_core = std::atoi(v.c_str());
      }
    } else if (arg == "--roi") {
      cfg.stereo.roi_only = std::atoi(next("--roi").c_str()) != 0;
    } else if (arg == "--stereo-threads") {
      cfg.stereo.threads = std::atoi(next("--stereo-threads").c_str());
    } else if (arg == "--image-pair") {
      pair_left = next("--image-pair");
      pair_right = next("--image-pair");
    } else if (arg == "--fullscreen") {
      cfg.ui.fullscreen = true;
      cfg.ui.borderless = false;
    } else if (arg == "--borderless") {
      cfg.ui.borderless = true;
      cfg.ui.fullscreen = false;
    } else if (arg == "--windowed") {
      cfg.ui.fullscreen = false;
      cfg.ui.borderless = false;
    } else if (arg == "--low-power") {
      // 板上供电/散热余量有限时使用：牺牲一部分帧率换稳定
      cfg.detect.npu_core = 0;
      cfg.capture.fps = 15;
      cfg.stereo.threads = 2;
      cfg.stereo.display_scale = 0.2f;
      cfg.stereo.display_num_disparities = 48;
      cfg.ui.fps = 12;
    } else if (arg == "--headless") {
      headless = true;
    } else if (arg == "--duration") {
      duration = std::atof(next("--duration").c_str());
    } else if (arg == "--out-dir") {
      out_dir = next("--out-dir");
    } else if (arg == "--save-interval") {
      save_interval = std::atof(next("--save-interval").c_str());
    } else if (arg == "--log-level") {
      const std::string v = next("--log-level");
      if (v == "debug") {
        eye::set_log_level(eye::LogLevel::kDebug);
      } else if (v == "warn") {
        eye::set_log_level(eye::LogLevel::kWarn);
      } else if (v == "error") {
        eye::set_log_level(eye::LogLevel::kError);
      } else {
        eye::set_log_level(eye::LogLevel::kInfo);
      }
    } else if (arg == "--help" || arg == "-h") {
      print_usage();
      std::exit(0);
    } else {
      std::fprintf(stderr, "未知参数: %s\n", arg.c_str());
      print_usage();
      return false;
    }
  }
  (void)config_path;
  return true;
}

int run_headless(eye::Pipeline& pipeline, double duration, const std::string& out_dir, double save_interval) {
  using clock = std::chrono::steady_clock;
  const auto start = clock::now();
  double last_save = -1e9;
  double last_print = -1e9;
  int saved = 0;
  bool warned_uncalibrated = false;
  std::string summary;

  while (!g_stop.load()) {
    std::this_thread::sleep_for(std::chrono::milliseconds(200));
    const double elapsed = std::chrono::duration<double>(clock::now() - start).count();
    if (duration > 0 && elapsed >= duration) {
      break;
    }
    const eye::PipelineStats st = pipeline.stats();
    const auto frame = pipeline.latest();
    if (!warned_uncalibrated && pipeline.baseline_mm() > 0) {
      if (!pipeline.calibrated()) {
        std::printf("警告: 未加载标定文件，距离为估算值（baseline=%.1fmm, fx=%.1fpx）\n", pipeline.baseline_mm(),
                    pipeline.fx_px());
      }
      warned_uncalibrated = true;
    }
    if (frame && elapsed - last_print >= 1.0) {
      last_print = elapsed;
      std::printf(
          "[%.1fs] 采集 %.1ffps | 识别 %.1ffps(%.1fms) | 测距 %.1ffps(%.1fms) | 端到端 %.0fms | 目标 %zu | "
          "NPU %s\n",
          elapsed, st.capture_fps, st.detect_fps, st.infer_ms, st.stereo_fps, st.stereo_ms, st.pipeline_ms,
          frame->detections.size(), eye::npu_core_name(st.npu_core));
      for (const eye::Detection& det : frame->detections) {
        std::printf("        - %-12s %.2f  disp=%6.2fpx  dist=%s  (pts=%d%s)\n", det.label.c_str(), det.score,
                    det.disparity_px,
                    det.distance_m > 0.f ? (std::to_string(det.distance_m) + "m").c_str() : "--",
                    det.valid_pixels, det.note.empty() ? "" : (" " + det.note).c_str());
      }
    }

    if (out_dir.empty()) {
      continue;
    }
    if (elapsed - last_save < save_interval) {
      continue;
    }
    last_save = elapsed;
    if (frame && !frame->annotated_bgr.empty()) {
      char name[512];
      std::snprintf(name, sizeof(name), "%s/frame_%03d.png", out_dir.c_str(), saved);
      cv::imwrite(name, frame->annotated_bgr);
      if (frame->has_disparity && !frame->disparity_vis.empty()) {
        std::snprintf(name, sizeof(name), "%s/disparity_%03d.png", out_dir.c_str(), saved);
        cv::imwrite(name, frame->disparity_vis);
      }
      char line[512];
      std::snprintf(line, sizeof(line),
                    "{\"t\": %.2f, \"capture_fps\": %.2f, \"detect_fps\": %.2f, \"stereo_fps\": %.2f, "
                    "\"infer_ms\": %.2f, \"stereo_ms\": %.2f, \"pipeline_ms\": %.2f, \"detections\": %zu}\n",
                    elapsed, st.capture_fps, st.detect_fps, st.stereo_fps, st.infer_ms, st.stereo_ms, st.pipeline_ms,
                    frame->detections.size());
      summary += line;
      ++saved;
    }
  }

  if (!out_dir.empty()) {
    std::ofstream ofs(out_dir + "/stats.jsonl");
    ofs << summary;
    std::printf("已保存 %d 组截图/统计数据到 %s\n", saved, out_dir.c_str());
  }
  return 0;
}

}  // namespace

namespace eye {
std::atomic<bool>& stop_flag() { return g_stop; }
}  // namespace eye

int main(int argc, char** argv) {
  std::signal(SIGINT, on_signal);
  std::signal(SIGTERM, on_signal);
  std::signal(SIGSEGV, on_crash);
  std::signal(SIGABRT, on_crash);
  std::signal(SIGBUS, on_crash);
  std::signal(SIGFPE, on_crash);

  std::string config_path = "config/eye_range.yaml";
  bool headless = false;
  double duration = 0.0;
  std::string out_dir;
  double save_interval = 2.0;
  std::string pair_left;
  std::string pair_right;

  eye::AppConfig cfg;
  // 先读配置文件，再让命令行覆盖
  for (int i = 1; i < argc; ++i) {
    if (std::strcmp(argv[i], "--config") == 0 && i + 1 < argc) {
      config_path = argv[i + 1];
    }
  }
  cfg = eye::AppConfig::load(config_path);
  if (!parse_args(argc, argv, cfg, headless, duration, out_dir, save_interval, pair_left, pair_right, config_path)) {
    return 2;
  }

  eye::Pipeline pipeline(cfg);
  std::string err;
  const bool ok = !pair_left.empty() ? pipeline.start_image_pair(pair_left, pair_right, &err)
                                     : pipeline.start(&err);
  if (!ok) {
    std::fprintf(stderr, "启动失败: %s\n", err.c_str());
    return 1;
  }

  int rc = 0;
#if EYE_HAVE_QT
  if (!headless) {
    rc = eye::run_ui(pipeline, cfg);
  } else {
    rc = run_headless(pipeline, duration, out_dir, save_interval);
  }
#else
  if (!headless) {
    std::printf("本二进制未编译 Qt 界面，切换到 headless 模式\n");
  }
  rc = run_headless(pipeline, duration, out_dir, save_interval);
#endif
  pipeline.stop();
  return rc;
}
