# Boot Sequence Analysis — NUCLEO-F446RE (Cortex-M4)

리셋 벡터부터 `main()` 진입까지 Zephyr가 실제로 무엇을 어떤 순서로 실행하는지
소스와 빌드 산출물을 직접 추적해 정리했음.

부팅 시간 측정에 앞서 **"어디를 재야 하는가"** 를 확정하는 것이 목적임.

- 대상: Zephyr v4.4.99, NUCLEO-F446RE (STM32F446RE, Cortex-M4)

---

## 1. 진입점 찾기 — 벡터 테이블

### Cortex-M의 고정 규격

전원이 인가되면 하드웨어가 자동으로 수행함.
소프트웨어가 개입할 수 없음.

```
① 플래시 시작 주소 +0  의 4바이트를 읽어 → SP (스택 포인터)
② 플래시 시작 주소 +4  의 4바이트를 읽어 → PC (프로그램 카운터)
③ 그 PC 주소부터 실행 시작
```

STM32는 플래시가 `0x08000000`에 매핑되므로 `+0` = `0x08000000`, `+4` = `0x08000004`.

### 실측

```bash
arm-zephyr-eabi-objdump -s \
  --start-address=0x08000000 --stop-address=0x08000010 \
  build/zephyr/zephyr.elf
```

결과는 아래와 같이 나왔음.

```
Contents of section rom_start:
 8000000 40110020 191c0008 953f0008 fd1b0008
```

리틀엔디안이라 바이트를 뒤집어 읽어야 함.

| 오프셋       | 원시 바이트     | 실제 값                  | 정체                                                    |
| ------------ | --------------- | ------------------------ | ------------------------------------------------------- |
| +0           | `40 11 00 20` | `0x20001140`           | 초기 스택 포인터 (`0x20…` = SRAM)                    |
| **+4** | `19 1c 00 08` | **`0x08001c19`** | **리셋 벡터** (`0x08…` = 플래시, 홀수 = Thumb) |
| +8           | `95 3f 00 08` | `0x08003f95`           | NMI 핸들러                                              |
| +12          | `fd 1b 00 08` | `0x08001bfd`           | HardFault 핸들러                                        |

### 리셋 벡터 → 함수명

Cortex-M은 Thumb 명령어만 실행하므로 분기 주소의 **비트 0은 "Thumb 상태" 표시**이고
주소의 일부가 아님. PC에 적재될 때 마스킹됨.

`0x08001c19` → 비트 0 마스킹 → `0x08001c18`

```bash
arm-zephyr-eabi-addr2line -f -e build/zephyr/zephyr.elf 0x08001c18
```

```
z_arm_reset
zephyr/arch/arm/core/cortex_m/reset.S:147
```

칩이 전원 인가 후 최초로 실행하는 함수가 `z_arm_reset`임을 확인했음.

---

## 2. 리셋 핸들러 — `reset.S`

`.S` 확장자는 어셈블리 소스.

### 실행 순서

ARM 어셈블리에서 함수 호출은 `bl`(branch with link)임.
`bl` 목록을 뽑으면 이 함수가 하는 일의 순서를 확인할 수 있음.

| 행                 | 호출                           | 내용                           |
| ------------------ | ------------------------------ | ------------------------------ |
| 109                | `soc_early_reset_hook`       | SoC별 훅 — 가장 이른 시점     |
| 126                | `arch_pm_s2ram_resume`       | Suspend-to-RAM 복귀 경로       |
| **147–148** | *(스택 설정)*                | **이후부터 C 호출 가능** |
| 163                | `soc_reset_hook`             | SoC별 훅 — 스택 설정 이후     |
| 176                | `z_arm_init_arch_hw_at_boot` | ARM 코어 초기화                |
| 191                | `z_arm_watchdog_init`        | 워치독 설정                    |
| 206                | `arch_early_memset`          | 메모리 초기화 준비             |
| **233**      | **`z_prep_c`**         | **C 코드로 진입**        |

### 스택 설정 (147–148)

```asm
ldr r0, =z_main_stack + CONFIG_MAIN_STACK_SIZE
msr msp, r0
```

