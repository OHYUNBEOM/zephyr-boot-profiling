#include <stdint.h>
#include <zephyr/kernel.h>

extern uint32_t boot_t[6];
enum { T_EARLY, T_PRE1, T_PRE2, T_POST, T_APP, T_MAIN };

#define CPU_HZ CONFIG_SYS_CLOCK_HW_CYCLES_PER_SEC

/* 사이클 → 마이크로초 */
static uint32_t to_us(uint32_t cyc) {
  return (uint32_t)((uint64_t)cyc * 1000000U / CPU_HZ);
}

static void report(const char *name, uint32_t from, uint32_t to) {
  uint32_t d = to - from;
  printk("%-14s %8u cyc  %8u us\n", name, d, to_us(d));
}

int main(void) {
  boot_t[T_MAIN] = (*(volatile uint32_t *)0xE0001004);

  printk("\n===== boot timing (CPU %u Hz) =====\n", CPU_HZ);
  report("EARLY", boot_t[T_EARLY], boot_t[T_PRE1]);
  report("PRE_KERNEL_1", boot_t[T_PRE1], boot_t[T_PRE2]);
  report("PRE_KERNEL_2", boot_t[T_PRE2], boot_t[T_POST]);
  report("POST_KERNEL", boot_t[T_POST], boot_t[T_APP]);
  report("APPLICATION", boot_t[T_APP], boot_t[T_MAIN]);
  printk("----------------------------------\n");
  report("total", boot_t[T_EARLY], boot_t[T_MAIN]);
  printk("==================================\n");

  return 0;
}