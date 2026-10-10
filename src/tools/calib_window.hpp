#pragma once

#include <string>

namespace eye {

// 标定取景器（Qt 全屏）：实时显示画面与棋盘检出状态，自动保存图像对
struct CalibGuiOptions {
  std::string device = "/dev/video0";
  int width = 2560;
  int height = 960;
  int fps = 30;
  bool swap_eyes = false;
  int cols = 9;
  int rows = 6;
  double square_mm = 25.0;
  int count = 20;
  double interval = 1.3;
  std::string out_dir = "calib_images";
  bool fullscreen = true;
};

int run_calib_gui(const CalibGuiOptions& opt);

}  // namespace eye
