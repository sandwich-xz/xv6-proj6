#!/usr/bin/env bash
# 构建指定架构的内核与文件系统镜像。
#   ./scripts/build.sh riscv
#   ./scripts/build.sh loongarch
set -euo pipefail
cd "$(dirname "$0")/.."

ARCH="${1:-riscv}"
case "$ARCH" in
  riscv)     TP=riscv64-linux-gnu- ;;
  loongarch) TP=loongarch64-linux-gnu- ;;
  *) echo "usage: $0 [riscv|loongarch]" >&2; exit 1 ;;
esac

cd src
make clean >/dev/null 2>&1 || true
make -j"$(nproc)" ARCH="$ARCH" TOOLPREFIX="$TP" kernel/kernel fs.img
echo "built: src/kernel/kernel ($ARCH)"
