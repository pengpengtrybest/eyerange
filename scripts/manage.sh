#!/usr/bin/env bash
# 项目管理入口：把「项目文件夹 / 项目工具 / 工作流程」集中到一条命令里。
#
#   scripts/manage.sh overview    项目结构、各目录体积、关键产物是否齐全
#   scripts/manage.sh tools       列出所有项目工具及其用途
#   scripts/manage.sh check       体检：工具链 / Qt / sysroot / git / 板子连通性
#   scripts/manage.sh workflows   打印标准工作流命令（改代码→部署→验证→标定→压测）
#   scripts/manage.sh logs        列出已归档的实测日志
set -uo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
# shellcheck source=board_env.defaults.sh
source "${SCRIPT_DIR}/board_env.defaults.sh"
if [[ -f "${SCRIPT_DIR}/board_env.sh" ]]; then
  # shellcheck source=board_env.sh
  source "${SCRIPT_DIR}/board_env.sh"
fi

ROOT="${PROJECT_ROOT}"
cd "${ROOT}"

ok()   { printf '  \033[32m✓\033[0m %s\n' "$1"; }
bad()  { printf '  \033[31m✗\033[0m %s\n' "$1"; }
warn() { printf '  \033[33m!\033[0m %s\n' "$1"; }
head1() { printf '\n\033[1m== %s ==\033[0m\n' "$1"; }

# 从脚本头部注释里摘一句用途说明
tool_desc() {
  awk 'NR>1 && (/^#/ || /^"""/) {
         sub(/^# ?/, ""); sub(/^"""/, "");
         if ($0 !~ /^!/ && length($0) > 0) {print; exit}
       }' "$1"
}

cmd_overview() {
  head1 "项目结构（顶层）"
  printf '  %-28s %-10s %s\n' "目录/文件" "体积" "说明"
  local desc
  desc="C++ 源码（capture/detect/stereo/fusion/app/ui/tools）";  printf '  %-28s %-10s %s\n' "src/" "$(du -sh src 2>/dev/null | cut -f1)" "$desc"
  desc="过程文档 01~07";                                          printf '  %-28s %-10s %s\n' "doc/" "$(du -sh doc 2>/dev/null | cut -f1)" "$desc"
  desc="工具脚本 + 智能体定义";                                  printf '  %-28s %-10s %s\n' "scripts/" "$(du -sh scripts 2>/dev/null | cut -f1)" "$desc"
  desc="板端实测日志（入库）";                                    printf '  %-28s %-10s %s\n' "logs/" "$(du -sh logs 2>/dev/null | cut -f1)" "$desc"
  desc="技能（板卡事实/构建要点/踩坑）";                          printf '  %-28s %-10s %s\n' "skills/" "$(du -sh skills 2>/dev/null | cut -f1)" "$desc"
  desc="RKNN 模型与标签";                                        printf '  %-28s %-10s %s\n' "model/" "$(du -sh model 2>/dev/null | cut -f1)" "$desc"
  desc="运行配置 + 标定文件";                                     printf '  %-28s %-10s %s\n' "config/" "$(du -sh config 2>/dev/null | cut -f1)" "$desc"
  desc="编译产物 + sysroot + 部署包（不入库）";                    printf '  %-28s %-10s %s\n' "build/" "$(du -sh build 2>/dev/null | cut -f1)" "$desc"
  desc="第三方参考工程（不入库）";                                printf '  %-28s %-10s %s\n' "reference/" "$(du -sh reference 2>/dev/null | cut -f1)" "$desc"

  head1 "关键文件"
  local f
  for f in AGENTS.md README.md CMakeLists.txt config/eye_range.yaml config/calib.yaml \
           model/yolov8.rknn model/coco_80_labels_list.txt; do
    [[ -f "$f" ]] && ok "$f" || bad "$f 缺失"
  done

  head1 "关键产物"
  for f in build/sysroot/usr/include/opencv4 \
           build/aarch64/eye_range build/aarch64/eye_range_calib build/aarch64/eye_range_sysinfo; do
    [[ -e "$f" ]] && ok "$f" || warn "$f 不存在（跑 bootstrap_sysroot.sh / build_aarch64.sh 生成）"
  done
}

