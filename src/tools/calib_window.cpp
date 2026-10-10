// 标定取景器：板子显示器上实时显示双目的棋盘检出情况，命中就自动存一对图
#include "tools/calib_window.hpp"

#include <opencv2/calib3d.hpp>
#include <opencv2/core/ocl.hpp>
#include <opencv2/imgcodecs.hpp>
#include <opencv2/imgproc.hpp>

#include <QApplication>
#include <QDateTime>
#include <QImage>
#include <QKeyEvent>
#include <QLabel>
#include <QMainWindow>
#include <QPixmap>
#include <QShortcut>
#include <QStatusBar>
#include <QTimer>
#include <QVBoxLayout>
#include <QWidget>

#include <sys/stat.h>

#include <chrono>
#include <cstdio>
#include <string>
#include <vector>

#include "capture/stereo_source.hpp"

namespace eye {
namespace {

bool find_corners_fast(const cv::Mat& gray, const cv::Size& pattern, std::vector<cv::Point2f>& corners) {
  corners.clear();
  const int flags = cv::CALIB_CB_ADAPTIVE_THRESH | cv::CALIB_CB_NORMALIZE_IMAGE | cv::CALIB_CB_FAST_CHECK;
  return cv::findChessboardCorners(gray, pattern, corners, flags);
}

bool find_corners_accurate(const cv::Mat& gray, const cv::Size& pattern, std::vector<cv::Point2f>& corners) {
  corners.clear();
  if (cv::findChessboardCornersSB(gray, pattern, corners, cv::CALIB_CB_EXHAUSTIVE | cv::CALIB_CB_ACCURACY)) {
    return true;
  }
  corners.clear();
  const int flags = cv::CALIB_CB_ADAPTIVE_THRESH | cv::CALIB_CB_NORMALIZE_IMAGE;
  if (!cv::findChessboardCorners(gray, pattern, corners, flags)) {
    return false;
  }
  cv::cornerSubPix(gray, corners, cv::Size(11, 11), cv::Size(-1, -1),
                   cv::TermCriteria(cv::TermCriteria::EPS + cv::TermCriteria::COUNT, 30, 0.01));
  return true;
}

QImage to_qimage(const cv::Mat& mat) {
  if (mat.empty()) {
    return QImage();
  }
  const QImage img(mat.data, mat.cols, mat.rows, static_cast<int>(mat.step), QImage::Format_BGR888);
  return img.copy();
}

double now_s() {
  return std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count();
}

class CalibWindow : public QMainWindow {
 public:
  explicit CalibWindow(const CalibGuiOptions& opt) : opt_(opt) {
    setWindowTitle("eye_range 标定取景器");
    auto* central = new QWidget(this);
    central->setObjectName("calibCentral");
    central->setStyleSheet("#calibCentral { background:#0b0b0b; }");
    auto* layout = new QVBoxLayout(central);
    layout->setContentsMargins(0, 0, 0, 0);
    view_ = new QLabel(central);
    view_->setAlignment(Qt::AlignCenter);
    view_->setStyleSheet("color:#ddd; font-size:16px;");
    view_->setText("正在打开相机...");
    layout->addWidget(view_);
    setCentralWidget(central);
    statusBar()->setStyleSheet("background:#1a1a1a; color:#cccccc; font-size:12px;");
    statusBar()->showMessage(
        "把棋盘格放进画面（建议占画面 1/3 以上），缓慢改变位置/角度；空格=手动保存当前帧，Q=结束采集");

    auto* quit_shortcut = new QShortcut(QKeySequence(Qt::Key_Q), this);
    connect(quit_shortcut, &QShortcut::activated, this, [this] { close(); });
    auto* full_shortcut = new QShortcut(QKeySequence(Qt::Key_F11), this);
    connect(full_shortcut, &QShortcut::activated, this, [this] {
      if (isFullScreen()) {
        showMaximized();
      } else {
        showFullScreen();
      }
    });

    if (!source_.open(capture_cfg(), &error_)) {
      view_->setText(QString::fromStdString("打开相机失败: " + error_));
    }
    ::mkdir(opt_.out_dir.c_str(), 0755);

    if (opt_.fullscreen) {
      showFullScreen();
    } else {
      resize(1280, 800);
      show();
    }

    timer_ = new QTimer(this);
    connect(timer_, &QTimer::timeout, this, [this] { on_tick(); });
    timer_->start(40);
  }

