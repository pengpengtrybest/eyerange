# scripts 目录说明

```
scripts/
├── board_env.defaults.sh        连接参数默认值（入库，不含密码）
├── board_env.sh                 本地私有凭据（**不入库**，见 .gitignore）
├── board.py                     SSH/SFTP 助手（WSL 无 sshpass，用 paramiko）
├── bootstrap_sysroot.sh         本机构建交叉编译 sysroot
├── remote_pack_sysroot.sh       在板上执行的打包脚本（由上面调用）
├── build_aarch64.sh             交叉编译
├── deploy_board.sh              打包并上传到板
├── run_board.sh                 板上运行入口（sysinfo/headless/ui/calib/raw）
├── run_stress.sh                压测 + 自动把日志取回 logs/<时间戳>_<主题>/
├── board_stress_test.sh         板端：带监控跑 eye_range（频率/温度/ADC/负载）
├── board_kiosk_test.sh          板端：停 X 用 linuxfb 跑（绕开 X 验证）
├── board_display_check.sh       板端：显示/合成器/GL 状态检查
├── board_boot_log.py            板端：解析上次启动的内核日志
├── board_calib_analyze.py       板端：标定图像对质量检查
├── board_calib_solve.py         板端：稳健双目标定（逐视角剔除离群）
├── board_calib_verify.py        板端：极线对齐自检
└── agents/                      智能体定义（协作时作为子智能体提示词使用）
```

## 首次使用：配置板卡连接

仓库里只有不含密码的默认值，真实凭据放在本地 `scripts/board_env.sh`（已在 `.gitignore` 中）：

```bash
cat > scripts/board_env.sh <<'EOF'
export BOARD_HOST=192.168.228.113
export BOARD_USER=orangepi
export BOARD_PASS=你的密码
export BOARD_ROOT=/home/orangepi/eye_range
EOF
```

也可以临时用环境变量覆盖：`BOARD_PASS=xxx scripts/run_board.sh sysinfo`。

## 常用命令

```bash
scripts/bootstrap_sysroot.sh     # 首次或板上 OpenCV 变化后
scripts/build_aarch64.sh
scripts/deploy_board.sh
scripts/run_board.sh sysinfo
scripts/run_board.sh headless 15
scripts/run_board.sh ui
```

## board.py 直接用法

```bash
python3 scripts/board.py run "uname -a"
python3 scripts/board.py put build/bundle/bin /home/orangepi/eye_range/bin
python3 scripts/board.py get /home/orangepi/eye_range/out build/board_out
```

## agents 目录

`agents/*.md` 是本工程协作时使用的子智能体定义：每个文件写清了职责范围、
输入输出、完成判据与不能越界的事。把它们作为子智能体的提示词投喂
（例如 `fork_turns=none` 只带该文件内容），可以让多个执行者并行推进
互不冲突的子任务（构建部署 / 视觉流水线 / 界面 / 板端测试）。
