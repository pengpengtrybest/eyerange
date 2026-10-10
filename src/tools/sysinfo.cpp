// 运行环境自检：版本、设备、相机能力、NPU 状态
#include <fcntl.h>
#include <linux/videodev2.h>
#include <sys/ioctl.h>
#include <sys/stat.h>
#include <sys/utsname.h>
#include <unistd.h>

#include <opencv2/core.hpp>
#include <opencv2/core/version.hpp>

#include <cstdio>
#include <cerrno>
#include <cstring>
#include <string>
#include <vector>

#if EYE_HAVE_QT
#include <QtCore/qglobal.h>
#endif

#include "detect/rknn_engine.hpp"

namespace {

bool xioctl(int fd, unsigned long request, void* arg) {
  return ::ioctl(fd, request, arg) != -1;
}

void print_camera_info(const std::string& device) {
  const int fd = ::open(device.c_str(), O_RDWR | O_NONBLOCK, 0);
  if (fd < 0) {
    std::printf("  %s: 打不开 (%s)\n", device.c_str(), std::strerror(errno));
    return;
  }
  v4l2_capability cap{};
  if (!xioctl(fd, VIDIOC_QUERYCAP, &cap)) {
    std::printf("  %s: 查询能力失败\n", device.c_str());
    ::close(fd);
    return;
  }
  std::printf("  %s: %s (%s), 驱动 %s\n", device.c_str(), cap.card, cap.bus_info, cap.driver);

  v4l2_fmtdesc fmt{};
  fmt.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
  for (fmt.index = 0; xioctl(fd, VIDIOC_ENUM_FMT, &fmt); ++fmt.index) {
    char fourcc[5] = {static_cast<char>(fmt.pixelformat & 0xFF),
                      static_cast<char>((fmt.pixelformat >> 8) & 0xFF),
                      static_cast<char>((fmt.pixelformat >> 16) & 0xFF),
                      static_cast<char>((fmt.pixelformat >> 24) & 0xFF), 0};
    std::printf("    格式 %s (%s)\n", fourcc, fmt.description);
    v4l2_frmsizeenum size{};
    size.pixel_format = fmt.pixelformat;
    for (size.index = 0; xioctl(fd, VIDIOC_ENUM_FRAMESIZES, &size); ++size.index) {
      if (size.type != V4L2_FRMSIZE_TYPE_DISCRETE) {
        continue;
      }
      std::printf("      %ux%u :", size.discrete.width, size.discrete.height);
      v4l2_frmivalenum iv{};
      iv.pixel_format = fmt.pixelformat;
      iv.width = size.discrete.width;
      iv.height = size.discrete.height;
      for (iv.index = 0; xioctl(fd, VIDIOC_ENUM_FRAMEINTERVALS, &iv); ++iv.index) {
        if (iv.type == V4L2_FRMIVAL_TYPE_DISCRETE && iv.discrete.numerator > 0) {
          std::printf(" %.1ffps", static_cast<double>(iv.discrete.denominator) / iv.discrete.numerator);
        }
      }
      std::printf("\n");
    }
  }
  ::close(fd);
}

bool file_exists(const std::string& path) {
  struct stat st {};
  return ::stat(path.c_str(), &st) == 0;
}

std::string read_first_line(const std::string& path) {
  FILE* f = std::fopen(path.c_str(), "r");
  if (f == nullptr) {
    return "";
  }
  char buf[256] = {0};
  if (std::fgets(buf, sizeof(buf), f) == nullptr) {
    buf[0] = 0;
  }
  std::fclose(f);
  std::string s(buf);
  while (!s.empty() && (s.back() == '\n' || s.back() == '\r')) {
    s.pop_back();
  }
  return s;
}

}  // namespace

int main(int argc, char** argv) {
  std::string device = "/dev/video0";
  std::string model;
  for (int i = 1; i < argc; ++i) {
    if (std::strcmp(argv[i], "--device") == 0 && i + 1 < argc) {
      device = argv[++i];
    } else if (std::strcmp(argv[i], "--model") == 0 && i + 1 < argc) {
      model = argv[++i];
    }
  }

  utsname uts{};
  ::uname(&uts);
  std::printf("=== eye_range 环境自检 ===\n");
  std::printf("系统      : %s %s (%s)\n", uts.sysname, uts.release, uts.machine);
  std::printf("CPU 核数  : %ld\n", ::sysconf(_SC_NPROCESSORS_ONLN));
  std::printf("CPU 型号  : %s\n", read_first_line("/proc/device-tree/model").c_str());
  std::printf("OpenCV    : %s\n", CV_VERSION);
#if EYE_HAVE_QT
  std::printf("Qt        : %s\n", qVersion());
#else
  std::printf("Qt        : 未编译\n");
#endif
  if (!model.empty()) {
    eye::RknnEngine engine;
    std::string err;
    if (engine.load(model, -1, &err)) {
      std::printf("RKNN 运行时: %s\n", engine.sdk_version().c_str());
      std::printf("RKNN 模型  : %s 输入 %dx%dx%d %s\n", model.c_str(), engine.model_width(), engine.model_height(),
                  engine.model_channel(), engine.quantized() ? "(量化)" : "(浮点)");
    } else {
      std::printf("RKNN 运行时: 加载模型失败: %s\n", err.c_str());
    }
  } else {
    std::printf("RKNN 运行时: 需要 --model <rknn> 才能查询版本\n");
  }
  std::printf("librknnrt : %s\n", file_exists("/usr/lib/librknnrt.so") ? "已安装 /usr/lib/librknnrt.so" : "未找到");
  std::printf("NPU 驱动  : %s\n", read_first_line("/sys/kernel/debug/rknpu/version").c_str());
  std::printf("显示      : %s\n", file_exists("/dev/fb0") ? "/dev/fb0 存在" : "无 framebuffer");
  std::printf("相机设备  :\n");
  for (int i = 0; i < 12; ++i) {
    const std::string dev = "/dev/video" + std::to_string(i);
    if (file_exists(dev)) {
      print_camera_info(dev);
    }
  }
  return 0;
}
