/*
 * 부팅 구간별 소요 시간 측정
 *
 * Zephyr 트리를 수정하지 않고 SYS_INIT 으로 각 초기화 레벨에
 * 우선순위 0(그 레벨에서 가장 먼저)으로 마커를 등록해 시각을 기록한다.
 * 계측은 Cortex-M4 내장 DWT 사이클 카운터를 사용한다.
 */

#include <stdint.h>
#include <zephyr/init.h>
#include <zephyr/kernel.h>

/* ARMv7-M 디버그 블록 레지스터 */
#define DEMCR (*(volatile uint32_t *)0xE000EDFC) /* 디버그 예외/모니터 제어 */
#define DWT_CTRL (*(volatile uint32_t *)0xE0001000)   /* DWT 제어 */
#define DWT_CYCCNT (*(volatile uint32_t *)0xE0001004) /* 사이클 카운터 */

#define DEMCR_TRCENA (1U << 24)  /* 트레이스 블록 전체 활성화 */
#define DWT_CTRL_CYCEN (1U << 0) /* 사이클 카운터 활성화 */

/* 각 구간 진입 시각 (사이클) */
uint32_t boot_t[6];

enum { T_EARLY, T_PRE1, T_PRE2, T_POST, T_APP, T_MAIN };

static void dwt_start(void) {
  DEMCR |= DEMCR_TRCENA;      /* 먼저 트레이스 블록을 켜야 DWT 접근 가능 */
  DWT_CYCCNT = 0;             /* 카운터 0으로 */
  DWT_CTRL |= DWT_CTRL_CYCEN; /* 카운트 시작 */
}

static int mark_early(void) {
  dwt_start(); /* 여기가 t=0 */
  boot_t[T_EARLY] = DWT_CYCCNT;
  return 0;
}

static int mark_pre1(void) {
  boot_t[T_PRE1] = DWT_CYCCNT;
  return 0;
}
static int mark_pre2(void) {
  boot_t[T_PRE2] = DWT_CYCCNT;
  return 0;
}
static int mark_post(void) {
  boot_t[T_POST] = DWT_CYCCNT;
  return 0;
}
static int mark_app(void) {
  boot_t[T_APP] = DWT_CYCCNT;
  return 0;
}

/* 각 레벨의 우선순위 0 = 그 레벨에서 가장 먼저 실행 */
SYS_INIT(mark_early, EARLY, 0);
SYS_INIT(mark_pre1, PRE_KERNEL_1, 0);
SYS_INIT(mark_pre2, PRE_KERNEL_2, 0);
SYS_INIT(mark_post, POST_KERNEL, 0);
SYS_INIT(mark_app, APPLICATION, 0);