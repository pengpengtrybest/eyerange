#!/usr/bin/env python3
"""开发板 SSH/SFTP 助手（本机 WSL 无 sshpass，改用 paramiko）。

用法:
  board.py run  "<shell command>"          # 在板上执行命令
  board.py stream "<shell command>"        # 实时流式输出（跟踪 dmesg -w）
  board.py put  <local> <remote>           # 上传文件或目录（目录递归）
  board.py get  <remote> <local>           # 下载文件或目录（目录递归）
  board.py sudo "<shell command>"          # 通过 echo password | sudo -S 执行

连接参数来自环境变量 BOARD_HOST/BOARD_USER/BOARD_PASS/BOARD_PORT。
"""

import os
import posixpath
import stat
import sys
import warnings

if sys.version_info < (3, 8):
    sys.exit("需要 python3.8+")

warnings.filterwarnings("ignore")

try:
    import paramiko
except ImportError:  # pragma: no cover
    sys.exit("缺少 paramiko: pip3 install paramiko")


HOST = os.environ.get("BOARD_HOST", "192.168.228.113")
USER = os.environ.get("BOARD_USER", "orangepi")
# 密码不写死在代码里：从环境变量或 scripts/board_env.sh 取
PASS = os.environ.get("BOARD_PASS", "")
PORT = int(os.environ.get("BOARD_PORT", "22"))


def connect():
    if not PASS:
        sys.exit(
            "缺少板卡密码。请设置环境变量 BOARD_PASS，或创建 scripts/board_env.sh：\n"
            "  export BOARD_PASS=<密码>\n"
            "（可先执行 scripts/run_board.sh 等脚本，它们会自动加载 board_env.sh）"
        )
    client = paramiko.SSHClient()
    client.set_missing_host_key_policy(paramiko.AutoAddPolicy())
    client.connect(
        HOST,
        port=PORT,
        username=USER,
        password=PASS,
        timeout=int(os.environ.get("BOARD_TIMEOUT", "15")),
        banner_timeout=20,
        auth_timeout=20,
    )
    return client


def run(client, command, check=False):
    stdin, stdout, stderr = client.exec_command(command, timeout=None)
    stdin.close()
    out = stdout.read().decode("utf-8", "replace")
    err = stderr.read().decode("utf-8", "replace")
    code = stdout.channel.recv_exit_status()
    if out:
        sys.stdout.write(out)
    if err:
        sys.stderr.write(err)
    if check and code != 0:
        raise SystemExit(f"remote command failed (rc={code}): {command}")
    return code


def stream(client, command):
    """逐行输出远端命令的输出（用于跟踪 dmesg -w 这类长时间输出）。"""
    stdin, stdout, stderr = client.exec_command(command, timeout=None)
    stdin.close()
    try:
        for line in iter(stdout.readline, ""):
            sys.stdout.write(line)
            sys.stdout.flush()
    except Exception as exc:  # 板子重启/断电时连接会断
        sys.stderr.write(f"\n[stream 中断: {type(exc).__name__}]\n")
    return 0


def _put_dir(sftp, local, remote):
    _mkdirs(sftp, remote)
    for name in sorted(os.listdir(local)):
        lp = os.path.join(local, name)
        rp = posixpath.join(remote, name)
        if os.path.isdir(lp):
            _put_dir(sftp, lp, rp)
        else:
            sftp.put(lp, rp)


def _mkdirs(sftp, remote):
    parts = remote.split("/")
    cur = ""
    for part in parts:
        if not part:
            continue
        cur += "/" + part
        try:
            sftp.stat(cur)
        except IOError:
            sftp.mkdir(cur)


def put(local, remote):
    client = connect()
    try:
        sftp = client.open_sftp()
        if os.path.isdir(local):
            _put_dir(sftp, local, remote)
        else:
            sftp.put(local, remote)
        sftp.close()
    finally:
        client.close()


def _get_dir(sftp, remote, local):
    os.makedirs(local, exist_ok=True)
    for entry in sftp.listdir_attr(remote):
        rp = posixpath.join(remote, entry.filename)
        lp = os.path.join(local, entry.filename)
        if stat.S_ISDIR(entry.st_mode):
            _get_dir(sftp, rp, lp)
        else:
            sftp.get(rp, lp)


def get(remote, local):
    client = connect()
    try:
        sftp = client.open_sftp()
        try:
            is_dir = stat.S_ISDIR(sftp.stat(remote).st_mode)
        except IOError as exc:
            sys.exit(f"远端路径不存在: {remote} ({exc})")
        if is_dir:
            _get_dir(sftp, remote, local)
        else:
            parent = os.path.dirname(local)
            if parent:
                os.makedirs(parent, exist_ok=True)
            sftp.get(remote, local)
        sftp.close()
    finally:
        client.close()


def main(argv):
    if len(argv) < 2:
        sys.exit(__doc__)
    action = argv[1]
    if action == "run":
        client = connect()
        try:
            return run(client, argv[2])
        finally:
            client.close()
    if action == "stream":
        client = connect()
        try:
            return stream(client, argv[2])
        finally:
            client.close()
    if action == "sudo":
        client = connect()
        try:
            return run(client, f"echo {PASS} | sudo -S bash -lc {shell_quote(argv[2])}")
        finally:
            client.close()
    if action == "put":
        put(argv[2], argv[3])
        return 0
    if action == "get":
        get(argv[2], argv[3])
        return 0
    sys.exit(f"未知子命令: {action}")


def shell_quote(text):
    return "'" + text.replace("'", "'\\''") + "'"


if __name__ == "__main__":
    try:
        sys.exit(main(sys.argv))
    except KeyboardInterrupt:
        sys.exit(130)