- `ldr` — r0에 값 적재
- `msr` — 특수 레지스터(MSP, Main Stack Pointer)에 기록

### SoC 훅이란

**공통 코드가 호출하지만 내용은 칩 벤더가 채우는 빈 함수**임.

```
[ARM 공통 코드]  arch/arm/core/cortex_m/reset.S
     bl soc_reset_hook          "여기서 칩별 초기화를 해라"고 호출만 함.
            │                    무슨 코드인지는 모름
            ↓  (링커가 연결)
[칩별 코드]  soc/st/stm32/.../soc.c
     void soc_reset_hook(void) { /* STM32에 필요한 초기화 */ }
```

같은 이름의 함수가 벤더마다 하나씩 존재함. 실제로 검색해 보면 이렇게 나옴.

```
soc/nordic/nrf71/soc.c:235       void soc_reset_hook(void)
soc/nxp/s32/s32ze/soc.c:16       void soc_reset_hook(void)
soc/nxp/imxrt/imxrt10xx/soc.c:360 void soc_reset_hook(void)
soc/alif/ensemble/rtss/soc.c:8   void soc_reset_hook(void)
soc/st/stm32/...
```

빌드할 때 `-b nucleo_f446re` 로 보드를 지정하면 **ST 버전만 링크되고 나머지는 들어가지 않음.**
또 `#ifdef CONFIG_SOC_RESET_HOOK` 로 감싸져 있어, 훅이 필요 없는 칩은 아예 생성되지 않음.

|                  |                                                         |
| ---------------- | ------------------------------------------------------- |
| 누가 호출하나    | 아키텍처 공통 코드 (모든 Cortex-M 공유)                 |
| 누가 채우나      | 칩 벤더                                                 |
| 왜 이런 구조인가 | **공통 코드를 고치지 않고 새 칩을 지원하기 위해** |

새 칩을 Zephyr에서 동작시킨다는 것은 이 훅들을 채우고
드라이버와 보드 정의를 함께 작성하는 작업임.

### SoC 훅의 제약 차이

`soc_early_reset_hook`(109행)은 **스택 설정 전**에 호출됨.
따라서 그 안에서는 C 함수 호출과 지역변수 사용에 제약이 있고,
"RAM이 동작하려면 먼저 켜야 하는 클럭" 같은 아주 이른 하드웨어 조작만 들어감.

`soc_reset_hook`(163행)은 스택 설정 이후라 C를 자유롭게 쓸 수 있음.

**같은 SoC 훅이라도 호출 위치에 따라 쓸 수 있는 코드가 다름.**

---

## 3. C 진입 — `z_prep_c` (`arch/arm/core/cortex_m/prep_c.c`)

```c
FUNC_NORETURN void z_prep_c(void)
{
        soc_prep_hook();
        relocate_vector_table();
#if defined(CONFIG_CPU_HAS_FPU)
        z_arm_floating_point_init();
#endif
        arch_bss_zero();            /* .bss 를 0으로 */
        arch_data_copy();           /* .data 를 플래시 → RAM 복사 */
        z_arm_interrupt_init();
        z_cstart();
        CODE_UNREACHABLE;
}
```

`FUNC_NORETURN`은 커널로 넘어간 뒤 복귀하지 않는 함수라는 표시임.

### `.data` 복사와 `.bss` 클리어가 필요한 이유

`int x = 5;` 를 예시로 이해함

- 실행 중 값이 바뀔 수 있음 → **쓰기 가능한 RAM**에 있어야 함
- 그런데 RAM은 휘발성이라 전원 인가 직후에는 쓰레기값임
- 그럼 초기값 `5`는 어디서 오는가 → **플래시에 따로 저장해 두고 부팅 시 복사함**

```
[플래시]  5  ──── 부팅 시 1회 복사 ────→  [RAM] x = 5
   │                                            ↓  x = x + 5
   │  (변화 없음)                         [RAM] x = 10
   │                                            ↓  전원 OFF → RAM 소실
   └──────── 다시 복사 ──────────────→  [RAM] x = 5
```

