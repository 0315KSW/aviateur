# 수신기 패치 1 상세 변경 내역

| 항목 | 내용 |
|---|---|
| 저장소 / 브랜치 | https://github.com/0315KSW/aviateur, `receiver-lowlat` |
| 코드 커밋 | `0d1ab0d` (전체 diff: https://github.com/0315KSW/aviateur/commit/0d1ab0d) |
| 기준 버전 | OpenIPC/aviateur **v0.3.3** (`c46a116`), devourer `800c3c8` (v0.3.3에 고정된 버전) |
| 빌드 | GitHub Actions `lowlat Windows build` 성공 (windows-2025, MSVC, vcpkg FFmpeg/libusb/libsodium) |
| 빌드 검증 | Linux(gcc 13)에서도 경고·오류 없이 빌드. 통계 모듈은 별도 단위 시험으로 CSV와 끊김/복구 이벤트 동작 확인 |
| 아직 안 한 것 | 실제 장비(RTL8812AU + 송신 GX10)에서의 실행 검증 → 이번 A/B 시험에서 함께 확인 |

**성격:** 계측 추가와 확인된 버그 3건 수정입니다. 수신·복호화·FEC·디코딩·화면 표시 로직은 바꾸지 않았습니다.
지연을 줄이는 수정(디코딩·표시 경로)은 이번 측정으로 병목을 확인한 뒤 패치 2에서 진행합니다.

---

## 1. 키 파일을 바이너리 모드로 읽기 (버그 수정)

**문제**
수신 쪽에서 `Unable to read rx secret key: No error` 오류로 수신이 바로 중단됐습니다.
WFB-ng 키(64바이트 바이너리)를 텍스트 모드로 열고 있었는데, Windows CRT는 텍스트 모드에서
`0x1A`를 파일 끝으로 처리하고 CR/LF를 변환합니다. 당시 gs.key는 offset 2와 56이 `0x1A`여서
2바이트만 읽히고 실패했습니다. (그때는 키를 재생성해 우회했습니다.)

**변경** `src/wifi/wfb-ng/rx.cpp`
```diff
-    if((fp = fopen(keypair.c_str(), "r")) == NULL)
+    // "rb": the key is binary. In text mode the Windows CRT treats 0x1A as end-of-file and
+    // translates CR/LF, so keys containing those bytes failed with "Unable to read rx secret key".
+    if((fp = fopen(keypair.c_str(), "rb")) == NULL)
```
송신용 경로(`src/wifi/transmitter.cpp`)는 원래 `"rb"`였고, 수신 경로만 `"r"`이었습니다.

**효과:** 어떤 키든 Windows에서 읽힙니다. 키를 재생성할 때 바이트 검사가 더는 필요 없습니다.

---

## 2. USB 수신 큐 설정을 드라이버에 전달 (설정이 무시되던 문제)

**문제**
devourer(Windows용 사용자 공간 RTL8812AU 드라이버)는 USB 수신 큐 깊이 등을 `DeviceConfig`로 받습니다.
그런데 라이브러리 자체는 환경변수를 읽지 않고(`examples/common/env_config.h`: "the library ... reads no env vars"),
예제 프로그램만 `DEVOURER_*` 환경변수를 `DeviceConfig`로 바꿔 줍니다.
Aviateur는 설정 없이 장치를 만들어서, `DEVOURER_RX_URBS`를 지정해도 **항상 기본값 8**이었습니다.

**변경 1** 새 파일 `src/wifi/devourer_env.h` (103줄)
아래 환경변수를 읽어 `devourer::DeviceConfig`를 만들고, 실제 적용값을 한 줄 요약으로 돌려줍니다.
지정하지 않은 값은 드라이버 기본값 그대로입니다.

| 환경변수 | 의미 | 기본값 |
|---|---|---|
| `DEVOURER_RX_URBS` | USB bulk-IN 수신 요청(URB) 큐 깊이 | 8 |
| `DEVOURER_RX_URB_BYTES` | URB 1개당 버퍼 크기 (4096 이상) | 16384 |
| `DEVOURER_RX_MODE` | 수신 링 방식: async / sync / reorder-pool / spsc-fat / decoupled | async |
| `DEVOURER_RX_POOL_SPARE` | pool 방식에서 추가 버퍼 수 | 0 |
| `DEVOURER_RX_RING_MS` | USB 수신 링 상태(rx.ring) 진단 출력 주기, ms | 끔 |

**변경 2** `src/wifi/wfbng_link.cpp`
```diff
-            rtlDevice = wifi_driver.CreateRtlDevice(devHandle, ctx, usb_lock);
+            std::string rx_cfg_summary;
+            const devourer::DeviceConfig dev_cfg = lowlat::DevourerRxConfigFromEnv(rx_cfg_summary);
+            GuiInterface::Instance().PutLog(LogLevel::Info, "{}", rx_cfg_summary);
+
+            rtlDevice = wifi_driver.CreateRtlDevice(devHandle, ctx, usb_lock, dev_cfg);
```

**확인 방법:** 로그에 아래 줄이 찍히고, 콘솔 로그의 devourer 출력이 `async ring of N URBs`로 바뀝니다.
```
[DRIVER] devourer RX config: urbs=16 urb_bytes=default(16384) mode=async pool_spare=0 ring_ms=1000
```

**주의:** 큐를 늘린다고 영상 프레임이 쌓이는 것은 아닙니다(URB는 USB 전송 단위). 다만 손실이 USB 단계에서
생기는지 확인하기 위한 A/B 변수이고, 원인이라고 확정된 것은 아닙니다.

---

## 3. RTP 손실 계산의 16비트 순환 오류 (버그 수정)

**문제**
RTP 시퀀스 번호는 16비트라 65535 다음이 0입니다. 기존 코드는 `seq_num - prev`를 int로 계산해서
순환 지점의 손실이 음수가 되어 기록되지 않았습니다. 3Mbps 4K 기준 약 4분마다 한 번 순환합니다.

**변경** `src/wifi/wfbng_link.cpp`
```diff
-        if (prev_seq_num.has_value() && seq_num - prev_seq_num.value() > 1) {
-            GuiInterface::Instance().PutLog(LogLevel::Info, "RTP packets lost: {}", seq_num - prev_seq_num.value() - 1);
+        if (prev_seq_num.has_value()) {
+            // 16-bit wrap-safe difference. Upstream compared with int promotion, so
+            // gaps across the 65535 -> 0 wrap were never reported.
+            const uint16_t diff = static_cast<uint16_t>(seq_num - prev_seq_num.value());
+            if (diff > 1 && diff < 0x8000) {
+                const int lost = diff - 1;
+                lowlat::LinkStats::I().rtp_gap_events++;
+                lowlat::LinkStats::I().rtp_gap_pkts += lost;
+                GuiInterface::Instance().PutLog(LogLevel::Info, "RTP packets lost: {}", lost);
+            }
```
`diff < 0x8000` 조건으로 순서가 뒤바뀐 패킷(뒤로 가는 번호)은 손실로 세지 않습니다.

---

## 4. 수신 경로 전 구간 초당 통계 (계측 추가)

**목적:** 기존 Aviateur는 WiFi/WFB/RTP 카운터를 내부에서 세기만 하고 화면이나 로그에 내보내지 않았고,
FEC 복구 수, 복구 후 손실 수도 알 수 없었습니다. 그래서 손실 위치를 추측으로만 좁혔습니다.

**새 파일** `src/link_stats.h` (394줄): `lowlat::LinkStats` 싱글턴
- 패킷 경로에서는 `std::atomic` 카운터 증가만 합니다(잠금 없음, 부하 무시 가능).
- 별도 스레드가 1초마다 카운터를 읽고 0으로 되돌린 뒤, `logs/link_<시작시각>.csv`에 한 줄 기록, 로그에 `[STATS]` 요약을 남깁니다.
- `logs` 폴더는 실행 파일 옆에 만듭니다(한글 경로 안전: Windows에서 `_wfopen` 사용).

**계측 지점**

| 위치 | 파일 | 세는 것 |
|---|---|---|
| 드라이버가 프레임을 넘겨줄 때 | `wfbng_link.cpp` `handle_80211_frame` | `wifi_frames` (주변 AP 포함 전체) |
| WFB 프레임 판별 후 | 같은 함수 | `wfb_frames` |
| 영상 채널 프레임 처리 직후, `clear_stats()` 직전 | 같은 함수 | `video_frames`, WFB-ng 집계기 카운터 전부, 안테나별 RSSI/SNR |
| WFB-ng가 RTP를 내보낼 때 | `AggregatorX::send_to_socket` | `rtp_pkts`, RTP 손실 이벤트/패킷 수 |
| 디코더가 프레임을 낼 때 | `video_player.cpp` 디코드 스레드 | `frames_decoded` |
| 표시 큐가 차서 이전 프레임을 버릴 때 | 같은 곳 | `frames_dropped_queue` |
| 렌더러가 프레임을 가져갈 때 | `video_player.cpp` `getFrame()` | `frames_rendered`, 큐 대기시간 |
| 디코더 예외/재오픈 | 디코드 스레드 catch 블록 | `read_errors`, `send_errors`, `reopens` |

WFB-ng 집계기 카운터는 Aviateur가 패킷마다 `clear_stats()`로 지우기 때문에, 지우기 직전에 누적합니다.
```cpp
            st.wfb_all += video_aggregator->count_p_all;
            st.wfb_session += video_aggregator->count_p_session;
            st.wfb_data += video_aggregator->count_p_data;
            st.wfb_dec_err += video_aggregator->count_p_dec_err;
            st.wfb_bad += video_aggregator->count_p_bad;
            st.fec_recovered += video_aggregator->count_p_fec_recovered;
            st.lost_after_fec += video_aggregator->count_p_lost;
            st.wfb_out_pkts += video_aggregator->count_p_outgoing;
            st.wfb_out_bytes += video_aggregator->count_b_outgoing;
        }

        // This is necessary.
        video_aggregator->clear_stats();
```

큐 대기시간은 디코딩된 프레임을 큐에 넣은 시각을 별도 큐(`videoFramePushUs`, `video_player.h`)에 같이 넣고,
렌더러가 꺼낼 때 차이를 잽니다. Aviateur는 화면 갱신(약 60Hz) 때 프레임을 가져가므로, 이 값이 표시 단계에서 생기는 대기입니다.

**CSV 컬럼**

| 컬럼 | 뜻 |
|---|---|
| `time` | 로컬 시각(ms) |
| `wifi_frames` | 드라이버가 넘긴 모든 802.11 프레임 |
| `wfb_frames` | WFB-ng 형식 프레임 |
| `video_frames` | 우리 영상 채널(link_id 7669206, radio port 0) 프레임 = **실제 받은 무선 패킷** |
| `wfb_all`, `wfb_session`, `wfb_data` | WFB-ng 집계기: 전체 / 세션 키 / 데이터 패킷 |
| `wfb_dec_err` | 복호화 실패 (키 불일치, 세션 키 받기 전 데이터) |
| `wfb_bad` | 형식 오류 패킷 |
| `fec_recovered` | 무선에서 잃었지만 FEC로 복구 |
| `lost_after_fec` | **FEC로도 복구 못 한 패킷** (화면 깨짐 직접 원인) |
| `wfb_out_pkts`, `wfb_out_kbps` | WFB-ng가 내보낸 RTP 패킷 수 / 속도 |
| `rtp_pkts`, `rtp_gap_events`, `rtp_gap_pkts` | RTP 수신 / 손실 이벤트 / 손실 패킷 |
| `frames_decoded`, `frames_rendered` | 초당 디코딩 / 표시 프레임 |
| `frames_dropped_queue` | 디코딩됐지만 표시 전에 더 새 프레임으로 교체됨 |
| `queue_wait_avg_ms`, `queue_wait_max_ms` | 디코딩 후 표시될 때까지 대기 |
| `rssi0_avg`, `rssi1_avg`, `rssi_min`, `snr0_avg`, `snr1_avg` | 안테나별 수신 세기, SNR |
| `read_errors`, `send_errors`, `reopens` | 디코더 읽기/전송 오류, 재오픈 |

---

## 5. 끊김·복구 이벤트와 복구 시간 측정 (계측 추가)

**목적:** 녹음에서 가장 중요하게 다룬 "끊겼다 다시 붙을 때의 복구 지연"을 숫자로 재기 위함입니다.

**동작** (`src/link_stats.h`)
1. 50ms마다 마지막 영상 패킷 시각을 확인해서, **300ms 이상** 패킷이 없으면
   `[EVENT] link lost: no video packets for N ms`
2. 다음 영상 패킷이 오면
   `[EVENT] link restored: video packets back after N ms without packets`
3. 그 뒤 처음으로 렌더러가 프레임을 가져가면
   `[EVENT] recovery: first rendered frame N ms after link restored`

3번 값이 "신호 복귀 → 첫 화면" 복구 시간입니다. 첫 IDR/헤더 대기, FFmpeg 재오픈 대기(코드상 1초 sleep),
디코딩, 표시 대기가 모두 포함됩니다. 재오픈 시각도 `[EVENT] decoder reopen (...)`로 남아서 어느 부분이 오래 걸렸는지 나눠 볼 수 있습니다.

**한계:** 300ms 기준이라 그보다 짧은 끊김은 이벤트가 아니라 CSV의 손실 수치로만 보입니다.
화면에 실제로 그려지는 시점이 아니라 렌더러가 프레임을 가져가는 시점까지입니다(그 뒤 화면 출력 지연은 별도).

---

## 6. 로그 파일과 시각 (계측 추가)

**문제:** 기존에는 로그가 숨겨진 콘솔로만 나가서 cmd 리다이렉트로 받아야 했고, 시각이 없어 송신 쪽 기록과 대조할 수 없었습니다.

**변경** `src/gui_interface.h`
```diff
     void PutLog(LogLevel level, const std::string_view message, Args... format_items) {
         std::string str = std::vformat(message, std::make_format_args(format_items...));
-        EmitLog(level, str);
+        EmitLog(level, lowlat::WallClock() + " " + str);
     }
```
그리고 로그 콜백을 하나 더 등록해서 `logs/aviateur_<시작시각>.log`에 레벨과 함께 줄 단위로 저장합니다(매 줄 flush).
첫 줄에 빌드 식별자가 찍힙니다.
```
[INFO] 2026-10-28 16:05:01.123 Aviateur 0.3.3 + lowlat-rx patch 1 (stats, key rb, devourer rx env) (log file: ...)
```
링크 시작/정지도 남깁니다: `[LINK] start: device 0bda:8812 channel 36 width_mode 0 key ...`, `[LINK] stop requested`

---

## 7. 실행 배치 파일과 Windows 자동 빌드

| 파일 | 내용 |
|---|---|
| `lowlat/run_lowlat.bat` | `DEVOURER_RX_URBS`(인자, 기본 8)와 `DEVOURER_RX_RING_MS=1000`을 설정하고 실행. 드라이버와 WFB-ng 원본 출력을 `logs/console_<시각>_urbsN.txt`에 저장 |
| `lowlat/run_urbs8.bat`, `16`, `32` | 큐 값별 바로가기 |
| `.github/workflows/lowlat-windows.yml` | push마다 Windows 빌드, exe + DLL + assets + bat를 zip 아티팩트로 업로드 |

---

## 바꾸지 않은 것

- WFB-ng 수신·복호화·FEC 로직, 채널 ID(link_id 7669206, port 0)
- FFmpeg 디코더 설정(`probesize`, `analyzeduration`, 하드웨어 디코딩 선택), CPU 메모리 복사 경로
- 표시 큐(최대 1프레임 유지, 화면 갱신 시 가져감), 디코더 재오픈 시 1초 대기
- 송신(TX) 경로, adaptive link

위 항목 중 지연·복구에 영향이 큰 것(재오픈 대기, CPU 복사, 내부 UDP/SDP 입력, 표시 대기)은
이번 측정값으로 실제 비중을 확인한 뒤 패치 2에서 다룹니다.

---

## 이번 A/B 시험 요청 (USB 수신 큐)

조건 고정: 같은 replay 파일, 같은 위치, 채널 36 / 20MHz / MCS 1, 같은 송신 출력.
수신 쪽만 `run_urbs8.bat` → `run_urbs16.bat` → `run_urbs32.bat` 순서로 3회.

**송신 쪽에 부탁드릴 것:** 회차마다 시작·종료 시각, 초당 송신(주입) 패킷 수를 시각과 함께.

**판단 기준** (같은 시각의 송신 초당 패킷 수와 수신 CSV `video_frames` 비교)

| 관찰 | 해석 |
|---|---|
| 송신 수 ≫ `video_frames`, `rx.ring` 깊이가 0 근처로 떨어지거나 resubmit 실패 | 수신 USB 단계 손실 → 큐 값으로 개선되는지 확인 |
| 송신 수 ≫ `video_frames`, `rx.ring` 여유 있음 | 무선 구간 손실 (출력, 채널, 안테나, MCS) |
| `video_frames` ≈ 송신 수인데 `lost_after_fec` > 0 | 집계기 단계 이상 → 추가 조사 |
| `lost_after_fec` = 0인데 화면 깨짐 | 디코더/표시 단계 |

## 추가 확인 부탁

1. 13:3x 시험 때 송신 출력이 실제로 바뀌었는지 (`iw dev wlxfc221c1000f1 info`의 txpower 표시)
2. 16.5fps는 어느 단계에서 잰 값인지 (카메라 입력 / 인코더 출력 / 송신)
