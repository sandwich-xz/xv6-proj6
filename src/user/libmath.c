// SPDX-License-Identifier: BSD-2-Clause
//
// 共享库示例：只导出两个纯函数，方便验证 ELF 加载与重定位。
int
add(int a, int b)
{
  return a + b;
}

int
mul(int a, int b)
{
  return a * b;
}

int
fib(int n)
{
  if (n < 2)
    return n;
  return fib(n - 1) + fib(n - 2);
}