복사는 **부팅 시 한 번, 단방향**임. 런타임에 플래시로 되돌아 쓰지 않음.
전원을 껐다 켜면 변수가 초기값으로 돌아가는 게 이 구조의 결과임.

### 섹션 분리

| 섹션                | 내용                         | 위치         | 부팅 시 처리                 |
| ------------------- | ---------------------------- | ------------ | ---------------------------- |
| `.text`           | 코드                         | 플래시       | 그대로 실행                  |
| `.rodata`         | `const` 상수               | 플래시       | 그대로 읽음                  |
| **`.data`** | 초기값이 있는 전역변수       | 플래시 + RAM | **플래시 → RAM 복사** |
| **`.bss`**  | 초기값이 없거나 0인 전역변수 | RAM만        | **0으로 채움**         |

`.bss`를 분리하는 이유는 플래시 절약임.
초기값이 0인 변수가 1000개여도 플래시에 0을 1000개 저장할 필요가 없고,
"이만큼 자리를 잡고 0으로 채워라"는 지시만 있으면 됨.

**런타임에 플래시를 쓰지 않는 이유**

| 이유 | 내용                                                          |
| ---- | ------------------------------------------------------------- |
| 속도 | 플래시 쓰기는 RAM보다 수백~수천 배 느림                       |
| 수명 | 쓰기 횟수 제한이 있음 (통상 1만~10만 회)                      |
| 단위 | 바이트 단위 수정이 불가능하고 블록 단위로 지우고 다시 써야 함 |

전원이 꺼져도 남겨야 하는 설정값 등은 프로그램이 명시적으로 플래시에 기록하는
별도 작업이며 자동으로 이루어지지 않음.

---

## 4. 커널 초기화 — `z_cstart` (`kernel/init.c:537`)

### 어떻게 여기까지 왔는지 (추적 순서)

앞 단계에서 `z_prep_c` 마지막 줄이 `z_cstart()` 호출인 것을 확인했음.
이 함수가 어디 정의되어 있는지 모르는 상태였으므로 소스 전체를 검색했음.

```bash
grep -rn "void z_cstart" zephyr/ --include="*.c" --include="*.h"
```

결과가 17건 나왔는데 대부분 `extern ... ;` 로 끝나는 선언이었고,
`;` 없이 `{` 가 이어지는 **정의는 `kernel/init.c:537` 하나**였음.

경로가 `arch/arm/` 이 아니라 `kernel/` 이라는 점에서
**아키텍처 종속 구간이 끝나고 커널 구간으로 넘어왔다**는 것을 알 수 있었음.

### 레벨이 5개인 것을 알게 된 경위

`z_cstart` 본문(537–618행)을 읽으니 `z_sys_init_run_level` 호출이 **3개**뿐이었음.

```
543   z_sys_init_run_level(INIT_LEVEL_EARLY);
570   z_sys_init_run_level(INIT_LEVEL_PRE_KERNEL_1);
574   z_sys_init_run_level(INIT_LEVEL_PRE_KERNEL_2);
```

이 함수가 무엇을 하는지 확인하려고 정의를 찾아갔더니(`kernel/init.c:209`),
내부의 `levels[]` 배열에 레벨이 **5개** 나열되어 있었음.

```c
__init_EARLY_start, __init_PRE_KERNEL_1_start, __init_PRE_KERNEL_2_start,
__init_POST_KERNEL_start, __init_APPLICATION_start,
```

**여기서 불일치가 드러남 — 배열에는 5개인데 `z_cstart`에서는 3개만 호출함.**
나머지 2개를 누가 호출하는지 찾아야 했음.

`z_cstart` 끝부분을 보면 `switch_to_main_thread(...)` 로 넘어가고,
그 경로에 `bg_thread_main` 이 있었음.

```bash
grep -n "bg_thread_main" zephyr/kernel/init.c
```

`bg_thread_main` 본문에서 나머지 두 개를 확인했음.

```
z_sys_init_run_level(INIT_LEVEL_POST_KERNEL);
soc_late_init_hook();
board_late_init_hook();
z_sys_init_run_level(INIT_LEVEL_APPLICATION);
main();
```

