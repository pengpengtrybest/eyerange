#include "ui/main_window.hpp"

#include <opencv2/imgcodecs.hpp>
#include <opencv2/imgproc.hpp>

#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QDateTime>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QImage>
#include <QLabel>
#include <QMainWindow>
#include <QPixmap>
#include <QScreen>
#include <QPushButton>
#include <QSlider>
#include <QShortcut>
#include <QSizePolicy>
#include <QSpinBox>
#include <QStatusBar>
#include <QTimer>
#include <QVBoxLayout>
#include <QWidget>

#include <cstdio>
#include <memory>
#include <string>
#include <sys/stat.h>

#include "common/log.hpp"
#include "common/stop_flag.hpp"

namespace eye {
namespace {

QImage mat_to_qimage(const cv::Mat& mat) {
  if (mat.empty()) {
    return QImage();
  }
  if (mat.type() == CV_8UC3) {
    const QImage img(mat.data, mat.cols, mat.rows, static_cast<int>(mat.step), QImage::Format_BGR888);
    return img.copy();
  }
  if (mat.type() == CV_8UC1) {
    const QImage img(mat.data, mat.cols, mat.rows, static_cast<int>(mat.step), QImage::Format_Grayscale8);
    return img.copy();
  }
  return QImage();
}

class MainWindow : public QMainWindow {
 public:
  MainWindow(Pipeline& pipeline, const AppConfig& cfg) : pipeline_(pipeline), cfg_(cfg) {
    setWindowTitle("eye_range - 双目 YOLO 识别 + 立体测距 (OrangePi 5 Max)");
    resize(cfg.ui.window_width, cfg.ui.window_height);

    auto* central = new QWidget(this);
    central->setObjectName("eyeCentral");
    // 全屏时四周默认是浅灰底，改成深色更像“画面终端”
    central->setStyleSheet("#eyeCentral { background: #0b0b0b; }");
    auto* root = new QHBoxLayout(central);
    root->setContentsMargins(4, 4, 4, 4);
    root->setSpacing(4);

    video_label_ = new QLabel(this);
    // 最小尺寸要足够小，否则布局会把窗口撑得比屏幕还大，全屏(Ctrl+F11)就落不下来
    video_label_->setMinimumSize(320, 160);
    // QLabel 的 minimumSizeHint 会跟着 pixmap 长大，形成“窗口越缩越大”的正反馈，
    // 这里让尺寸完全由布局决定
    video_label_->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Ignored);
    video_label_->setAlignment(Qt::AlignCenter);
    video_label_->setStyleSheet("background:#101010;color:#888;");
    video_label_->setText("等待画面...");
    root->addWidget(video_label_, 3);

    auto* side = new QWidget(this);
    auto* side_layout = new QVBoxLayout(side);
    side_layout->setContentsMargins(0, 0, 0, 0);
    side_layout->setSpacing(4);
    side->setMaximumWidth(420);  // 右侧面板限宽，把空间留给画面

    disparity_label_ = new QLabel(this);
    disparity_label_->setMinimumSize(200, 80);
    disparity_label_->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Ignored);
    disparity_label_->setAlignment(Qt::AlignCenter);
    disparity_label_->setStyleSheet("background:#101010;color:#888;");
    disparity_label_->setText("视差图");
    side_layout->addWidget(disparity_label_, 1);

    depth_label_ = new QLabel(this);
    depth_label_->setMinimumSize(200, 80);
    depth_label_->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Ignored);
    depth_label_->setAlignment(Qt::AlignCenter);
    depth_label_->setStyleSheet("background:#101010;color:#888;");
    depth_label_->setText("深度图");
    side_layout->addWidget(depth_label_, 1);

    side_layout->addWidget(build_control_panel());
    side_layout->addWidget(build_status_panel());
    root->addWidget(side, 2);

    setCentralWidget(central);
    statusBar()->setStyleSheet("background:#1a1a1a; color:#bbbbbb;");

    timer_ = new QTimer(this);
    connect(timer_, &QTimer::timeout, this, [this] { on_tick(); });
    // 纯软件渲染，刷新率直接决定 CPU 峰值；默认 20fps（可用 ui.fps 调整）
    const int fps = std::max(5, std::min(60, cfg_.ui.fps));
    timer_->start(1000 / fps);

