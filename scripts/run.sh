#!/usr/bin/env bash
# 启动 QEMU（构建由 build.sh 负责）。
#   ./scripts/run.sh riscv
#   ./scripts/run.sh loongarch
set -euo pipefail
cd "$(dirname "$0")/.."

ARCH="${1:-riscv}"
case "$ARCH" in
  riscv)     TP=riscv64-linux-gnu- ;;
  loongarch) TP=loongarch64-linux-gnu- ;;
  *) echo "usage: $0 [riscv|loongarch]" >&2; exit 1 ;;
esac

cd src
exec make ARCH="$ARCH" TOOLPREFIX="$TP" qemu