**배열은 레벨의 "목록"이고 실제 호출은 두 함수에 나뉘어 있었음.**

### 초기화 레벨

Zephyr는 드라이버/서브시스템 초기화를 5단계 레벨로 나누어 순차 실행함.
각 드라이버는 자신이 속할 레벨을 등록해 두고, 커널이 레벨 순서대로 호출함.

| 레벨             | 호출 위치          | 등록되는 것                                           |
| ---------------- | ------------------ | ----------------------------------------------------- |
| `EARLY`        | `z_cstart`       | 가장 이른 하드웨어                                    |
| `PRE_KERNEL_1` | `z_cstart`       | 클럭, 전원, 시리얼 — 다른 것이 의존하는 기반         |
| `PRE_KERNEL_2` | `z_cstart`       | `PRE_KERNEL_1`에 의존하는 것 (시스템 타이머 등)     |
| `POST_KERNEL`  | `bg_thread_main` | 커널 기동 후 — 스레드/세마포어를 쓸 수 있는 드라이버 |
| `APPLICATION`  | `bg_thread_main` | 하드웨어가 모두 준비된 뒤의 앱 레벨 초기화            |

**레벨을 나누는 이유는 의존성임.**
UART를 초기화하려면 클럭이 먼저 살아 있어야 함. 순서가 뒤바뀌면 부팅이 실패함.

### `z_cstart`에는 3개만 있음

```
543   z_sys_init_run_level(INIT_LEVEL_EARLY);
570   z_sys_init_run_level(INIT_LEVEL_PRE_KERNEL_1);
574   z_sys_init_run_level(INIT_LEVEL_PRE_KERNEL_2);
```

나머지 `POST_KERNEL`, `APPLICATION`은 커널이 기동된 뒤 `bg_thread_main`에서 실행됨.
스레드와 커널 객체를 사용해야 하는 초기화라 커널 기동 이후로 미뤄져 있음.

### `z_sys_init_run_level` 구현 (`kernel/init.c:209`)

```c
static void z_sys_init_run_level(enum init_level level)
{
    static const struct init_entry *levels[] = {
        __init_EARLY_start,
        __init_PRE_KERNEL_1_start,
        __init_PRE_KERNEL_2_start,
        __init_POST_KERNEL_start,
        __init_APPLICATION_start,
#ifdef CONFIG_SMP
        __init_SMP_start,
#endif
        __init_end,                 /* End marker */
    };

    for (entry = levels[level]; entry < levels[level+1]; entry++) {
        sys_trace_sys_init_enter(entry, level);
        if (dev != NULL) {
            result = do_device_init(dev);
        } else {
            result = entry->init_fn();
        }
        sys_trace_sys_init_exit(entry, level, result);
    }
}
```

- 초기화 항목들은 링커가 레벨별 섹션에 모아 배치하고, 루프는 그 구간을 순회함
- `__init_end`는 SMP의 대안이 아니라 **끝 표시(sentinel)** 임.
  루프가 `levels[level]`부터 `levels[level+1]` 직전까지 돌기 때문에
  마지막 레벨에도 "다음 시작점" 자리가 필요함. **레벨 N개 → 경계 N+1개.**
- `sys_trace_sys_init_enter` / `_exit`는 각 초기화 항목의 진입/종료 지점임.

### 커널 기동과 `main()`

커널이 실제로 기동되는 지점은 `z_cstart` 590행임.

```c
590   switch_to_main_thread(prepare_multithreading());
```

1. `prepare_multithreading()` — 스케줄러 자료구조를 세우고 main 스레드를 생성
2. `switch_to_main_thread()` — 해당 스레드로 문맥 전환.
   **이 시점부터 스케줄러가 살아 있음**
3. main 스레드 안에서 `bg_thread_main()` 실행 → `POST_KERNEL` → `APPLICATION` → `main()`

**`PRE_KERNEL`과 `POST_KERNEL`의 경계가 이 590행임.**

