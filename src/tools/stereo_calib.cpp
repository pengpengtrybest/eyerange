// 双目标定工具：
//   采集模式：把棋盘格放好，程序自动识别到棋盘就存一对图
//   标定模式：用存下来的图像对做标定，输出 config/calib.yaml
#include <opencv2/calib3d.hpp>
#include <opencv2/core/ocl.hpp>
#include <opencv2/imgcodecs.hpp>
#include <opencv2/imgproc.hpp>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <string>
#include <sys/stat.h>
#include <thread>
#include <vector>
#include <vector>

#include "capture/stereo_source.hpp"
#include "common/config.hpp"
#include "common/log.hpp"

#if EYE_HAVE_QT
#include "tools/calib_window.hpp"
#endif

namespace {

std::atomic<bool> g_stop{false};
void on_signal(int) { g_stop.store(true); }

struct Options {
  std::string mode;
  std::string device = "/dev/video0";
  std::string dir = "calib_images";
  std::string out = "config/calib.yaml";
  int width = 2560;
  int height = 960;
  int fps = 30;
  int cols = 9;    // 内角点列数
  int rows = 6;    // 内角点行数
  double square_mm = 25.0;
  int count = 20;      // 采集对数
  double interval = 1.3;
  bool swap_eyes = false;
  double preview_interval = 3.0;  // 写入预览图的间隔（远程辅助观察用）
  bool gui = false;                // 采集时用取景器界面
};

void usage() {
  std::printf(
      "stereo_calib - 双目标定\n"
      "\n"
      "采集:\n"
      "  eye_range_calib capture [--out-dir calib_images] [--device /dev/video0]\n"
      "                         [--size 2560x960] [--cols 9] [--rows 6] [--square 25]\n"
      "                         [--count 20] [--interval 1.3] [--swap-eyes]\n"
      "                         [--preview-interval 3] [--gui]\n"
      "                         --gui 会在板子显示器上全屏显示取景器（推荐）\n"
      "\n"
      "自动识别棋盘格规格（不知道是几乘几时先跑这个）:\n"
      "  eye_range_calib probe [--out-dir calib_images] [--device /dev/video0]\n"
      "                        [--size 2560x960] [--swap-eyes]\n"
      "\n"
      "标定:\n"
      "  eye_range_calib calibrate [--dir calib_images] [--out config/calib.yaml]\n"
      "                            [--cols 9] [--rows 6] [--square 25]\n");
}

bool parse(int argc, char** argv, Options& opt) {
  if (argc < 2) {
    usage();
    return false;
  }
  opt.mode = argv[1];
  for (int i = 2; i < argc; ++i) {
    const std::string arg = argv[i];
    auto next = [&]() -> std::string { return (i + 1 < argc) ? argv[++i] : std::string(); };
    if (arg == "--device") {
      opt.device = next();
    } else if (arg == "--out-dir" || arg == "--dir") {
      opt.dir = next();
    } else if (arg == "--out") {
      opt.out = next();
    } else if (arg == "--size") {
      const std::string s = next();
      const auto pos = s.find('x');
      if (pos != std::string::npos) {
        opt.width = std::atoi(s.substr(0, pos).c_str());
        opt.height = std::atoi(s.substr(pos + 1).c_str());
      }
    } else if (arg == "--cols") {
      opt.cols = std::atoi(next().c_str());
    } else if (arg == "--rows") {
      opt.rows = std::atoi(next().c_str());
    } else if (arg == "--square") {
      opt.square_mm = std::atof(next().c_str());
    } else if (arg == "--count") {
      opt.count = std::atoi(next().c_str());
    } else if (arg == "--interval") {
      opt.interval = std::atof(next().c_str());
    } else if (arg == "--swap-eyes") {
      opt.swap_eyes = true;
    } else if (arg == "--preview-interval") {
      opt.preview_interval = std::atof(next().c_str());
    } else if (arg == "--gui") {
      opt.gui = true;
    } else {
      std::fprintf(stderr, "未知参数: %s\n", arg.c_str());
      usage();
      return false;
    }
  }
  return true;
}

bool find_corners(const cv::Mat& gray, const cv::Size& pattern, std::vector<cv::Point2f>& corners) {
  corners.clear();
  // 只用 SB 一种检测器：经典算法与 SB 的角点排序可能不一致，
  // 混用会让部分视角的对应关系错乱（实测 RMS 从 0.3px 恶化到 24px）。
  return cv::findChessboardCornersSB(gray, pattern, corners, cv::CALIB_CB_EXHAUSTIVE);
}

// 经典检测器（更快，供取景器实时预览用；标定解算不要用它）
bool find_corners_fast(const cv::Mat& gray, const cv::Size& pattern, std::vector<cv::Point2f>& corners) {
  corners.clear();
  const int flags = cv::CALIB_CB_ADAPTIVE_THRESH | cv::CALIB_CB_NORMALIZE_IMAGE | cv::CALIB_CB_FAST_CHECK;
  return cv::findChessboardCorners(gray, pattern, corners, flags);
}

// 把左右目与检测结果写成一张预览图，便于远程判断现场状况
void write_preview(const std::string& dir, const eye::StereoPair& pair, const cv::Size& pattern,
                   const std::vector<cv::Point2f>& cl, const std::vector<cv::Point2f>& cr, const std::string& note) {
  cv::Mat l = pair.left.clone();
  cv::Mat r = pair.right.clone();
  if (!cl.empty()) {
    cv::drawChessboardCorners(l, pattern, cl, true);
  }
  if (!cr.empty()) {
    cv::drawChessboardCorners(r, pattern, cr, true);
  }
  cv::Mat both;
  cv::hconcat(l, r, both);
  if (both.cols > 1800) {
    cv::resize(both, both, cv::Size(1800, both.rows * 1800 / both.cols));
  }
  cv::putText(both, note, cv::Point(10, 30), cv::FONT_HERSHEY_SIMPLEX, 0.9, cv::Scalar(0, 0, 255), 2);
  cv::imwrite(dir + "/preview.png", both);
  std::ofstream ofs(dir + "/preview.txt");
  ofs << note << "\n";
}

int do_capture(const Options& opt) {
  eye::CaptureConfig ccfg;
  ccfg.device = opt.device;
  ccfg.width = opt.width;
  ccfg.height = opt.height;
  ccfg.fps = opt.fps;
  ccfg.swap_eyes = opt.swap_eyes;
  eye::StereoSource source;
  std::string err;
  if (!source.open(ccfg, &err)) {
    std::fprintf(stderr, "打开相机失败: %s\n", err.c_str());
    return 1;
  }
  ::mkdir(opt.dir.c_str(), 0755);

  const cv::Size pattern(opt.cols, opt.rows);
  std::printf("采集模式: 每目 %dx%d，棋盘内角点 %dx%d，格长 %.1fmm\n", source.eye_width(), source.eye_height(),
              opt.cols, opt.rows, opt.square_mm);
  std::printf("把棋盘放到视野里，识别到就自动保存，目标 %d 对（Ctrl-C 结束）\n", opt.count);

  int saved = 0;
  double last_save = -1e9;
  double last_check = -1e9;
  double last_preview = -1e9;
  const auto start = std::chrono::steady_clock::now();
  while (!g_stop.load() && saved < opt.count) {
    eye::StereoPair pair;
    if (!source.grab(pair, 500)) {
      continue;
    }
    const double now = std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
    if (now - last_check < 0.25) {
      continue;
    }
    last_check = now;
    cv::Mat gl;
    cv::Mat gr;
    cv::cvtColor(pair.left, gl, cv::COLOR_BGR2GRAY);
    cv::cvtColor(pair.right, gr, cv::COLOR_BGR2GRAY);
    std::vector<cv::Point2f> cl;
    std::vector<cv::Point2f> cr;
    const bool ok_left = find_corners_fast(gl, pattern, cl);
    const bool ok_right = ok_left ? find_corners_fast(gr, pattern, cr) : false;

    char note[256];
    float coverage = 0.f;
    if (ok_left) {
      const cv::Rect bb = cv::boundingRect(cl);
      coverage = static_cast<float>(bb.area()) / static_cast<float>(gl.total());
    }
    std::snprintf(note, sizeof(note), "saved=%d/%d left=%s right=%s coverage=%.2f", saved, opt.count,
                  ok_left ? "OK" : "--", ok_right ? "OK" : "--", coverage);

    if (now - last_preview >= opt.preview_interval) {
      last_preview = now;
      write_preview(opt.dir, pair, pattern, cl, cr, note);
      std::ofstream ofs(opt.dir + "/status.txt");
      ofs << note << "\n";
    }

    if (ok_left && ok_right) {
      if (now - last_save >= opt.interval) {
        // 保存前用 SB 再检一次，保证角点质量
        std::vector<cv::Point2f> al;
        std::vector<cv::Point2f> ar;
        if (find_corners(gl, pattern, al) && find_corners(gr, pattern, ar)) {
          char name[512];
          std::snprintf(name, sizeof(name), "%s/left_%02d.png", opt.dir.c_str(), saved);
          cv::imwrite(name, pair.left);
          std::snprintf(name, sizeof(name), "%s/right_%02d.png", opt.dir.c_str(), saved);
          cv::imwrite(name, pair.right);
          ++saved;
          last_save = now;
          std::printf("已保存第 %d/%d 对 (棋盘占比 %.0f%%)\n", saved, opt.count, coverage * 100);
          std::fflush(stdout);
        }
      }
    } else {
      std::printf(".");
      std::fflush(stdout);
    }
  }
  source.close();
  std::printf("采集结束，共 %d 对\n", saved);
  return saved > 0 ? 0 : 1;
}

// 自动判断棋盘格规格：用一批候选内角点数去试，看哪个能稳定检出
int do_probe(const Options& opt) {
  eye::CaptureConfig ccfg;
  ccfg.device = opt.device;
  ccfg.width = opt.width;
  ccfg.height = opt.height;
  ccfg.fps = opt.fps;
  ccfg.swap_eyes = opt.swap_eyes;
  eye::StereoSource source;
  std::string err;
  if (!source.open(ccfg, &err)) {
    std::fprintf(stderr, "打开相机失败: %s\n", err.c_str());
    return 1;
  }
  ::mkdir(opt.dir.c_str(), 0755);

  const std::vector<cv::Size> candidates = {{11, 8}, {9, 6},  {10, 7}, {8, 6},  {7, 5}, {12, 9},
                                            {9, 7},  {8, 7},  {6, 4},  {13, 9}, {7, 6}, {5, 4}};

  eye::StereoPair pair;
  // 丢掉前几帧，等自动曝光稳定
  for (int i = 0; i < 12; ++i) {
    if (!source.grab(pair, 500)) {
      --i;
    }
  }
  double min_v = 0.0;
  double max_v = 0.0;
  cv::Mat gl;
  cv::Mat gr;
  cv::cvtColor(pair.left, gl, cv::COLOR_BGR2GRAY);
  cv::cvtColor(pair.right, gr, cv::COLOR_BGR2GRAY);
  cv::minMaxLoc(gl, &min_v, &max_v);
  const cv::Scalar ml = cv::mean(gl);
  const cv::Scalar mr = cv::mean(gr);
  std::printf("画面统计: 左目 均值 %.0f 范围 %.0f~%.0f | 右目 均值 %.0f\n", ml[0], min_v, max_v, mr[0]);
  if (max_v - min_v < 40) {
    std::printf("提示: 画面几乎全白/全黑，先把棋盘格放进视野再试\n");
  }

  int matched = 0;
  for (const cv::Size& p : candidates) {
    std::vector<cv::Point2f> cl;
    std::vector<cv::Point2f> cr;
    const bool ok_left = find_corners(gl, p, cl);
    const bool ok_right = ok_left ? find_corners(gr, p, cr) : false;
    if (!ok_left && !ok_right) {
      continue;
    }
    float coverage = 0.f;
    if (ok_left) {
      coverage = static_cast<float>(cv::boundingRect(cl).area()) / static_cast<float>(gl.total());
    }
    std::printf("候选 %2dx%-2d 内角点: 左 %s 右 %s  (棋盘占画面 %.0f%%)\n", p.width, p.height,
                ok_left ? "OK" : "--", ok_right ? "OK" : "--", coverage * 100);
    if (ok_left && ok_right) {
      char note[192];
      std::snprintf(note, sizeof(note), "detected %dx%d coverage=%.2f", p.width, p.height, coverage);
      write_preview(opt.dir, pair, p, cl, cr, note);
      ++matched;
    }
  }
  if (matched == 0) {
    write_preview(opt.dir, pair, cv::Size(9, 6), {}, {}, "no chessboard detected");
    std::printf("没有检出任何常见规格的棋盘格。检查：棋盘是否在视野内/是否太斜太远/是否对焦清晰/光照是否均匀\n");
  } else {
    std::printf("检出 %d 个候选规格，请用能同时左右目检出的那一组坐标做标定\n", matched);
  }
  source.close();
  return matched > 0 ? 0 : 1;
}

int do_calibrate(const Options& opt) {
  std::vector<cv::Mat> lefts;
  std::vector<cv::Mat> rights;
  for (int i = 0; i < 200; ++i) {
    char name[512];
    std::snprintf(name, sizeof(name), "%s/left_%02d.png", opt.dir.c_str(), i);
    cv::Mat l = cv::imread(name, cv::IMREAD_COLOR);
    std::snprintf(name, sizeof(name), "%s/right_%02d.png", opt.dir.c_str(), i);
    cv::Mat r = cv::imread(name, cv::IMREAD_COLOR);
    if (l.empty() || r.empty()) {
      continue;
    }
    lefts.push_back(l);
    rights.push_back(r);
  }
  if (lefts.size() < 8) {
    std::fprintf(stderr, "有效图像对太少 (%zu)，至少需要 8 对\n", lefts.size());
    return 1;
  }

  const cv::Size pattern(opt.cols, opt.rows);
  std::vector<std::vector<cv::Point3f>> object_points;
  std::vector<std::vector<cv::Point2f>> img_left;
  std::vector<std::vector<cv::Point2f>> img_right;
  std::vector<cv::Point3f> obj;
  for (int r = 0; r < opt.rows; ++r) {
    for (int c = 0; c < opt.cols; ++c) {
      obj.emplace_back(static_cast<float>(c * opt.square_mm), static_cast<float>(r * opt.square_mm), 0.f);
    }
  }

  const cv::Size image_size = lefts[0].size();
  for (size_t i = 0; i < lefts.size(); ++i) {
    cv::Mat gl;
    cv::Mat gr;
    cv::cvtColor(lefts[i], gl, cv::COLOR_BGR2GRAY);
    cv::cvtColor(rights[i], gr, cv::COLOR_BGR2GRAY);
    std::vector<cv::Point2f> cl;
    std::vector<cv::Point2f> cr;
    if (!find_corners(gl, pattern, cl) || !find_corners(gr, pattern, cr)) {
      std::printf("第 %zu 对未找到棋盘，跳过\n", i);
      continue;
    }
    object_points.push_back(obj);
    img_left.push_back(cl);
    img_right.push_back(cr);
  }
  if (object_points.size() < 8) {
    std::fprintf(stderr, "可用于标定的图像对只有 %zu 对，太少\n", object_points.size());
    return 1;
  }

  // 逐视角误差分析 + 离群剔除：有些视角角点检测不准（模糊/遮挡/顺序异常），
  // 不剔除会把整体 RMS 从 0.3px 拉到 20px 以上。
  std::vector<size_t> keep(object_points.size());
  for (size_t i = 0; i < keep.size(); ++i) {
    keep[i] = i;
  }
  cv::Mat K1;
  cv::Mat D1;
  cv::Mat K2;
  cv::Mat D2;
  for (int round = 0; round < 4; ++round) {
    std::vector<std::vector<cv::Point3f>> objs;
    std::vector<std::vector<cv::Point2f>> imgs;
    for (size_t i : keep) {
      objs.push_back(object_points[i]);
      imgs.push_back(img_left[i]);
    }
    std::vector<cv::Mat> rvecs;
    std::vector<cv::Mat> tvecs;
    const double rms = cv::calibrateCamera(objs, imgs, image_size, K1, D1, rvecs, tvecs);
    std::vector<std::pair<double, size_t>> errors;
    for (size_t i = 0; i < objs.size(); ++i) {
      std::vector<cv::Point2f> proj;
      cv::projectPoints(objs[i], rvecs[i], tvecs[i], K1, D1, proj);
      double sum = 0.0;
      for (size_t k = 0; k < proj.size(); ++k) {
        const cv::Point2f d = proj[k] - imgs[i][k];
        sum += d.x * d.x + d.y * d.y;
      }
      errors.emplace_back(std::sqrt(sum / static_cast<double>(proj.size())), keep[i]);
    }
    std::sort(errors.rbegin(), errors.rend());
    std::vector<double> all;
    for (const auto& e : errors) {
      all.push_back(e.first);
    }
    const double median = all[all.size() / 2];
    const double limit = std::max(1.0, 5.0 * median);
    std::vector<size_t> drop;
    for (const auto& e : errors) {
      if (e.first > limit) {
        drop.push_back(e.second);
      }
    }
    std::printf("第 %d 轮: 左目 RMS %.3f px, 中位 %.3f px, 最差 %.2f px (%zu 个视角)\n", round + 1, rms, median,
                errors.front().first, objs.size());
    if (drop.empty() || drop.size() > keep.size() * 2 / 5) {
      break;
    }
    std::printf("        剔除 %zu 个离群视角\n", drop.size());
    std::vector<size_t> next;
    for (size_t i : keep) {
      if (std::find(drop.begin(), drop.end(), i) == drop.end()) {
        next.push_back(i);
      }
    }
    keep.swap(next);
  }

  std::vector<std::vector<cv::Point3f>> objs;
  std::vector<std::vector<cv::Point2f>> imgs_l;
  std::vector<std::vector<cv::Point2f>> imgs_r;
  for (size_t i : keep) {
    objs.push_back(object_points[i]);
    imgs_l.push_back(img_left[i]);
    imgs_r.push_back(img_right[i]);
  }
  std::vector<cv::Mat> rvecs;
  std::vector<cv::Mat> tvecs;
  const double rms1 = cv::calibrateCamera(objs, imgs_l, image_size, K1, D1, rvecs, tvecs);
  const double rms2 = cv::calibrateCamera(objs, imgs_r, image_size, K2, D2, rvecs, tvecs);
  std::printf("单目标定 RMS: 左 %.4f px, 右 %.4f px (使用 %zu 个视角)\n", rms1, rms2, keep.size());

  cv::Mat R;
  cv::Mat T;
  cv::Mat E;
  cv::Mat F;
  const double rms = cv::stereoCalibrate(objs, imgs_l, imgs_r, K1, D1, K2, D2, image_size, R, T, E, F,
                                         cv::CALIB_FIX_INTRINSIC,
                                         cv::TermCriteria(cv::TermCriteria::COUNT + cv::TermCriteria::EPS, 100, 1e-6));
  std::printf("双目标定 RMS: %.4f px, baseline = %.2f mm\n", rms, std::abs(T.at<double>(0, 0)));

  cv::Mat R1;
  cv::Mat R2;
  cv::Mat P1;
  cv::Mat P2;
  cv::Mat Q;
  cv::stereoRectify(K1, D1, K2, D2, image_size, R, T, R1, R2, P1, P2, Q, cv::CALIB_ZERO_DISPARITY, 0);
  const double fx = P1.at<double>(0, 0);
  const double baseline = std::abs(T.at<double>(0, 0));
  // Z[m] = fx_px * baseline_mm / disparity_px / 1000，即 Z = k / d
  const double k = fx * baseline / 1000.0;  // 单位：m·px
  std::printf("校正后 fx = %.2f px, 距离公式 Z[m] = %.2f / 视差[px]\n", fx, k);
  for (double d : {10.0, 20.0, 40.0, 80.0, 120.0}) {
    std::printf("  视差 %5.0f px -> 距离 %.2f m (1px 视差误差 ≈ %.1f%% 距离误差)\n", d, k / d, 100.0 / d);
  }

  cv::FileStorage fs(opt.out, cv::FileStorage::WRITE);
  if (!fs.isOpened()) {
    std::fprintf(stderr, "无法写出 %s\n", opt.out.c_str());
    return 1;
  }
  fs << "image_width" << image_size.width;
  fs << "image_height" << image_size.height;
  fs << "M1" << K1 << "D1" << D1;
  fs << "M2" << K2 << "D2" << D2;
  fs << "R" << R << "T" << T;
  fs << "R1" << R1 << "R2" << R2;
  fs << "P1" << P1 << "P2" << P2;
  fs << "Q" << Q;
  fs << "rms" << rms;
  fs << "square_mm" << opt.square_mm;
  fs << "pattern_cols" << opt.cols;
  fs << "pattern_rows" << opt.rows;
  fs << "pairs" << static_cast<int>(keep.size());
  fs.release();
  std::printf("标定结果已写入 %s\n", opt.out.c_str());
  return 0;
}

}  // namespace