    // F11 在“铺满屏幕(无边框)”和“窗口”之间切换；Esc 回到窗口
    auto* toggle_fullscreen = new QShortcut(QKeySequence(Qt::Key_F11), this);
    connect(toggle_fullscreen, &QShortcut::activated, this, [this] { toggle_fullscreen_mode(); });
    auto* escape = new QShortcut(QKeySequence(Qt::Key_Escape), this);
    connect(escape, &QShortcut::activated, this, [this] { apply_window_mode(WindowMode::kWindowed); });
    statusBar()->showMessage("F11 切换铺满/窗口 / Esc 回到窗口 / 参数可边跑边调");

    apply_window_mode(cfg_.ui.fullscreen ? WindowMode::kFullscreen
                                         : (cfg_.ui.borderless ? WindowMode::kBorderless : WindowMode::kWindowed));
  }

 private:
  QWidget* build_control_panel() {
    auto* group = new QGroupBox("参数", this);
    auto* form = new QFormLayout(group);
    // 紧凑布局：面板高度直接决定窗口能否在 1024x768 这类小屏上真正全屏
    form->setContentsMargins(6, 6, 6, 6);
    form->setVerticalSpacing(3);
    form->setHorizontalSpacing(6);

    conf_slider_ = new QSlider(Qt::Horizontal, group);
    conf_slider_->setRange(5, 95);
    conf_slider_->setValue(static_cast<int>(pipeline_.conf() * 100));
    connect(conf_slider_, &QSlider::valueChanged, this,
            [this](int v) { pipeline_.set_conf(v / 100.0f); });
    form->addRow("置信度", conf_slider_);

    nms_slider_ = new QSlider(Qt::Horizontal, group);
    nms_slider_->setRange(5, 95);
    nms_slider_->setValue(static_cast<int>(pipeline_.nms() * 100));
    connect(nms_slider_, &QSlider::valueChanged, this, [this](int v) { pipeline_.set_nms(v / 100.0f); });
    form->addRow("NMS", nms_slider_);

    npu_combo_ = new QComboBox(group);
    npu_combo_->addItem("自动 (AUTO)", -1);
    npu_combo_->addItem("NPU0", 0);
    npu_combo_->addItem("NPU1", 1);
    npu_combo_->addItem("NPU2", 2);
    npu_combo_->addItem("NPU0+1+2", -2);
    connect(npu_combo_, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this](int idx) {
      pipeline_.set_npu_core(npu_combo_->itemData(idx).toInt());
    });
    form->addRow("NPU 核", npu_combo_);

    roi_check_ = new QCheckBox("只算目标附近视差(快)", group);
    roi_check_->setChecked(pipeline_.stereo_params().roi_only);
    connect(roi_check_, &QCheckBox::toggled, this, [this](bool on) {
      StereoConfig cfg = pipeline_.stereo_params();
      cfg.roi_only = on;
      pipeline_.set_stereo_params(cfg);
    });
    form->addRow("", roi_check_);

    threads_spin_ = new QSpinBox(group);
    threads_spin_->setRange(1, 8);
    threads_spin_->setValue(pipeline_.stereo_params().threads);
    connect(threads_spin_, QOverload<int>::of(&QSpinBox::valueChanged), this, [this](int v) {
      StereoConfig cfg = pipeline_.stereo_params();
      cfg.threads = v;
      pipeline_.set_stereo_params(cfg);
    });
    form->addRow("测距线程", threads_spin_);

    disp_spin_ = new QSpinBox(group);
    disp_spin_->setRange(16, 256);
    disp_spin_->setSingleStep(16);
    disp_spin_->setValue(pipeline_.stereo_params().num_disparities);
    connect(disp_spin_, QOverload<int>::of(&QSpinBox::valueChanged), this, [this](int v) {
      StereoConfig cfg = pipeline_.stereo_params();
      cfg.num_disparities = (v / 16) * 16;
      pipeline_.set_stereo_params(cfg);
    });
    form->addRow("视差范围", disp_spin_);

    block_spin_ = new QSpinBox(group);
    block_spin_->setRange(3, 21);
    block_spin_->setSingleStep(2);
    block_spin_->setValue(pipeline_.stereo_params().block_size | 1);
    connect(block_spin_, QOverload<int>::of(&QSpinBox::valueChanged), this, [this](int v) {
      StereoConfig cfg = pipeline_.stereo_params();
      cfg.block_size = v | 1;
      pipeline_.set_stereo_params(cfg);
    });
    form->addRow("块大小", block_spin_);

    show_depth_check_ = new QCheckBox("显示深度图", group);
    show_depth_check_->setChecked(cfg_.ui.show_depth);
    depth_label_->setVisible(cfg_.ui.show_depth);
    connect(show_depth_check_, &QCheckBox::toggled, this,
            [this](bool on) { depth_label_->setVisible(on); });
    form->addRow("", show_depth_check_);

    auto* buttons = new QWidget(group);
    auto* button_layout = new QHBoxLayout(buttons);
    auto* snap_button = new QPushButton("保存快照", buttons);
    connect(snap_button, &QPushButton::clicked, this, [this] { save_snapshot(); });
    button_layout->addWidget(snap_button);
    auto* full_button = new QPushButton("全屏切换", buttons);
    connect(full_button, &QPushButton::clicked, this, [this] { toggle_fullscreen_mode(); });
    button_layout->addWidget(full_button);
    form->addRow(buttons);

    return group;
  }

  QWidget* build_status_panel() {
    auto* group = new QGroupBox("状态", this);
    auto* layout = new QVBoxLayout(group);
    layout->setContentsMargins(6, 6, 6, 6);
    stats_label_ = new QLabel(group);
    stats_label_->setTextFormat(Qt::PlainText);
    stats_label_->setStyleSheet("font-family:monospace; font-size:10px;");
    stats_label_->setWordWrap(true);
    stats_label_->setMinimumWidth(200);
    layout->addWidget(stats_label_);
    return group;
  }

  enum class WindowMode { kWindowed, kBorderless, kFullscreen };

  // 铺满屏幕的三种做法：
  //   kWindowed   普通窗口（最大化）
  //   kBorderless 无边框 + 屏幕几何（推荐；不触发合成器的全屏 unredirect）
  //   kFullscreen 真正的 EWMH 全屏（板上开着 xfwm4 合成器时可能把系统拖死）
  void apply_window_mode(WindowMode mode) {
    if (isFullScreen()) {
      showNormal();
    }
    switch (mode) {
      case WindowMode::kFullscreen:
        setWindowFlags(Qt::Window);
        showFullScreen();
        break;
      case WindowMode::kBorderless: {
        setWindowFlags(Qt::Window | Qt::FramelessWindowHint);
        show();
        // 用“可用区域”(屏幕去掉面板/任务栏)而不是整块屏幕：否则窗口会被
        // 窗口管理器按面板高度下推，底部溢出屏幕被裁掉。
        const QRect geo = screen() != nullptr ? screen()->availableGeometry() : geometry();
        setGeometry(geo);
        raise();
        activateWindow();
        break;
      }
      case WindowMode::kWindowed:
      default:
        setWindowFlags(Qt::Window);
        showMaximized();
        break;
    }
  }

  void toggle_fullscreen_mode() {
    apply_window_mode(isFullScreen() || windowFlags().testFlag(Qt::FramelessWindowHint) ? WindowMode::kWindowed
                                                                                        : WindowMode::kBorderless);
  }

  void save_snapshot() {
    const auto frame = pipeline_.latest();
    if (!frame || frame->annotated_bgr.empty()) {
      statusBar()->showMessage("还没有可保存的画面", 2000);
      return;
    }
    const QString stamp = QDateTime::currentDateTime().toString("yyyyMMdd_HHmmss_zzz");
    const std::string base = "snapshots/frame_" + stamp.toStdString();
    ::mkdir("snapshots", 0755);
    cv::imwrite(base + ".png", frame->annotated_bgr);
    if (frame->has_disparity && !frame->disparity_vis.empty()) {
      cv::imwrite(base + "_disparity.png", frame->disparity_vis);
    }
    statusBar()->showMessage(QString::fromStdString("已保存 " + base + ".png"), 3000);
  }

  void on_tick() {
    if (eye::stop_flag().load()) {
      // 收到 SIGINT/SIGTERM（例如 Ctrl-C 或 systemd 停止）：退出事件循环
      QApplication::quit();
      return;
    }
    const auto frame = pipeline_.latest();
    if (frame) {
      if (!frame->annotated_bgr.empty()) {
        const QImage img = mat_to_qimage(frame->annotated_bgr);
        // 默认用最近邻缩放：画面本来就大于显示区，平滑插值纯属浪费 CPU/功耗
        const Qt::TransformationMode mode =
            cfg_.ui.smooth_scale ? Qt::SmoothTransformation : Qt::FastTransformation;
        video_label_->setPixmap(QPixmap::fromImage(img).scaled(video_label_->size(), Qt::KeepAspectRatio, mode));
      }
      if (frame->has_disparity && !frame->disparity_vis.empty()) {
        const QImage dimg = mat_to_qimage(frame->disparity_vis);
        disparity_label_->setPixmap(QPixmap::fromImage(dimg).scaled(disparity_label_->size(), Qt::KeepAspectRatio,
                                                                    Qt::FastTransformation));
        if (!frame->depth_vis.empty()) {
          const QImage zimg = mat_to_qimage(frame->depth_vis);
          depth_label_->setPixmap(QPixmap::fromImage(zimg).scaled(depth_label_->size(), Qt::KeepAspectRatio,
                                                                  Qt::FastTransformation));
        }
      }
    }
    update_stats();
  }

  void update_stats() {
    ++display_frames_;
    const double now = QDateTime::currentMSecsSinceEpoch() / 1000.0;
    if (now - display_fps_ts_ >= 1.0) {
      display_fps_ = display_frames_ / (now - display_fps_ts_);
      display_frames_ = 0;
      display_fps_ts_ = now;
      pipeline_.report_display_fps(display_fps_);
    }
    const PipelineStats st = pipeline_.stats();
    char text[1024];
    std::snprintf(text, sizeof(text),
                  "界面 %.1f | 采集 %.1f | 识别 %.1f fps\n"
                  "前处理 %.1fms 推理 %.1fms 后 %.1fms\n"
                  "测距 %.1f fps 匹配 %.1fms\n"
                  "端到端 %.0f ms   NPU %s\n"
                  "标定 %s  fx%.0f B%.0fmm\n"
                  "帧 采集%lld 识别%lld 测距%lld\n"
                  "来源 %s",
                  display_fps_, st.capture_fps, st.detect_fps, st.preprocess_ms, st.infer_ms, st.postprocess_ms,
                  st.stereo_fps, st.stereo_ms, st.pipeline_ms, npu_core_name(st.npu_core),
                  pipeline_.calibrated() ? "已加载" : "未标定", pipeline_.fx_px(), pipeline_.baseline_mm(),
                  st.frames_captured, st.frames_detected, st.frames_stereo, pipeline_.source_description().c_str());
    stats_label_->setText(QString::fromUtf8(text));
  }

  Pipeline& pipeline_;
  AppConfig cfg_;
  QLabel* video_label_ = nullptr;
  QLabel* disparity_label_ = nullptr;
  QLabel* depth_label_ = nullptr;
  QLabel* stats_label_ = nullptr;
  QSlider* conf_slider_ = nullptr;
  QSlider* nms_slider_ = nullptr;
  QComboBox* npu_combo_ = nullptr;
  QCheckBox* roi_check_ = nullptr;
  QCheckBox* show_depth_check_ = nullptr;
  QSpinBox* threads_spin_ = nullptr;
  QSpinBox* disp_spin_ = nullptr;
  QSpinBox* block_spin_ = nullptr;
  QTimer* timer_ = nullptr;
  double display_fps_ = 0.0;
  double display_fps_ts_ = 0.0;
  int display_frames_ = 0;
};

}  // namespace

int run_ui(Pipeline& pipeline, const AppConfig& cfg) {
  static int argc = 1;
  static char name[] = "eye_range";
  static char* argv[] = {name, nullptr};
  QApplication app(argc, argv);
  MainWindow window(pipeline, cfg);
  return app.exec();
}

}  // namespace eye
