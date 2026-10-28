# Aviateur lowlat-rx 패치 1: 계측 + 확인된 버그 수정

기준: OpenIPC/aviateur **v0.3.3** (devourer `800c3c8`)
목적: 수신 경로 전체(USB/무선 → WFB-ng → RTP → 디코더 → 화면)를 **시각과 함께 수치로** 남겨서,
손실이 어디서 생기는지와 끊김 복구 시간을 측정할 수 있게 하는 것.
기본 동작(수신, 디코딩, 화면 표시 방식)은 바꾸지 않았습니다.

## 바뀐 것

| 항목 | 내용 | 파일 |
|---|---|---|
| 키 파일 읽기 | `fopen(..., "r")` → `"rb"`. Windows 텍스트 모드에서 0x1A 바이트를 파일 끝으로 처리해 생기던 `Unable to read rx secret key` 오류 수정 | `src/wifi/wfb-ng/rx.cpp` |
| USB 수신 큐 설정 | devourer 라이브러리는 환경변수를 읽지 않아 upstream에서는 `DEVOURER_RX_URBS` 등이 무시됨. 이를 읽어 드라이버에 전달 | `src/wifi/devourer_env.h`, `src/wifi/wfbng_link.cpp` |
| RTP 손실 계산 | 시퀀스 번호가 65535→0으로 넘어갈 때 손실을 못 세던 문제 수정 | `src/wifi/wfbng_link.cpp` |
| 초당 통계 CSV | `logs/link_<시각>.csv` (아래 컬럼 설명) | `src/link_stats.h` |
| 링크 끊김/복구 이벤트 | 영상 패킷이 300ms 이상 안 오면 `link lost`, 다시 오면 `link restored`, 그 뒤 첫 화면 표시까지 걸린 시간 `recovery: first rendered frame N ms` | `src/link_stats.h` |
| 로그 파일 + 시각 | 모든 로그 줄 앞에 `YYYY-MM-DD HH:MM:SS.mmm`, `logs/aviateur_<시각>.log`에 저장 | `src/gui_interface.h` |
| 디코더 이벤트 | 디코딩/표시 프레임 수, 큐에서 버려진 프레임, 큐 대기 시간, 디코더 재오픈 시각 | `src/player/ffmpeg/video_player.*` |

## 실행

압축을 푼 폴더에서 배치 파일을 더블클릭합니다. (aviateur.exe를 직접 실행해도 되지만 그러면 드라이버 메시지가 저장되지 않습니다.)

| 파일 | USB 수신 큐 |
|---|---|
| `run_urbs8.bat` | 8 (드라이버 기본값, 기준) |
| `run_urbs16.bat` | 16 |
| `run_urbs32.bat` | 32 |

Key, Channel, Width는 예전처럼 Aviateur 화면에서 설정합니다. 종료는 반드시 창의 X로 하세요.

## 생성되는 파일 (`logs` 폴더)

| 파일 | 내용 |
|---|---|
| `aviateur_<시각>.log` | 앱 로그 (시각 포함). `[STATS]` 초당 요약, `[EVENT]` 끊김/복구, `[DRIVER]` 실제 적용된 USB 큐 값 |
| `link_<시각>.csv` | 초당 통계 (엑셀로 열기) |
| `console_<시각>_urbsN.txt` | 드라이버(devourer)와 WFB-ng 원본 출력. `rx.ring` USB 수신 링 상태 포함 |

시험이 끝나면 이 세 파일을 보내주시면 됩니다.

## CSV 컬럼 해석