 protected:
  void keyPressEvent(QKeyEvent* event) override {
    if (event->key() == Qt::Key_Space) {
      force_save_ = true;
    } else if (event->key() == Qt::Key_Q || event->key() == Qt::Key_Escape) {
      close();
    } else {
      QMainWindow::keyPressEvent(event);
    }
  }

 private:
  CaptureConfig capture_cfg() const {
    CaptureConfig cfg;
    cfg.device = opt_.device;
    cfg.width = opt_.width;
    cfg.height = opt_.height;
    cfg.fps = opt_.fps;
    cfg.swap_eyes = opt_.swap_eyes;
    return cfg;
  }

  void on_tick() {
    if (!source_.is_open()) {
      return;
    }
    StereoPair pair;
    if (!source_.grab(pair, 40)) {
      return;
    }
    cv::Mat gl;
    cv::Mat gr;
    cv::cvtColor(pair.left, gl, cv::COLOR_BGR2GRAY);
    cv::cvtColor(pair.right, gr, cv::COLOR_BGR2GRAY);

    std::vector<cv::Point2f> cl;
    std::vector<cv::Point2f> cr;
    ok_left_ = find_corners_fast(gl, pattern(), cl);
    ok_right_ = ok_left_ ? find_corners_fast(gr, pattern(), cr) : false;

    // 质检：太小/太满/左右不匹配/视差异常/模糊/与上一张几乎同姿势，都不存
    std::string reason;
    float coverage = 0.f;
    double center_x = 0.0;
    double board_w = 0.0;
    if (ok_left_ && ok_right_) {
      const cv::Rect bl = cv::boundingRect(cl);
      const cv::Rect br = cv::boundingRect(cr);
      coverage = static_cast<float>(bl.area()) / static_cast<float>(gl.total());
      board_w = bl.width;
      center_x = cv::mean(cl)[0];
      const double dcy = cv::mean(cl)[1] - cv::mean(cr)[1];
      const double dcx = cv::mean(cl)[0] - cv::mean(cr)[0];
      const double wr = static_cast<double>(bl.width) / br.width;
      cv::Mat lap;
      cv::Laplacian(gl, lap, CV_64F);
      const double sharp = cv::mean(lap.mul(lap))[0];  // 清晰度：Laplacian 方差
      if (coverage < 0.03f) {
        reason = "board too small";
      } else if (coverage > 0.8f) {
        reason = "board too close";
      } else if (std::abs(dcy) > 0.15 * bl.height) {
        reason = "eyes mismatch";
      } else if (dcx <= 0 || dcx > 0.5 * bl.width) {
        reason = "disparity odd";
      } else if (wr < 0.8 || wr > 1.25) {
        reason = "scale mismatch";
      } else if (sharp < 20.0) {
        reason = "blurry - hold still";
      } else if (has_last_ && std::abs(center_x - last_center_x_) < 0.05 * gl.cols &&
                 std::abs(board_w - last_board_w_) < 0.08 * last_board_w_) {
        reason = "same pose - move board";
      }
    } else {
      reason = "no board";
    }

    const double now = now_s();
    const bool quality_ok = ok_left_ && ok_right_ && reason.empty();
    if (((quality_ok && now - last_save_ >= opt_.interval) || (force_save_ && ok_left_ && ok_right_)) &&
        saved_ < opt_.count) {
      // 保存时用更准的检测，保证角点质量
      std::vector<cv::Point2f> al;
      std::vector<cv::Point2f> ar;
      const bool accurate = find_corners_accurate(gl, pattern(), al) && find_corners_accurate(gr, pattern(), ar);
      if (accurate) {
        char name[512];
        std::snprintf(name, sizeof(name), "%s/left_%02d.png", opt_.out_dir.c_str(), saved_);
        cv::imwrite(name, pair.left);
        std::snprintf(name, sizeof(name), "%s/right_%02d.png", opt_.out_dir.c_str(), saved_);
        cv::imwrite(name, pair.right);
        ++saved_;
        last_save_ = now;
        flash_until_ = now + 0.6;
        has_last_ = true;
        last_center_x_ = center_x;
        last_board_w_ = board_w;
      }
      force_save_ = false;
    }

    cv::Mat show = pair.left.clone();
    if (ok_left_) {
      cv::drawChessboardCorners(show, pattern(), cl, true);
    }
    coverage_ = coverage;
    char overlay[256];
    if (saved_ >= opt_.count) {
      std::snprintf(overlay, sizeof(overlay), "saved %d/%d  DONE - press Q to quit", saved_, opt_.count);
    } else if (reason.empty()) {
      std::snprintf(overlay, sizeof(overlay), "saved %d/%d  L=%s R=%s  board=%.0f%%  capturing...", saved_,
                    opt_.count, ok_left_ ? "OK" : "--", ok_right_ ? "OK" : "--", coverage_ * 100);
    } else {
      std::snprintf(overlay, sizeof(overlay), "saved %d/%d  L=%s R=%s  board=%.0f%%  [%s]", saved_, opt_.count,
                    ok_left_ ? "OK" : "--", ok_right_ ? "OK" : "--", coverage_ * 100, reason.c_str());
    }
    cv::rectangle(show, cv::Rect(0, 0, show.cols, 40), cv::Scalar(0, 0, 0), cv::FILLED);
    cv::putText(show, overlay, cv::Point(10, 28), cv::FONT_HERSHEY_SIMPLEX, 0.8,
                (ok_left_ && ok_right_) ? cv::Scalar(0, 255, 0) : cv::Scalar(0, 200, 255), 2);
    if (now < flash_until_) {
      cv::rectangle(show, cv::Rect(2, 2, show.cols - 4, show.rows - 4), cv::Scalar(0, 255, 0), 8);
    }
    if (saved_ >= opt_.count) {
      cv::putText(show, "DONE - press Q", cv::Point(10, show.rows - 20), cv::FONT_HERSHEY_SIMPLEX, 1.0,
                  cv::Scalar(0, 255, 0), 3);
    }

    view_->setPixmap(QPixmap::fromImage(to_qimage(show)).scaled(view_->size(), Qt::KeepAspectRatio,
                                                                Qt::FastTransformation));
  }

  cv::Size pattern() const { return cv::Size(opt_.cols, opt_.rows); }

  CalibGuiOptions opt_;
  StereoSource source_;
  std::string error_;
  QLabel* view_ = nullptr;
  QTimer* timer_ = nullptr;
  int saved_ = 0;
  double last_save_ = -1e9;
  double flash_until_ = -1e9;
  bool ok_left_ = false;
  bool ok_right_ = false;
  bool force_save_ = false;
  float coverage_ = 0.f;
  bool has_last_ = false;
  double last_center_x_ = 0.0;
  double last_board_w_ = 0.0;
};

}  // namespace

int run_calib_gui(const CalibGuiOptions& opt) {
  cv::ocl::setUseOpenCL(false);
  static int argc = 1;
  static char name[] = "eye_range_calib";
  static char* argv[] = {name, nullptr};
  QApplication app(argc, argv);
  CalibWindow window(opt);
  const int rc = app.exec();
  return rc;
}

}  // namespace eye