int main(int argc, char** argv) {
  std::signal(SIGINT, on_signal);
  Options opt;
  if (!parse(argc, argv, opt)) {
    return 2;
  }
  eye::set_log_level(eye::LogLevel::kWarn);
  // 板上 Mali 的 OpenCL 内核编译会失败（只刷错误日志、无收益），直接关掉
  cv::ocl::setUseOpenCL(false);
  if (opt.mode == "capture") {
#if EYE_HAVE_QT
    if (opt.gui) {
      eye::CalibGuiOptions gui;
      gui.device = opt.device;
      gui.width = opt.width;
      gui.height = opt.height;
      gui.fps = opt.fps;
      gui.swap_eyes = opt.swap_eyes;
      gui.cols = opt.cols;
      gui.rows = opt.rows;
      gui.square_mm = opt.square_mm;
      gui.count = opt.count;
      gui.interval = opt.interval;
      gui.out_dir = opt.dir;
      return eye::run_calib_gui(gui);
    }
#else
    if (opt.gui) {
      std::fprintf(stderr, "该可执行文件未编译 Qt 取景器，改用无界面采集\n");
    }
#endif
    return do_capture(opt);
  }
  if (opt.mode == "calibrate") {
    return do_calibrate(opt);
  }
  if (opt.mode == "probe") {
    return do_probe(opt);
  }
  usage();
  return 2;
}
