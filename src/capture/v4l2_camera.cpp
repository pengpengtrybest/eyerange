#include "capture/v4l2_camera.hpp"

#include <fcntl.h>
#include <linux/videodev2.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <sys/select.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#include <opencv2/imgcodecs.hpp>
#include <opencv2/imgproc.hpp>

#include <cerrno>
#include <chrono>
#include <algorithm>
#include <cstdint>
#include <cstring>

#include "common/log.hpp"

namespace eye {
namespace {

bool xioctl(int fd, unsigned long request, void* arg) {
  int ret;
  do {
    ret = ::ioctl(fd, request, arg);
  } while (ret == -1 && errno == EINTR);
  return ret != -1;
}

std::string fourcc_to_string(uint32_t fourcc) {
  std::string s(4, ' ');
  s[0] = static_cast<char>(fourcc & 0xFF);
  s[1] = static_cast<char>((fourcc >> 8) & 0xFF);
  s[2] = static_cast<char>((fourcc >> 16) & 0xFF);
  s[3] = static_cast<char>((fourcc >> 24) & 0xFF);
  return s;
}

}  // namespace

V4l2Camera::~V4l2Camera() { close(); }

bool V4l2Camera::open(const std::string& device, int width, int height, int fps, int buffer_count, std::string* err) {
  close();
  device_ = device;

  fd_ = ::open(device.c_str(), O_RDWR | O_NONBLOCK, 0);
  if (fd_ < 0) {
    if (err) {
      *err = "打开 " + device + " 失败: " + std::strerror(errno);
    }
    return false;
  }

  v4l2_capability cap{};
  if (!xioctl(fd_, VIDIOC_QUERYCAP, &cap)) {
    if (err) {
      *err = "VIDIOC_QUERYCAP 失败: " + std::string(std::strerror(errno));
    }
    close();
    return false;
  }
  if (!(cap.capabilities & V4L2_CAP_VIDEO_CAPTURE)) {
    if (err) {
      *err = device + " 不是视频采集设备";
    }
    close();
    return false;
  }

  v4l2_format fmt{};
  fmt.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
  fmt.fmt.pix.width = static_cast<uint32_t>(width);
  fmt.fmt.pix.height = static_cast<uint32_t>(height);
  fmt.fmt.pix.pixelformat = V4L2_PIX_FMT_MJPEG;
  fmt.fmt.pix.field = V4L2_FIELD_ANY;
  if (!xioctl(fd_, VIDIOC_S_FMT, &fmt)) {
    if (err) {
      *err = "VIDIOC_S_FMT(MJPG) 失败: " + std::string(std::strerror(errno));
    }
    close();
    return false;
  }
  width_ = static_cast<int>(fmt.fmt.pix.width);
  height_ = static_cast<int>(fmt.fmt.pix.height);
  format_name_ = fourcc_to_string(fmt.fmt.pix.pixelformat);
  if (fmt.fmt.pix.pixelformat != V4L2_PIX_FMT_MJPEG) {
    EYE_LOG_WARN("驱动未接受 MJPG，实际格式为 " + format_name_);
  }

  v4l2_streamparm parm{};
  parm.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
  parm.parm.capture.timeperframe.numerator = 1;
  parm.parm.capture.timeperframe.denominator = static_cast<uint32_t>(fps);
  if (!xioctl(fd_, VIDIOC_S_PARM, &parm)) {
    EYE_LOG_WARN("设置帧率失败（忽略）: " + std::string(std::strerror(errno)));
  }

  if (!init_mmap(std::max(2, buffer_count), err)) {
    close();
    return false;
  }

  v4l2_buf_type type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
  if (!xioctl(fd_, VIDIOC_STREAMON, &type)) {
    if (err) {
      *err = "VIDIOC_STREAMON 失败: " + std::string(std::strerror(errno));
    }
    close();
    return false;
  }

  EYE_LOG_INFO("相机已打开 " + device_ + " " + std::to_string(width_) + "x" + std::to_string(height_) + " " +
               format_name_ + " @" + std::to_string(fps) + "fps, 缓冲 " + std::to_string(buffers_.size()));
  return true;
}

bool V4l2Camera::init_mmap(int buffer_count, std::string* err) {
  v4l2_requestbuffers req{};
  req.count = static_cast<uint32_t>(buffer_count);
  req.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
  req.memory = V4L2_MEMORY_MMAP;
  if (!xioctl(fd_, VIDIOC_REQBUFS, &req)) {
    if (err) {
      *err = "VIDIOC_REQBUFS 失败: " + std::string(std::strerror(errno));
    }
    return false;
  }
  if (req.count < 2) {
    if (err) {
      *err = "驱动申请的缓冲区不足";
    }
    return false;
  }

  buffers_.resize(req.count);
  for (uint32_t i = 0; i < req.count; ++i) {
    v4l2_buffer buf{};
    buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    buf.memory = V4L2_MEMORY_MMAP;
    buf.index = i;
    if (!xioctl(fd_, VIDIOC_QUERYBUF, &buf)) {
      if (err) {
        *err = "VIDIOC_QUERYBUF 失败: " + std::string(std::strerror(errno));
      }
      return false;
    }
    buffers_[i].length = buf.length;
    buffers_[i].start = ::mmap(nullptr, buf.length, PROT_READ | PROT_WRITE, MAP_SHARED, fd_, buf.m.offset);
    if (buffers_[i].start == MAP_FAILED) {
      buffers_[i].start = nullptr;
      if (err) {
        *err = "mmap 失败: " + std::string(std::strerror(errno));
      }
      return false;
    }
    if (!queue_buffer(static_cast<int>(i), err)) {
      return false;
    }
  }
  return true;
}

bool V4l2Camera::queue_buffer(int index, std::string* err) {
  v4l2_buffer buf{};
  buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
  buf.memory = V4L2_MEMORY_MMAP;
  buf.index = static_cast<uint32_t>(index);
  if (!xioctl(fd_, VIDIOC_QBUF, &buf)) {
    if (err) {
      *err = "VIDIOC_QBUF 失败: " + std::string(std::strerror(errno));
    }
    return false;
  }
  return true;
}

bool V4l2Camera::grab(cv::Mat& bgr, int timeout_ms) {
  if (fd_ < 0) {
    return false;
  }
  fd_set fds;
  FD_ZERO(&fds);
  FD_SET(fd_, &fds);
  timeval tv{};
  timeval* tv_ptr = nullptr;
  if (timeout_ms >= 0) {
    tv.tv_sec = timeout_ms / 1000;
    tv.tv_usec = (timeout_ms % 1000) * 1000;
    tv_ptr = &tv;
  }
  const int sel = ::select(fd_ + 1, &fds, nullptr, nullptr, tv_ptr);
  if (sel <= 0) {
    return false;
  }

  v4l2_buffer buf{};
  buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
  buf.memory = V4L2_MEMORY_MMAP;
  if (!xioctl(fd_, VIDIOC_DQBUF, &buf)) {
    if (errno != EAGAIN) {
      EYE_LOG_WARN("VIDIOC_DQBUF 失败: " + std::string(std::strerror(errno)));
    }
    return false;
  }

  bool ok = false;
  if (buf.index < buffers_.size() && buf.bytesused > 0) {
    const auto t0 = std::chrono::steady_clock::now();
    const cv::Mat raw(1, static_cast<int>(buf.bytesused), CV_8UC1,
                      static_cast<void*>(static_cast<uint8_t*>(buffers_[buf.index].start)));
    cv::Mat decoded = cv::imdecode(raw, cv::IMREAD_COLOR);
    const auto t1 = std::chrono::steady_clock::now();
    last_decode_ms_ = std::chrono::duration<double, std::milli>(t1 - t0).count();
    if (!decoded.empty()) {
      bgr = std::move(decoded);
      ok = true;
      ++frame_count_;
    }
  }

  std::string err;
  queue_buffer(static_cast<int>(buf.index), &err);
  return ok;
}

void V4l2Camera::close() {
  if (fd_ >= 0) {
    v4l2_buf_type type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    xioctl(fd_, VIDIOC_STREAMOFF, &type);
  }
  for (Buffer& b : buffers_) {
    if (b.start != nullptr) {
      ::munmap(b.start, b.length);
    }
  }
  buffers_.clear();
  if (fd_ >= 0) {
    ::close(fd_);
    fd_ = -1;
  }
}

}  // namespace eye
