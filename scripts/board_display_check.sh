#!/usr/bin/env bash
# 检查开发板显示设置，特别是可能导致“全屏把系统拖死”的 xfwm4 合成器。
#
# 用法（在板子上执行）: ./board_display_check.sh [check|fix|restore]
#   fix      关闭合成器（仅 --fullscreen 真全屏模式需要）
#   restore  恢复合成器
set -uo pipefail

export DISPLAY="${DISPLAY:-:0}"
export XAUTHORITY="${XAUTHORITY:-/home/${USER}/.Xauthority}"

action="${1:-check}"
case "$action" in
  fix)
    xfconf-query -c xfwm4 -p /general/use_compositing -s false
    echo "已关闭 xfwm4 合成器"
    ;;
  restore)
    xfconf-query -c xfwm4 -p /general/use_compositing -s true
    echo "已恢复 xfwm4 合成器"
    ;;
esac

echo "--- 屏幕分辨率:"
xwininfo -root 2>/dev/null | grep -E "Width|Height"
echo "--- xfwm4 合成器 (true=开启):"
xfconf-query -c xfwm4 -p /general/use_compositing 2>&1
echo "--- X 扩展:"
xdpyinfo 2>/dev/null | grep -iE '^ +(Composite|DAMAGE|GLX|RENDER)' | head -4
echo "--- GL 驱动 (libGL 报 rockchip 失败 => 纯软件渲染):"
glxinfo 2>/dev/null | head -3 || echo "  (glxinfo 未安装)"
