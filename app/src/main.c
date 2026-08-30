#include <zephyr/kernel.h>

int main(void) {
  printk("boot-profiling app alive\n");
  k_msleep(3000);

  /* 1) 쓰기 — 버퍼링되어 fault가 안 날 수 있음 */
  printk("[1] write to 0xDEADBEEF\n");
  *(volatile unsigned int *)0xDEADBEEF = 42;
  printk("    -> survived\n");

  /* 2) 읽기 — CPU가 값을 기다리므로 에러가 즉시 return */
  printk("[2] read from 0xDEADBEEF\n");
  volatile unsigned int v = *(volatile unsigned int *)0xDEADBEEF;
  printk("    -> survived, value=%u\n", v);

  /* 3) 잘못된 주소로 점프 — 반드시 fault */
  printk("[3] jump to 0xDEADBEEF\n");
  ((void (*)(void))0xDEADBEEF)();

  printk("this line never runs\n");
  return 0;
}