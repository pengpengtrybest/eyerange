#pragma once

#include "app/pipeline.hpp"
#include "common/config.hpp"

namespace eye {

// 启动 Qt 界面并进入事件循环；返回进程退出码
int run_ui(Pipeline& pipeline, const AppConfig& cfg);

}  // namespace eye
