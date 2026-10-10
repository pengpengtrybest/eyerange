#pragma once

#include <opencv2/core.hpp>
#include <rknn_api.h>

#include <string>
#include <vector>

namespace eye {

// RKNN 运行时封装：负责模型加载、NPU 核绑定、单输入多输出推理。
class RknnEngine {
 public:
  RknnEngine() = default;
  ~RknnEngine();

  RknnEngine(const RknnEngine&) = delete;
  RknnEngine& operator=(const RknnEngine&) = delete;

  // npu_core: -1=AUTO, 0/1/2=单核, -2=三核
  bool load(const std::string& model_path, int npu_core, std::string* err);
  void release();
  bool loaded() const { return ctx_ != 0; }

  // 输入 NHWC uint8（尺寸 = model_width*model_height*3）
  bool run(const uint8_t* input, size_t input_bytes, std::vector<cv::Mat>& outputs, std::string* err);

  int model_width() const { return model_width_; }
  int model_height() const { return model_height_; }
  int model_channel() const { return model_channel_; }
  bool quantized() const { return quantized_; }
  int npu_core() const { return npu_core_; }

  const std::vector<rknn_tensor_attr>& output_attrs() const { return output_attrs_; }
  const std::string& sdk_version() const { return sdk_version_; }

 private:
  rknn_context ctx_ = 0;
  int model_width_ = 0;
  int model_height_ = 0;
  int model_channel_ = 3;
  bool quantized_ = false;
  int npu_core_ = -1;
  std::string sdk_version_;
  std::vector<rknn_tensor_attr> input_attrs_;
  std::vector<rknn_tensor_attr> output_attrs_;
};

}  // namespace eye
