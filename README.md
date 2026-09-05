# zephyr-boot-profiling

Zephyr RTOS 부팅 시퀀스를 소스 수준에서 추적하고,
구간별 소요 시간을 실측해 병목을 제거한 기록.

- **보드** NUCLEO-F446RE (STM32F446RE, Cortex-M4 @ 96 MHz)
- **RTOS** Zephyr v4.4.99
- **호스트** macOS (Apple Silicon), Zephyr SDK 1.0.1

---

## 결과

부팅 시간(`EARLY` → `main()` 진입) **5236 µs → 146 µs, 97.2% 단축**

| 구간 | Before | After |
|---|---:|---:|
| EARLY | 4 µs | 4 µs |
| PRE_KERNEL_1 | 110 µs | 110 µs |
| PRE_KERNEL_2 | 23 µs | 23 µs |
| POST_KERNEL | 6 µs | 6 µs |
| **APPLICATION** | **5091 µs** | **2 µs** |
| **total** | **5236 µs** | **146 µs** |

병목은 `APPLICATION` 구간 하나였고, 원인은 **부팅 배너를 UART로 출력하며 대기하는 시간**이었음.
전체의 97%가 연산이 아니라 동기 I/O였음.

측정 전 예상은 클럭·시리얼 초기화가 있는 `PRE_KERNEL_1`이 병목일 것이었고, 그 예상은 틀렸음.

배너를 유지해야 한다면 — **보드레이트 115200 → 921600 으로 `APPLICATION` 5091 → 635 µs (정확히 1/8), total 780 µs.**
측정 전에 적어둔 가설(약 640 µs)과 5 µs 차이. 배너 시간이 순수 UART 전송 시간임을 확인함.

| 조건 | APPLICATION | total |
|---|---:|---:|
| 배너 유지, 115200 | 5091 µs | 5236 µs |
| 배너 제거 | 2 µs | 146 µs |
| **배너 유지, 921600** | **635 µs** | **780 µs** |

---

## 진행 방식

```
① 디버깅 경로 확보    크래시 덤프 → LR → addr2line → 소스 위치
        ↓             문제가 생겼을 때 쓸 도구를 먼저 갖춤
② 부팅 시퀀스 추적    리셋 벡터 → 스타트업 → 커널 초기화 → main()
        ↓             "어디를 재야 하는가"를 확정
③ 계측 체계 구축      SYS_INIT 마커 + DWT 사이클 카운터
        ↓             Zephyr 트리를 수정하지 않고 구간별 시각 획득
④ 병목 특정           측정 → 출력 순서 관찰 → UART 전송 시간 계산
        ↓             → .config 확인 → 소스 확정
⑤ 제거 후 재측정      단일 변경, 다른 구간 불변으로 인과 확인
        ↓
⑥ 대안 실측           가설 기록 → 보드레이트 8배 → 정확히 1/8 확인
```

진행 중 **예측이 세 번 빗나갔고**, 그때마다 이유를 확인하고 모델을 고쳤음.
각 문서에 예측과 실제를 함께 남겨 두었음.

---

## 문서

| 문서 | 내용 |
|---|---|
| [docs/fault-analysis.md](docs/fault-analysis.md) | 크래시 덤프에서 소스 위치를 특정하는 경로. 잘못된 주소 접근 3종 실험 |
| [docs/boot-sequence.md](docs/boot-sequence.md) | 벡터 테이블부터 `main()`까지 부팅 경로 추적 |
| [docs/boot-timing.md](docs/boot-timing.md) | 구간별 측정 설계, 병목 특정, 최적화와 재측정 |

---

## 구성

```
app/
├── CMakeLists.txt
├── prj.conf              # CONFIG_EXTRA_EXCEPTION_INFO=y
├── boards/
│   └── nucleo_f446re.overlay   # 콘솔 UART 921600 (Device Tree overlay)
└── src/
    ├── main.c            # 측정 결과 출력
    └── boot_timing.c     # SYS_INIT 마커 + DWT 계측
docs/
├── fault-analysis.md
├── boot-sequence.md
└── boot-timing.md
```

애플리케이션은 **out-of-tree** 구조로, Zephyr 트리를 수정하지 않음.

---

## 빌드 및 실행

```bash
# 빌드
west build -p always -b nucleo_f446re <이 저장소>/app

# 굽기 (ST-LINK / SWD)
west flash -r openocd

# 콘솔 (현재 overlay 기준 921600. overlay를 빼면 115200)
python -m serial.tools.miniterm /dev/tty.usbmodem* 921600
```

출력 예시 (현재 구성: 배너 유지 + 921600)

```
*** Booting Zephyr OS build v4.4.0-13404-gceb28342befb ***

===== boot timing (CPU 96000000 Hz) =====
EARLY               465 cyc         4 us
PRE_KERNEL_1      10620 cyc       110 us
PRE_KERNEL_2       2217 cyc        23 us
POST_KERNEL         579 cyc         6 us
APPLICATION       61022 cyc       635 us
----------------------------------
total             74903 cyc       780 us
==================================
```

`prj.conf`에 `CONFIG_BOOT_BANNER=n`을 넣으면 `APPLICATION` 2 µs / total 146 µs (5장 결과).

---

## 계측 범위

DWT 카운터를 `EARLY` 레벨에서 시작하므로
**리셋 벡터 → 스타트업 → `z_cstart` 진입 구간은 측정에 포함되지 않음.**
위 "total"은 리셋 시점이 아니라 `EARLY` 시작 기준임.

해당 구간까지 측정하려면 GPIO 토글 등 외부 계측이 필요함.

---

## 이번 조치의 성격

부팅 배너 제거는 **코드를 더 빠르게 만든 것이 아니라 불필요한 작업을 없앤 것**임.

배너를 유지해야 한다면 다른 접근이 필요함. 두 번째는 실측했고, 세 번째는 **시도하지 않았음.**

| 방법 | 상태 | 효과 | 대가 |
|---|---|---|---|
| 배너 제거 | **적용** | **실측 −5089 µs** | 부팅 확인 수단 상실 |
| 보드레이트 상향 | **실측** | **5091 → 635 µs (1/8)** | 호스트 설정 변경 |
| 비동기 UART | 미시도 | 미검증 | 구현 복잡도 증가 |

- **배너 제거** — `CONFIG_BOOT_BANNER=n`. APPLICATION 구간 5091 → 2 µs.
- **보드레이트 상향** (115200 → 921600) — Device Tree overlay 한 개. 측정 전 가설 약 640 µs, 실측 635 µs.
  ST-LINK VCP는 921600을 수용했고, 전송 외 오버헤드는 측정 한계 내 0 (`docs/boot-timing.md` 7장).
- **비동기 UART** (인터럽트/DMA) — CPU가 전송 완료를 기다리지 않게 되므로 대기가 사라진다는 개념 수준의 판단.

> 배너 제거 효과는 **먼저 측정하고 나서 알게 된 값**이지 사전에 예측한 값이 아님.
> 측정 전 예상은 `PRE_KERNEL_1`이 병목이라는 것이었고, 그 예상은 틀렸음.
> 보드레이트 실험은 반대로 **가설을 먼저 기록하고** 측정해 맞았음. 두 실험의 순서가 다르다는 점을 문서에 그대로 남김.

---

## 다음

`PRE_KERNEL_1`(110 µs — 배너 제거 기준 146 µs의 75%, 배너 유지 + 921600 기준 780 µs의 14%)에
등록된 항목을 확인하고 같은 절차를 한 번 더 반복할 예정.
