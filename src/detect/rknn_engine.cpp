#include "detect/rknn_engine.hpp"

#include <cstdio>
#include <cstring>
#include <fstream>

#include "common/log.hpp"

namespace eye {
namespace {

const char* type_name(rknn_tensor_type t) {
  switch (t) {
    case RKNN_TENSOR_FLOAT32:
      return "float32";
    case RKNN_TENSOR_FLOAT16:
      return "float16";
    case RKNN_TENSOR_INT8:
      return "int8";
    case RKNN_TENSOR_UINT8:
      return "uint8";
    case RKNN_TENSOR_INT16:
      return "int16";
    default:
      return "other";
  }
}

const char* fmt_name(rknn_tensor_format f) {
  switch (f) {
    case RKNN_TENSOR_NCHW:
      return "NCHW";
    case RKNN_TENSOR_NHWC:
      return "NHWC";
    default:
      return "UNDEF";
  }
}

rknn_core_mask to_core_mask(int core) {
  switch (core) {
    case 0:
      return RKNN_NPU_CORE_0;
    case 1:
      return RKNN_NPU_CORE_1;
    case 2:
      return RKNN_NPU_CORE_2;
    case -2:
      return RKNN_NPU_CORE_0_1_2;
    default:
      return RKNN_NPU_CORE_AUTO;
  }
}

bool read_file(const std::string& path, std::vector<char>* out) {
  std::ifstream ifs(path, std::ios::binary | std::ios::ate);
  if (!ifs) {
    return false;
  }
  const std::streamsize size = ifs.tellg();
  if (size <= 0) {
    return false;
  }
  ifs.seekg(0, std::ios::beg);
  out->resize(static_cast<size_t>(size));
  return static_cast<bool>(ifs.read(out->data(), size));
}

cv::Mat wrap_output(const rknn_tensor_attr& attr, const void* data) {
  int type = CV_8U;
  size_t elem = 1;
  bool to_float = false;
  switch (attr.type) {
    case RKNN_TENSOR_FLOAT32:
      type = CV_32F;
      elem = 4;
      break;
    case RKNN_TENSOR_FLOAT16:
      // want_float 场景下 rknn 已返回 float32；此处兼容直接拿到 fp16 的情况
      type = CV_16F;
      elem = 2;
      to_float = true;
      break;
    case RKNN_TENSOR_INT8:
      type = CV_8S;
      break;
    case RKNN_TENSOR_UINT8:
      type = CV_8U;
      break;
    default:
      type = CV_8U;
      break;
  }

  // 用 4 维描述，便于按 dims[1..3] 解码
  int dims[4] = {1, 1, 1, 1};
  const int nd = attr.n_dims <= 4 ? attr.n_dims : 4;
  for (int i = 0; i < nd; ++i) {
    dims[4 - nd + i] = static_cast<int>(attr.dims[i]);
  }
  // 校验元素数一致，否则退化为 1 维
  int total = 1;
  for (int i = 0; i < 4; ++i) {
    total *= dims[i];
  }
  cv::Mat mat;
  if (static_cast<size_t>(total) * elem == attr.n_elems * elem) {
    mat = cv::Mat(4, dims, type).clone();
    std::memcpy(mat.data, data, attr.n_elems * elem);
  } else {
    mat = cv::Mat(1, static_cast<int>(attr.n_elems), type);
    std::memcpy(mat.data, data, attr.n_elems * elem);
  }
  if (to_float) {
    cv::Mat f32;
    mat.convertTo(f32, CV_32F);
    return f32;
  }
  return mat;
}

}  // namespace

RknnEngine::~RknnEngine() { release(); }

bool RknnEngine::load(const std::string& model_path, int npu_core, std::string* err) {
  release();
  npu_core_ = npu_core;

  std::vector<char> model;
  if (!read_file(model_path, &model)) {
    if (err) {
      *err = "读取模型失败: " + model_path;
    }
    return false;
  }

  int ret = rknn_init(&ctx_, model.data(), static_cast<uint32_t>(model.size()), 0, nullptr);
  if (ret != RKNN_SUCC) {
    if (err) {
      *err = "rknn_init 失败 ret=" + std::to_string(ret) + " model=" + model_path;
    }
    ctx_ = 0;
    return false;
  }

  rknn_sdk_version version{};
  if (rknn_query(ctx_, RKNN_QUERY_SDK_VERSION, &version, sizeof(version)) == RKNN_SUCC) {
    sdk_version_ = std::string(version.api_version) + " / " + std::string(version.drv_version);
  }

  ret = rknn_set_core_mask(ctx_, to_core_mask(npu_core));
  if (ret != RKNN_SUCC) {
    EYE_LOG_WARN("rknn_set_core_mask 失败 ret=" + std::to_string(ret) + "，将使用 AUTO");
  }

  rknn_input_output_num io_num{};
  ret = rknn_query(ctx_, RKNN_QUERY_IN_OUT_NUM, &io_num, sizeof(io_num));
  if (ret != RKNN_SUCC) {
    if (err) {
      *err = "RKNN_QUERY_IN_OUT_NUM 失败 ret=" + std::to_string(ret);
    }
    release();
    return false;
  }

  input_attrs_.resize(io_num.n_input);
  for (uint32_t i = 0; i < io_num.n_input; ++i) {
    input_attrs_[i].index = i;
    if (rknn_query(ctx_, RKNN_QUERY_INPUT_ATTR, &input_attrs_[i], sizeof(rknn_tensor_attr)) != RKNN_SUCC) {
      if (err) {
        *err = "查询输入属性失败";
      }
      release();
      return false;
    }
  }
  output_attrs_.resize(io_num.n_output);
  for (uint32_t i = 0; i < io_num.n_output; ++i) {
    output_attrs_[i].index = i;
    if (rknn_query(ctx_, RKNN_QUERY_OUTPUT_ATTR, &output_attrs_[i], sizeof(rknn_tensor_attr)) != RKNN_SUCC) {
      if (err) {
        *err = "查询输出属性失败";
      }
      release();
      return false;
    }
  }

  const rknn_tensor_attr& in0 = input_attrs_[0];
  if (in0.fmt == RKNN_TENSOR_NCHW) {
    model_channel_ = static_cast<int>(in0.dims[1]);
    model_height_ = static_cast<int>(in0.dims[2]);
    model_width_ = static_cast<int>(in0.dims[3]);
  } else {
    model_height_ = static_cast<int>(in0.dims[1]);
    model_width_ = static_cast<int>(in0.dims[2]);
    model_channel_ = static_cast<int>(in0.dims[3]);
  }

  quantized_ = output_attrs_[0].type != RKNN_TENSOR_FLOAT32 &&
               output_attrs_[0].qnt_type != RKNN_TENSOR_QNT_NONE;

  std::string dump = "模型 " + model_path + " 输入 " + std::to_string(model_width_) + "x" +
                     std::to_string(model_height_) + "x" + std::to_string(model_channel_) + " (" +
                     fmt_name(in0.fmt) + " " + type_name(in0.type) + "), 输出 " +
                     std::to_string(output_attrs_.size()) + " 个, 量化=" + (quantized_ ? "是" : "否");
  EYE_LOG_INFO(dump);
  for (size_t i = 0; i < output_attrs_.size(); ++i) {
    const rknn_tensor_attr& a = output_attrs_[i];
    char buf[256];
    std::snprintf(buf, sizeof(buf), "  out[%zu] dims=[%u,%u,%u,%u] zp=%d scale=%f type=%s", i, a.dims[0], a.dims[1],
                  a.dims[2], a.dims[3], a.zp, a.scale, type_name(a.type));
    EYE_LOG_DEBUG(buf);
  }

  return true;
}

void RknnEngine::release() {
  if (ctx_ != 0) {
    rknn_destroy(ctx_);
    ctx_ = 0;
  }
  input_attrs_.clear();
  output_attrs_.clear();
}

bool RknnEngine::run(const uint8_t* input, size_t input_bytes, std::vector<cv::Mat>& outputs, std::string* err) {
  if (ctx_ == 0) {
    if (err) {
      *err = "模型未加载";
    }
    return false;
  }

  rknn_input inputs[1]{};
  inputs[0].index = 0;
  inputs[0].type = RKNN_TENSOR_UINT8;
  inputs[0].fmt = RKNN_TENSOR_NHWC;
  inputs[0].size = static_cast<uint32_t>(input_bytes);
  inputs[0].buf = const_cast<uint8_t*>(input);

  int ret = rknn_inputs_set(ctx_, 1, inputs);
  if (ret != RKNN_SUCC) {
    if (err) {
      *err = "rknn_inputs_set 失败 ret=" + std::to_string(ret);
    }
    return false;
  }

  ret = rknn_run(ctx_, nullptr);
  if (ret != RKNN_SUCC) {
    if (err) {
      *err = "rknn_run 失败 ret=" + std::to_string(ret);
    }
    return false;
  }

  std::vector<rknn_output> outs(output_attrs_.size());
  for (size_t i = 0; i < outs.size(); ++i) {
    outs[i].index = static_cast<uint32_t>(i);
    // 量化模型保持 int8（省掉反量化开销），非量化模型让 rknn 输出 float32
    outs[i].want_float = quantized_ ? 0 : 1;
    outs[i].is_prealloc = 0;
  }
  ret = rknn_outputs_get(ctx_, static_cast<uint32_t>(outs.size()), outs.data(), nullptr);
  if (ret != RKNN_SUCC) {
    if (err) {
      *err = "rknn_outputs_get 失败 ret=" + std::to_string(ret);
    }
    return false;
  }

  outputs.clear();
  outputs.reserve(outs.size());
  for (size_t i = 0; i < outs.size(); ++i) {
    outputs.push_back(wrap_output(output_attrs_[i], outs[i].buf));
  }
  rknn_outputs_release(ctx_, static_cast<uint32_t>(outs.size()), outs.data());
  return true;
}

}  // namespace eye