cmd_tools() {
  head1 "项目工具（scripts/）"
  printf '  %-30s %s\n' "工具" "用途"
  local f
  for f in "${SCRIPT_DIR}"/*.sh "${SCRIPT_DIR}"/*.py; do
    [[ -f "$f" ]] || continue
    [[ "$(basename "$f")" == "manage.sh" ]] && continue
    printf '  %-30s %s\n' "$(basename "$f")" "$(tool_desc "$f")"
  done
  head1 "智能体（scripts/agents/）"
  printf '  %-30s %s\n' "00-orchestrator.md" "总协调：拆任务/定边界/守验收"
  for f in "${SCRIPT_DIR}"/agents/*.md; do
    [[ -f "$f" ]] || continue
    local b; b="$(basename "$f")"
    [[ "$b" == "00-orchestrator.md" || "$b" == "README.md" ]] && continue
    printf '  %-30s %s\n' "$b" "$(sed -n 's/^# 智能体：//p' "$f" | head -1)"
  done
}

cmd_check() {
  head1 "本机环境"
  local tc="/opt/toolchains/gcc-linaro-7.5.0-2019.12-x86_64_aarch64-linux-gnu"
  [[ -x "${tc}/bin/aarch64-linux-gnu-g++" ]] && ok "交叉工具链 $( "${tc}/bin/aarch64-linux-gnu-g++" -dumpversion 2>/dev/null)" || bad "交叉工具链缺失: $tc"
  [[ -d /opt/qt5-arm/lib/cmake/Qt5Widgets ]] && ok "Qt 交叉库 /opt/qt5-arm" || bad "Qt 交叉库缺失 /opt/qt5-arm"
  command -v cmake >/dev/null && ok "cmake $(cmake --version | head -1 | awk '{print $3}')" || bad "cmake 缺失"
  [[ -d build/sysroot/usr/include/opencv4 ]] && ok "交叉 sysroot（OpenCV 头）" \
    || warn "sysroot 未就绪：执行 scripts/bootstrap_sysroot.sh"
  python3 -c 'import paramiko' 2>/dev/null && ok "paramiko（板卡通信）" || bad "缺 paramiko：pip3 install paramiko"

  head1 "Git"
  if git rev-parse --git-dir >/dev/null 2>&1; then
    ok "分支 $(git branch --show-current)，提交 $(git log --oneline -1 2>/dev/null)"
    local dirty; dirty="$(git status --porcelain | wc -l)"
    [[ "$dirty" -eq 0 ]] && ok "工作区干净" || warn "有 $dirty 个未提交改动"
    printf '  %-14s %s\n' "远端" "$(git remote get-url origin 2>/dev/null || echo '未配置')"
  else
    warn "未初始化 git 仓库"
  fi

  head1 "开发板"
  if [[ -z "${BOARD_PASS:-}" ]]; then
    warn "未配置 BOARD_PASS，跳过板子检查（见 scripts/board_env.defaults.sh 顶部说明）"
  else
    local out
    out="$(timeout 25 python3 "${SCRIPT_DIR}/board.py" run \
      "uname -sr; uptime | head -1; ls /usr/lib/librknnrt.so >/dev/null && echo rknn-ok; \
       ls /dev/video0 >/dev/null && echo camera-ok" 2>&1 | tr '\n' ' ')"
    if echo "$out" | grep -q "rknn-ok"; then ok "板子在线: $out"; else bad "板子不可达: $out"; fi
  fi

  head1 "智能体总纲"
  [[ -f AGENTS.md ]] && ok "AGENTS.md（项目总纲，先读它）" || bad "AGENTS.md 缺失"
}

cmd_workflows() {
  cat <<'EOF'

== 首次接手 ==
  scripts/manage.sh overview && scripts/manage.sh check
  scripts/bootstrap_sysroot.sh            # build/sysroot 缺失时

== 改代码 → 编译 → 部署 → 验证（主线）==
  scripts/build_aarch64.sh
  scripts/deploy_board.sh
  scripts/run_board.sh sysinfo
  scripts/run_board.sh headless 20        # 最快验证链路（截图+统计写到板上 out/）
  # 无相机时（板上已有 testdata/）：--image-pair testdata/left.png testdata/right.png

== 界面 ==
  scripts/run_board.sh ui                 # 默认真全屏
  scripts/run_board.sh ui --low-power     # 供电/散热紧张时

== 标定 ==
  scripts/run_board.sh calib-capture      # 举棋盘格自动存图
  scripts/run_board.sh calib-solve        # 解算 → config/calib.yaml
  # 校验: python3 board_calib_verify.py config/calib.yaml calib_images 9 6

== 性能/稳定性 + 日志归档 ==
  APP_TIMEOUT=300 scripts/run_stress.sh borderless
  APP_TIMEOUT=180 scripts/run_stress.sh fullscreen --fullscreen
  APP_TIMEOUT=300 scripts/run_stress.sh headless --headless --duration 300
  → 产物落 logs/<时间戳>_<主题>/

== 排障 ==
  无日志硬重启   → 先查供电；board_stress_test.sh 复现取证（doc/06 第 8~10 节）
  显示/性能异常  → board_display_check.sh；run_stress.sh
  检测/测距异常  → run_board.sh headless，再看 logs/ 最近一次采样
EOF
}

cmd_logs() {
  head1 "已归档日志（logs/）"
  [[ -d logs ]] || { warn "logs/ 不存在"; return; }
  local d
  for d in logs/*/; do
    [[ -d "$d" ]] || continue
    printf '  %-40s %s\n' "$(basename "$d")" "$(sed -n 's/^# //p' "$d/summary.md" 2>/dev/null | head -1)"
  done
}

case "${1:-overview}" in
  overview)  cmd_overview ;;
  tools)     cmd_tools ;;
  check)     cmd_check ;;
  workflows) cmd_workflows ;;
  logs)      cmd_logs ;;
  *) cat <<EOF
用法: scripts/manage.sh <子命令>
  overview   项目结构、体积、关键产物
  tools      项目工具与智能体清单
  check      工具链/Qt/sysroot/git/板子连通性体检
  workflows  标准工作流命令
  logs       已归档的实测日志
（先读 AGENTS.md 了解全局规则）
EOF
     exit 2 ;;
esac