```
z_cstart                      (스레드 없음)
   EARLY / PRE_KERNEL_1 / PRE_KERNEL_2
   switch_to_main_thread()    ← 커널 기동. 여기부터 스레드 동작
        ↓
bg_thread_main                (main 스레드 안에서 실행)
   POST_KERNEL / APPLICATION
   main()
```

`bg_thread_main`이 커널을 기동하는 것이 아니라,
**이미 기동된 커널 위에서 도는 첫 스레드의 본문**임.

### 왜 커널 기동 전후로 레벨이 갈리는가

`POST_KERNEL` 이후로 미뤄지는 드라이버는 **커널 객체를 쓰는 드라이버**임.

- **스레드** — 독립적으로 도는 실행 흐름. 코어가 하나여도 스케줄러가 번갈아 실행시켜
  여러 작업이 동시에 도는 것처럼 동작함
- **세마포어** — "준비될 때까지 기다림"을 구현하는 동기화 수단.
  `k_sem_take()`로 스레드가 잠들고, 인터럽트가 `k_sem_give()`로 깨움.
  기다리는 동안 CPU를 양보할 수 있음

`k_sem_take()`로 잠들려면 깨워 줄 스케줄러가 있어야 하므로,
커널이 기동되기 전에는 이런 드라이버를 초기화할 수 없음.

---

## 5. 전체 경로

```
[전원 인가]
  하드웨어: 벡터 테이블 +0 → SP,  +4 → PC
        ↓
┌─ z_arm_reset ──────────────── arch/arm/core/cortex_m/reset.S
│    soc_early_reset_hook              (SoC 훅 / 스택 설정 전)
│    [스택 설정 147–148]                 이후부터 C 호출 가능
│    soc_reset_hook                    (SoC 훅)
│    z_arm_init_arch_hw_at_boot
│    z_arm_watchdog_init
│    arch_early_memset
└─── bl z_prep_c
        ↓
┌─ z_prep_c ─────────────────── arch/arm/core/cortex_m/prep_c.c
│    soc_prep_hook                     (SoC 훅)
│    relocate_vector_table
│    arch_bss_zero                      .bss → 0
│    arch_data_copy                     .data 플래시 → RAM
│    z_arm_interrupt_init
└─── z_cstart()
        ↓
┌─ z_cstart ─────────────────── kernel/init.c:537
│  ★ z_sys_init_run_level(EARLY)
│    arch_kernel_init
│    z_dummy_thread_init
│    z_device_state_init
│    soc_early_init_hook               (SoC 훅)
│    board_early_init_hook             (보드 훅)
│  ★ z_sys_init_run_level(PRE_KERNEL_1)
│  ★ z_sys_init_run_level(PRE_KERNEL_2)
└─── switch_to_main_thread(prepare_multithreading())
        ↓
┌─ bg_thread_main ───────────── kernel/init.c
│  ★ z_sys_init_run_level(POST_KERNEL)
│    soc_late_init_hook                (SoC 훅)
│    board_late_init_hook              (보드 훅)
│  ★ z_sys_init_run_level(APPLICATION)
└─── main()
```

★ = 초기화 레벨 실행 지점 (5개). 다음 단계의 계측 대상임.

**부팅 시간의 정의**: 전원 인가부터 `main()` 진입까지의 위 전체 경로.

### SoC / 보드 훅

추적 과정에서 확인된 훅은 7개였음.

```
soc_early_reset_hook   soc_reset_hook        soc_prep_hook
soc_early_init_hook    board_early_init_hook
soc_late_init_hook     board_late_init_hook
```

아키텍처 공통 코드가 부팅 경로 곳곳에 비워 둔 자리이고,
칩·보드별 초기화 코드가 여기에 들어감.
호출 위치에 따라 사용 가능한 코드에 제약이 다름(스택 설정 전/후 등).

## 다음 단계

각 초기화 레벨의 소요 시간을 측정할 예정임.

- 계측 수단: Cortex-M4의 `DWT_CYCCNT` (사이클 카운터)
- 출력 통로: UART 콘솔
- 계측 지점: 위 지도의 ★ 5개 지점
