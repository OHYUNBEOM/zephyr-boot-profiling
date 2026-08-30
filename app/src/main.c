#include <zephyr/kernel.h>
/*
 * 잘못된 주소 접근 실험 — NUCLEO-F446RE (Cortex-M4)
 *
 * CPU가 메모리에 접근하는 세 가지 방식(store / load / fetch)이
 * 잘못된 주소에서 각각 어떻게 동작하는지 확인한다.
 *
 * 1차 실험: 0xDEADBEEF (비트0 = 1, Thumb)
 * 2차 실험: 0xDEADC0DE (비트0 = 0) — Thumb 비트가 fault 종류를 바꾸는지 검증
 */

int main(void) {
  printk("boot-profiling app alive\n");
  k_msleep(3000);

  //[1] 쓰기(store) — 결과: 통과.
  // Cortex-M은 store를 쓰기 버퍼에 넣고 결과를 기다리지 않는다.
  // 잘못된 주소여도 CPU는 그냥 다음 명령으로 넘어간다.
  printk("[1] write to 0xDEADC0DE\n");
  *(volatile unsigned int *)0xDEADC0DE = 42;
  printk("    -> survived\n");

  //[2] 읽기(load) — 결과: 통과, value=0.
  // 가설은 "CPU가 값을 기다리므로 즉시 fault"였으나 틀렸다.
  // STM32F446의 이 영역(FMC 미활성)은 버스 에러 대신 0을 반환한다.
  //-> 분명 잘못된 값을 읽는데 가짜 값을 줘서 fault가 발생하지 않았다,
  // MCU디버깅시 이런 이슈가 발생한다고 한다.
  printk("[2] read from 0xDEADC0DE\n");
  volatile unsigned int v = *(volatile unsigned int *)0xDEADC0DE;
  printk("    -> survived, value=%u\n", v);

  //[3] 점프(fetch) — 결과: MPU FAULT / Instruction Access Violation.
  // 명령어 인출이 T비트 검사보다 먼저라 MPU fault가 선점한다.
  // PC에는 0xdeadc0de가 그대로 찍힌다 (이미 짝수라 마스킹해도 변화 없음).
  // LR(0x0800123f)을 마스킹한 0x0800123e를 addr2line에 넣으면 main.c:21.
  //-> LR은 "복귀 주소"이므로 범인은 그 바로 윗줄
  printk("[3] jump to 0xDEADC0DE\n");
  ((void (*)(void))0xDEADC0DE)();

  printk("this line never runs\n");
  return 0;
}