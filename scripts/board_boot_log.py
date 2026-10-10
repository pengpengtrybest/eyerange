#!/usr/bin/env python3
"""打印开发板“上一次启动”结束时最后若干行内核日志，用于定位异常重启。

用法: python3 board_boot_log.py <kern.log 路径> [尾部行数]

先在有 sudo 的环境里把日志复制出来再跑：
  sudo cp /var/log/kern.log /tmp/kern_prev.log && sudo chmod 644 /tmp/kern_prev.log
"""

import re
import sys


def main():
    path = sys.argv[1] if len(sys.argv) > 1 else "/tmp/kern_prev.log"
    tail_n = int(sys.argv[2]) if len(sys.argv) > 2 else 60
    with open(path, "r", errors="replace") as handle:
        lines = handle.read().splitlines()
    boots = [i for i, line in enumerate(lines) if "Linux version" in line]
    print(f"日志共 {len(lines)} 行，发现 {len(boots)} 次启动标记，位置 {boots[-3:]}")
    if len(boots) >= 2:
        prev_start, prev_end = boots[-2], boots[-1]
        print(f"--- 上一次启动(第 {prev_start}~{prev_end} 行)末尾 {tail_n} 行 ---")
        for line in lines[max(prev_start, prev_end - tail_n):prev_end]:
            print(line)
        pattern = re.compile(
            r"panic|Oops|BUG:|thermal|throttl|watchdog|undervolt|ocp|over-current|SError|"
            r"I/O error|npu|rknn|reset|reboot|shutdown",
            re.IGNORECASE,
        )
        hits = [line for line in lines[prev_start:prev_end] if pattern.search(line)]
        print(f"--- 上次启动中命中的可疑行 ({len(hits)}) ---")
        for line in hits[-40:]:
            print(line)
    else:
        print("只有一次启动记录，直接打印最后几行：")
        for line in lines[-tail_n:]:
            print(line)


if __name__ == "__main__":
    main()
