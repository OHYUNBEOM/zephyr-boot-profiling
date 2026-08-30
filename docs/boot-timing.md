# Boot Time Measurement & Optimization — NUCLEO-F446RE

부팅 구간별 소요 시간을 측정하고, 병목을 특정해 제거한 기록임.

`boot-sequence.md`에서 파악한 초기화 레벨 5개를 계측 지점으로 삼았음.

- 대상: Zephyr v4.4.99, NUCLEO-F446RE (STM32F446RE, Cortex-M4 @ 96 MHz)
- 계측: Cortex-M4 내장 DWT 사이클 카운터
- 출력: UART 콘솔 (115200 8N1)

---

## 결과 요약

| 구간                  |             Before |             After |                       변화 |
| --------------------- | -----------------: | ----------------: | -------------------------: |
| EARLY                 |              4 µs |             4 µs |                         — |
| PRE_KERNEL_1          |            110 µs |           110 µs |                         — |
| PRE_KERNEL_2          |             23 µs |            23 µs |                         — |
| POST_KERNEL           |              6 µs |             6 µs |                         — |
| **APPLICATION** | **5091 µs** |   **2 µs** |       **−5089 µs** |
| **total**       | **5236 µs** | **146 µs** | **−97.2% (35.9배)** |

병목은 `APPLICATION` 구간 하나였고, 원인은 **부팅 배너를 UART로 출력하며 대기하는 시간**이었음.
조치는 `CONFIG_BOOT_BANNER=n` 한 줄.

---

## 1. 측정 설계

### 요구사항

- Zephyr 소스 트리를 수정하지 않을 것 (애플리케이션은 out-of-tree로 유지)
- 초기화 레벨 5개 각각의 시작 시각을 얻을 것

### 방법 — `SYS_INIT` 으로 마커를 끼워 넣기

Zephyr는 각 초기화 레벨에 등록된 항목들을 순서대로 실행함
(`z_sys_init_run_level`, `boot-sequence.md` 4장 참조).

여기에 **우선순위 0(해당 레벨에서 가장 먼저)으로 자체 마커를 등록**하면
Zephyr 코드를 건드리지 않고 각 레벨의 시작 시각을 얻을 수 있었음.

```c
SYS_INIT(mark_early, EARLY,        0);
SYS_INIT(mark_pre1,  PRE_KERNEL_1, 0);
SYS_INIT(mark_pre2,  PRE_KERNEL_2, 0);
SYS_INIT(mark_post,  POST_KERNEL,  0);
SYS_INIT(mark_app,   APPLICATION,  0);
```

`SYS_INIT` 매크로는 함수를 호출하는 것이 아니라 `struct init_entry` 하나를
**링커의 `initlevel` 섹션에 배치**함. 커널은 부팅 중 그 섹션을 순회하며 등록된 항목을 호출함.

```
$ arm-zephyr-eabi-objdump -h build/zephyr/zephyr.elf
  4 initlevel     000000a0  08004fdc  ...
```

즉 등록을 요청하는 코드가 따로 있는 것이 아니라,
**빌드 시점에 링커가 목록에 끼워 넣는 구조**임. 이 덕분에 out-of-tree 애플리케이션이 부팅 과정에 개입할 수 있음.

### 계측 수단 — DWT 사이클 카운터

Cortex-M4의 DWT(Data Watchpoint and Trace) 유닛에는 CPU 사이클을 세는 32비트 카운터가 있음.
96 MHz 기준 1사이클 ≈ 10.4 ns.

```c
#define DEMCR      (*(volatile uint32_t *)0xE000EDFC)
#define DWT_CTRL   (*(volatile uint32_t *)0xE0001000)
#define DWT_CYCCNT (*(volatile uint32_t *)0xE0001004)

DEMCR |= (1U << 24);        /* TRCENA — 트레이스 블록을 먼저 켜야 DWT 접근 가능 */
DWT_CYCCNT = 0;
DWT_CTRL  |= (1U << 0);     /* CYCCNTENA */
```

`CONFIG_CPU_CORTEX_M_HAS_DWT=y` 로 지원 여부를 확인했음.

### 계측 범위의 한계

DWT를 `EARLY` 레벨에서 켜므로 **리셋 벡터 → 스타트업 → `z_cstart` 진입 구간은 측정되지 않음.**
이 문서의 "total"은 리셋 시점이 아니라 `EARLY` 시작 기준임.

---

## 2. 구현

### 파일 구성

| 파일                      | 역할                                    |
| ------------------------- | --------------------------------------- |
| `app/CMakeLists.txt`    | 빌드에 포함할 소스 지정                 |
| `app/prj.conf`          | Kconfig 설정                            |
| `app/src/boot_timing.c` | 각 레벨에 마커 등록, 시각을 배열에 기록 |
| `app/src/main.c`        | 배열을 읽어 표로 출력                   |

`boot_timing.c`가 `uint32_t boot_t[6]` 를 정의하고, `main.c`는 `extern` 선언으로 참조함.
두 파일은 서로의 위치를 알 필요가 없고 링커가 연결함.

### 실행 순서

```
EARLY        → mark_early()  → boot_t[0]     (여기서 DWT 시작, t=0)
PRE_KERNEL_1 → mark_pre1()   → boot_t[1]
PRE_KERNEL_2 → mark_pre2()   → boot_t[2]
POST_KERNEL  → mark_post()   → boot_t[3]
APPLICATION  → mark_app()    → boot_t[4]
main()                       → boot_t[5], 표 출력
```

`main()`이 마지막에 실행되므로 그 시점에는 배열이 모두 채워져 있음.
각 구간의 소요 시간은 인접한 두 값의 차이로 계산함.

---

## 3. 1차 측정 결과