| 컬럼 | 뜻 |
|---|---|
| `wifi_frames` | 드라이버가 넘겨준 모든 802.11 프레임 (주변 공유기 포함) |
| `video_frames` | 우리 영상 채널(link_id 7669206, port 0)의 WFB 프레임 수 = **실제로 받은 무선 패킷** |
| `wfb_session` / `wfb_data` | 세션 키 패킷 / 데이터 패킷 |
| `wfb_dec_err` | 복호화 실패 (키 불일치, 세션 키 수신 전 데이터) |
| `fec_recovered` | 무선에서 잃었지만 FEC로 복구한 패킷 |
| `lost_after_fec` | **FEC로도 복구 못 한 패킷** (화면 깨짐의 직접 원인) |
| `wfb_out_kbps` | WFB-ng가 내보낸 영상 데이터 속도 |
| `rtp_gap_pkts` | RTP 번호가 비어 있던 패킷 수 (lost_after_fec와 거의 같아야 정상) |
| `frames_decoded` / `frames_rendered` | 초당 디코딩 / 화면에 올린 프레임 수 |
| `frames_dropped_queue` | 디코딩은 됐지만 화면에 올리기 전에 더 새 프레임으로 교체된 수 |
| `queue_wait_avg_ms` / `max` | 디코딩된 프레임이 화면 갱신을 기다린 시간 |
| `rssi0_avg`, `rssi1_avg`, `rssi_min`, `snr*` | 안테나별 수신 세기 / SNR |
| `read_errors`, `send_errors`, `reopens` | 디코더 오류와 재오픈 횟수 |

## 손실 위치 판단

1. **송신 쪽 초당 송신 패킷 수**(wfb_tx 통계)와 **`video_frames`**를 같은 시각끼리 비교
   - 차이가 크면 → 무선 구간 또는 수신 USB 단계에서 손실
2. 1번에서 손실이 있을 때 `console_*.txt`의 `rx.ring` 줄을 확인
   - 링 깊이가 0으로 떨어지거나 resubmit 실패가 있으면 → **수신 USB 단계** (큐 8→16→32 비교로 확인)
   - 링은 여유가 있는데 손실이 있으면 → **무선 구간** (출력, 채널, 안테나, MCS 쪽)
3. `lost_after_fec`가 0인데 화면이 깨지면 → 디코더/표시 단계 문제

## A/B 시험 절차 (USB 큐)

같은 송신 파일(replay), 같은 위치, 채널, MCS, 출력에서 **큐 값만** 바꿔 3회 진행합니다.

1. `run_urbs8.bat` → 수신 Start → 송신 replay 실행 → 끝나면 X로 종료
2. `run_urbs16.bat` → 동일
3. `run_urbs32.bat` → 동일
4. 각 회차의 `logs` 파일 3개씩 전달

각 로그 처음의 `[DRIVER] devourer RX config: urbs=N` 줄로 실제 적용 값을 확인합니다.

## 빌드

### A. GitHub Actions (PC에 컴파일러 설치 불필요)
이 브랜치를 GitHub에 올리면 `.github/workflows/lowlat-windows.yml`이 Windows에서 빌드해
`Aviateur_lowlat_windows_x86_64` 아티팩트(zip)를 만듭니다. Actions 탭 → 해당 실행 → Artifacts에서 받습니다.

### B. Windows PC에서 직접
1. Visual Studio 2022 Build Tools (C++ 데스크톱 개발), Git, CMake
2. vcpkg
   ```
   git clone https://github.com/Microsoft/vcpkg.git
   cd vcpkg
   .\bootstrap-vcpkg.bat
   .\vcpkg install libusb ffmpeg libsodium
   set VCPKG_ROOT=<vcpkg 경로>
   ```
3. 소스
   ```
   git submodule update --init --recursive -- 3rd/json 3rd/mINI 3rd/SDL 3rd/vecgui
   git submodule update --init -- 3rd/devourer
   cmake -B build -DCMAKE_BUILD_TYPE=Release -DVECGUI_VULKAN=OFF
   cmake --build build --config Release --target aviateur
   ```
4. `build\bin\Release\aviateur.exe` 옆에 `build\bin\assets` 폴더와 vcpkg의 DLL(`installed\x64-windows\bin\*.dll`), `lowlat\*.bat`를 복사
