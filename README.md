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

---

## 진행 방식

```
① 부팅 시퀀스 추적    리셋 벡터 → 스타트업 → 커널 초기화 → main()
        ↓             "어디를 재야 하는가"를 먼저 확정
② 계측 체계 구축      SYS_INIT 마커 + DWT 사이클 카운터
        ↓             Zephyr 트리를 수정하지 않고 구간별 시각 획득
③ 병목 특정           측정 → 출력 순서 관찰 → UART 전송 시간 계산
        ↓             → .config 확인 → 소스 확정
④ 제거 후 재측정      단일 변경, 다른 구간 불변으로 인과 확인
```

---

## 문서

| 문서 | 내용 |
|---|---|
| [docs/boot-sequence.md](docs/boot-sequence.md) | 벡터 테이블부터 `main()`까지 부팅 경로 추적 |
| [docs/boot-timing.md](docs/boot-timing.md) | 구간별 측정 설계, 병목 특정, 최적화와 재측정 |

---

## 구성

```
app/
├── CMakeLists.txt
├── prj.conf              # CONFIG_BOOT_BANNER=n
└── src/
    ├── main.c            # 측정 결과 출력
    └── boot_timing.c     # SYS_INIT 마커 + DWT 계측
docs/
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

# 콘솔
python -m serial.tools.miniterm /dev/tty.usbmodem* 115200
```

출력 예시

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

---

## 계측 범위

DWT 카운터를 `EARLY` 레벨에서 시작하므로
**리셋 벡터 → 스타트업 → `z_cstart` 진입 구간은 측정에 포함되지 않음.**
위 "total"은 리셋 시점이 아니라 `EARLY` 시작 기준임.

해당 구간까지 측정하려면 GPIO 토글 등 외부 계측이 필요함.

---

## 이번 조치의 성격

부팅 배너 제거는 **코드를 더 빠르게 만든 것이 아니라 불필요한 작업을 없앤 것**임.
배너를 유지해야 하는 경우의 대안은 아래와 같음.

| 방법 | 예상 효과 | 대가 |
|---|---|---|
| 배너 제거 (채택) | −5090 µs | 부팅 확인 수단 상실 |
| 보드레이트 상향 (115200 → 921600) | 약 −4400 µs | 호스트 설정 변경 필요 |
| 비동기 UART (인터럽트/DMA) | CPU 대기 제거 | 구현 복잡도 증가 |

---

## 다음

`PRE_KERNEL_1`(110 µs, 남은 시간의 75%)에 등록된 항목을 확인하고
같은 절차를 한 번 더 반복할 예정.
