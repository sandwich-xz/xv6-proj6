#!/usr/bin/env bash
# 跑 xv6 自带 usertests 回归，输出 PASS/FAIL。
#   ./scripts/test-usertests.sh riscv
set -uo pipefail
cd "$(dirname "$0")/.."

ARCH="${1:-riscv}"
case "$ARCH" in
  riscv)     TP=riscv64-linux-gnu- ;;
  loongarch) TP=loongarch64-linux-gnu- ;;
  *) echo "usage: $0 [riscv|loongarch]" >&2; exit 1 ;;
esac

cd src
make -j"$(nproc)" ARCH="$ARCH" TOOLPREFIX="$TP" kernel/kernel fs.img >/dev/null || exit 1

LOG=/tmp/usertests-$ARCH.log
{ sleep 8; echo "usertests -q"; sleep 420; printf '\001x'; } | \
  timeout 520 make ARCH="$ARCH" TOOLPREFIX="$TP" qemu > "$LOG" 2>&1

if grep -q "ALL TESTS PASSED" "$LOG"; then
  echo "PASS: usertests ($ARCH)，日志 $LOG"
  exit 0
fi
echo "FAIL: usertests ($ARCH)，看 $LOG"
grep -n -e FAILED -e panic "$LOG" | head -10
exit 1