```
===== boot timing (CPU 96000000 Hz) =====
EARLY               465 cyc         4 us
PRE_KERNEL_1      10628 cyc       110 us
PRE_KERNEL_2       2217 cyc        23 us
POST_KERNEL         579 cyc         6 us
APPLICATION      488812 cyc      5091 us
----------------------------------
total            502701 cyc      5236 us
==================================
```

**`APPLICATION` 한 구간이 전체의 97%.** 나머지 네 구간을 모두 합쳐도 143 µs인데
`APPLICATION`만 5091 µs로 약 35배임.

측정 전 예상은 클럭·시리얼 초기화가 있는 `PRE_KERNEL_1`이 가장 클 것이었으나 빗나갔음.

---

## 4. 원인 추적

### ① 관찰 — 출력 순서

콘솔 출력에서 부팅 배너가 측정 표보다 **먼저** 나왔음.

```
*** Booting Zephyr OS build v4.4.0-13404-gceb28342befb ***    ← 먼저

===== boot timing ... =====                                    ← main() 안에서 출력
```

측정 표는 `main()` 안에서 출력하므로, 배너는 `main()` 진입 **전에** 나간 것임.

### ② 계산 — UART 전송 시간

- 115200 bps, 8N1 → 문자당 10비트
- 초당 문자 수 = 115200 / 10 = 11,520
- 문자당 = 약 86.8 µs

배너 문자 수 × 86.8 µs 가 5091 µs 에 근접함.
**연산이 아니라 UART 전송을 대기**중이라고 판단되었음.

### ③ 설정 확인

```bash
$ grep -i "banner\|boot_delay" build/zephyr/.config
CONFIG_BOOT_BANNER=y
CONFIG_BOOT_BANNER_STRING="Booting Zephyr OS build"
CONFIG_BOOT_DELAY=0
```

### ④ 소스 확정

```bash
$ grep -rn "BOOT_BANNER_STRING" --include="*.c" zephyr/
lib/os/boot_banner.c:46:  printk("*** " CONFIG_BOOT_BANNER_STRING " " BANNER_VERSION ... " ***\n");
```

```c
/* lib/os/boot_banner.c */
SYS_INIT(boot_banner, APPLICATION, 0);
```

**배너가 자체 마커와 같은 레벨·같은 우선순위 0으로 등록되어 있었음.**
그래서 정확히 `APPLICATION` 구간에 계상된 것임.

### 검색 시 유의점

화면에 보이는 `"Booting Zephyr OS build"` 문자열로 `.c` 파일을 검색하면 찾을 수 없음.
소스에는 매크로 이름(`CONFIG_BOOT_BANNER_STRING`)만 있고
문자열 자체는 Kconfig 기본값에 정의되어 있기 때문임.

**기능의 on/off 스위치를 찾는 목적이라면 `.config`부터 검색하는 편이 빠름.**
소스를 먼저 찾아도 결국 `CONFIG_...` 로 되돌아오게 됨.

---

## 5. 조치와 재측정

`app/prj.conf` 에 한 줄 추가.

```
CONFIG_BOOT_BANNER=n
```

```
===== boot timing (CPU 96000000 Hz) =====
EARLY               465 cyc         4 us
PRE_KERNEL_1      10580 cyc       110 us
PRE_KERNEL_2       2221 cyc        23 us
POST_KERNEL         579 cyc         6 us
APPLICATION         233 cyc         2 us
----------------------------------
total             14078 cyc       146 us
==================================
```

`APPLICATION` 488812 → 233 사이클. **다른 네 구간은 오차 범위 내에서 동일**하므로
변경의 영향이 해당 구간에만 국한되었음을 확인할 수 있음.

---

## 6. 평가

### 무엇을 한 것인가

이 조치는 **코드를 더 빠르게 만든 것이 아니라 불필요한 작업을 제거한 것**임.
부팅 최적화에서 흔히 쓰이는 접근이지만, 성격을 구분해 둘 필요가 있음.

| 유형                           | 방법                      |
| ------------------------------ | ------------------------- |
| 같은 일을 더 빠르게            | 알고리즘/구현 개선        |
| **불필요한 일을 없애기** | **이번 조치**       |
| 순서/병렬화                    | 실행 순서 조정, 동시 실행 |

### 측정의 의미

부팅 시간의 97%가 **연산이 아니라 동기 I/O 대기**였음.
CPU 성능을 높이거나 코드를 최적화해도 이 시간은 줄지 않았을 것임.

측정하기 전에는 `PRE_KERNEL_1`을 병목으로 예상했고, 그 예상은 틀렸음.
**구간을 나눠 실측하지 않았다면 엉뚱한 곳을 최적화했을 것임.**

---

## 다음

배너 제거 후 남은 146 µs 의 구성.

| 구간                   |           µs |           비중 |
| ---------------------- | ------------: | -------------: |
| EARLY                  |             4 |            3 % |
| **PRE_KERNEL_1** | **110** | **75 %** |
| PRE_KERNEL_2           |            23 |           16 % |
| POST_KERNEL            |             6 |            4 % |
| APPLICATION            |             2 |            2 % |

`PRE_KERNEL_1` 이 다음 목표임. 해당 레벨에 등록된 항목은 링커 맵과 심볼 경계로 확인할 수 있다고 함.

```bash
$ arm-zephyr-eabi-nm -n build/zephyr/zephyr.elf | grep "__init_.*_start"
08004eac R __init_EARLY_start
08004eb4 R __init_PRE_KERNEL_1_start
08004f44 R __init_PRE_KERNEL_2_start
...
```

`0x08004eb4` ~ `0x08004f44` 구간(144바이트)에 `PRE_KERNEL_1` 항목들이 배치되어 있음.
`zephyr.map` 에서 해당 섹션을 보면 등록된 심볼 이름을 직접 확인할 수 있음.
