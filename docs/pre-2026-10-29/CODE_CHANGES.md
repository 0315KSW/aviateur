# 파일별 코드 변경 내역

기준 코드: Aviateur v0.3.3 `c46a116` → 수정본 `15f511c`.
아래는 10월 28일의 실제 커밋과 소스를 확인해 정리한 내용이다. 명령 예시나 제안만 존재하는 기능을 구현 실적으로 적지 않았다.

[전체 diff](https://github.com/0315KSW/aviateur/compare/c46a11666a1df9841a1c86849f39e4bda314377d...15f511c474db3fb0ab90c19c3d5233304fc443a5)

## 1. 파일 배치와 역할

| 파일 | 구분 | 역할 |
|---|---|---|
| `src/wifi/wfb-ng/rx.cpp` | 수정 | 수신 키를 바이너리 모드로 읽기 |
| `src/wifi/devourer_env.h` | 추가 | 환경변수 → devourer::DeviceConfig 변환 |
| `src/wifi/wfbng_link.cpp` | 수정 | 드라이버 설정 전달, WFB 통계 누적, RTP 누락 계산 |
| `src/link_stats.h` | 추가 | 초당 통계, CSV, 끊김·재수신 이벤트 |
| `src/player/ffmpeg/video_player.cpp` | 수정 | 디코딩·큐·인출·오류·재오픈 계측 |
| `src/player/ffmpeg/video_player.h` | 수정 | 프레임이 큐에 들어간 시각을 보관할 큐 추가 |
| `src/gui_interface.h` | 수정 | 시각을 포함한 앱 파일 로그, 콜백 문자열 전달 수정 |
| `lowlat/run_lowlat.bat` | 추가 | 설정값 적용 후 실행·콘솔 저장 |
| `lowlat/run_urbs8.bat`, `run_urbs16.bat`, `run_urbs32.bat` | 추가 | 8·16·32 비교 실행 |
| `.github/workflows/lowlat-windows.yml` | 추가 | Windows 자동 빌드와 패키징 |
| `lowlat/README_LOWLAT.md`, `lowlat/PATCH1_CHANGES.md` | 추가 | 패치 사용 방법·기존 상세 설명 |

`wifi`는 동글·무선 데이터 경로, `player`는 영상 재생 경로다. 공통 계측을 `src/link_stats.h`로 두고 각 경로에서 호출했다. 실행 배치와 안내는 `lowlat`에, 자동 빌드는 GitHub Actions가 읽는 `.github/workflows`에 배치했다.

## 2. 바이너리 키 읽기 수정

파일: [rx.cpp](https://github.com/0315KSW/aviateur/blob/15f511c474db3fb0ab90c19c3d5233304fc443a5/src/wifi/wfb-ng/rx.cpp)

Windows에서 바이너리 키를 텍스트 모드로 읽으면 일부 바이트가 EOF나 줄바꿈 변환으로 처리될 수 있다. 기존 패치 기록에는 `Unable to read rx secret key` 오류를 키 재생성으로 우회한 이력이 설명돼 있다.

```diff
-    if((fp = fopen(keypair.c_str(), "r")) == NULL)
+    if((fp = fopen(keypair.c_str(), "rb")) == NULL)
```

- `r`: 텍스트 읽기 모드.
- `rb`: 바이너리 읽기 모드. 파일 바이트를 그대로 읽는다.
- 키 형식·길이·송수신 키 일치까지 자동으로 보장하는 수정은 아니다.

## 3. USB 수신 설정을 실제 드라이버에 전달

파일: [devourer_env.h](https://github.com/0315KSW/aviateur/blob/15f511c474db3fb0ab90c19c3d5233304fc443a5/src/wifi/devourer_env.h), [wfbng_link.cpp](https://github.com/0315KSW/aviateur/blob/15f511c474db3fb0ab90c19c3d5233304fc443a5/src/wifi/wfbng_link.cpp)

고정된 devourer 버전은 라이브러리에서 환경변수를 자동으로 읽지 않는다. 앱에서 환경변수를 읽어 `DeviceConfig`에 넣어 전달해야 한다. 드라이버 헤더에 설정 항목이 있다는 것만으로 앱에서 적용되는 것은 아니었다.

```cpp
if (EnvLong("DEVOURER_RX_URBS", &v) && v >= 1) {
    cfg.rx.urbs = static_cast<int>(v);
}
```

실제 장치 생성 지점도 변경했다.

```diff
- rtlDevice = wifi_driver.CreateRtlDevice(devHandle, ctx, usb_lock);
+ std::string rx_cfg_summary;
+ const devourer::DeviceConfig dev_cfg = lowlat::DevourerRxConfigFromEnv(rx_cfg_summary);
+ GuiInterface::Instance().PutLog(LogLevel::Info, "{}", rx_cfg_summary);
+ rtlDevice = wifi_driver.CreateRtlDevice(devHandle, ctx, usb_lock, dev_cfg);
```

| 환경변수 | 의미 | 미지정 시 기록된 기본 동작 |
|---|---|---|
| `DEVOURER_RX_URBS` | USB bulk-IN 수신 요청 수 | 8 |
| `DEVOURER_RX_URB_BYTES` | 요청 하나의 버퍼 크기 | 16384바이트 |
| `DEVOURER_RX_MODE` | 수신 방식 | async |
| `DEVOURER_RX_POOL_SPARE` | pool 방식의 추가 버퍼 | 0 |
| `DEVOURER_RX_RING_MS` | 수신 링 진단 주기 | 끔 |

이번 패치는 32를 고정한 것이 아니라 8·16·32를 비교할 수 있도록 설정 경로를 연결한 작업이다. 큐 증가가 손실을 줄였다는 실측 결과는 아직 이 보고서에 없다.

## 4. RTP 번호 순환 시 누락 계산 수정

파일: `src/wifi/wfbng_link.cpp`, `AggregatorX::send_to_socket`.

RTP 시퀀스 번호는 16비트라 65535 다음이 0이다. 일반 정수 뺄셈만 하면 순환 경계에서 누락을 잘못 계산할 수 있어 16비트 차이로 계산하도록 변경했다.

```cpp
const uint16_t diff = static_cast<uint16_t>(seq_num - prev_seq_num.value());
if (diff > 1 && diff < 0x8000) {
    const int lost = diff - 1;
    lowlat::LinkStats::I().rtp_gap_events++;
    lowlat::LinkStats::I().rtp_gap_pkts += lost;
    GuiInterface::Instance().PutLog(LogLevel::Info, "RTP packets lost: {}", lost);
}
```

이 카운터는 시퀀스 간격을 기록하는 진단값이다. 송신 세션 변경·재정렬 등 조건까지 모두 처리한 절대적인 무선 손실률로 해석하지 않는다.

## 5. WFB·영상 처리 통계 추가

파일: [link_stats.h](https://github.com/0315KSW/aviateur/blob/15f511c474db3fb0ab90c19c3d5233304fc443a5/src/link_stats.h).

패킷·프레임 처리 경로에서 카운터를 증가시키고 별도 스레드에서 약 1초마다 CSV와 요약 로그를 기록한다. WFB 집계기는 처리 후 카운터를 지우므로, 지우기 전에 값을 누적한다.

```cpp
st.fec_recovered += video_aggregator->count_p_fec_recovered;
st.lost_after_fec += video_aggregator->count_p_lost;
st.wfb_out_pkts += video_aggregator->count_p_outgoing;
st.wfb_out_bytes += video_aggregator->count_b_outgoing;
```

| 측정값 | 측정 위치·의미 |
|---|---|
| `wifi_frames` | 드라이버가 넘긴 프레임. 주변 트래픽도 포함될 수 있음 |
| `wfb_frames` | WFB 형식 판별을 통과한 프레임 |
| `video_frames` | 앱이 처리한 영상 채널 WFB 프레임. 영상의 FPS가 아님 |
| `fec_recovered` | WFB 집계기가 FEC로 복구한 패킷 수 |
| `lost_after_fec` | WFB 집계기가 기록한 복구 후 누락 |
| `wfb_dec_err` | WFB 복호화 실패. 영상 디코딩 실패와 다름 |
| `rtp_gap_pkts` | RTP 번호 간격에서 계산한 누락 |
| `frames_decoded` | 디코딩 루프에서 받은 영상 프레임 수 |
| `frames_rendered` | `getFrame()`에서 렌더러 쪽으로 가져간 프레임 수 |
| `frames_dropped_queue` | 큐에서 표시 전에 버린 프레임 수 |
| `queue_wait_avg_ms`, `queue_wait_max_ms` | 프레임을 큐에 넣고 꺼낼 때까지의 평균·최대 대기 |
| `read_errors`, `send_errors`, `reopens` | 디코더 읽기·패킷 전송 예외·입력 재오픈 횟수 |

각 수치는 집계 위치가 다르다. `wifi_frames - video_frames`를 손실로 계산하거나 `video_frames`를 FPS로 읽지 않는다. 초당 값은 약 1초 간격의 카운트이며, 스케줄링 지연이 있을 때 정확한 FPS·비트레이트 평가에는 실제 경과 시간도 고려한다.

## 6. 끊김과 복구 계측

- 마지막 영상 채널 패킷이 들어온 시각을 저장한다.
- 약 50ms마다 확인해 300ms를 초과하는 무수신 구간이면 `link lost`를 기록한다.
- 다음 영상 채널 패킷 처리 시 `link restored`를 기록한다.
- 그 뒤 처음 `getFrame()`이 프레임을 가져가면 `recovery` 시간을 기록한다.

**측정 한계**

1. 물리 무선 링크 상태를 직접 측정한 값이 아니라 앱의 영상 채널 패킷 도착을 기준으로 한 이벤트다.
2. 재수신한 패킷이 곧바로 정상 디코딩 가능한 영상이라는 보장은 없다.
3. `frames_rendered`와 복구 시간의 끝점은 실제 모니터 표시 완료가 아니라 큐에서 프레임을 꺼내는 시점이다.
4. 기존 큐 프레임과 재수신 후 새 프레임을 ID로 대응시키지는 않는다. 재수신 뒤 오래된 큐 프레임을 꺼낸 경우를 배제하는 계측은 아니다.
5. 따라서 카메라부터 화면까지의 전체 지연이나 정상 영상 복구 시간을 이 로그만으로 확정하지 않는다. 원본 장면과 수신 화면을 함께 촬영한 비교가 추가로 필요하다.

## 7. 앱 로그와 빈 문자열 문제 수정

파일: [gui_interface.h](https://github.com/0315KSW/aviateur/blob/15f511c474db3fb0ab90c19c3d5233304fc443a5/src/gui_interface.h).

로그 메시지 앞에 로컬 시각을 붙이고 파일 저장 콜백을 등록했다. 이후 첫 콜백으로 문자열을 이동시켜 다음 콜백이 빈 문자열을 받는 문제를 패치 1.1에서 수정했다.

```cpp
std::string str = std::vformat(message, std::make_format_args(format_items...));
EmitLog(level, lowlat::WallClock() + " " + str);
```

```cpp
LogLevel lv = level;
std::string m = msg;
callback.operator()<LogLevel, std::string>(std::move(lv), std::move(m));
```

콜백마다 복사본을 만든 뒤 전달하므로 화면 로그와 파일 로그가 같은 메시지를 받을 수 있다.

## 8. 디코딩·표시 경로에서 유지된 원본 동작

[ffmpeg_decoder.cpp](https://github.com/0315KSW/aviateur/blob/15f511c474db3fb0ab90c19c3d5233304fc443a5/src/player/ffmpeg/ffmpeg_decoder.cpp)는 이 패치에서 변경되지 않았다.

- 하드웨어 디코더를 FFmpeg 지원 목록에서 탐색하고 초기화한다. 성공한 이름을 `Using hardware decoder: ...`로 출력한다.
- `fflags=nobuffer`, `flags=low_delay`, `reorder_queue_size=0`, `max_delay=0` 등은 원본에 있던 설정이다.
- `probesize=512000`, `analyzeduration=500000`도 원본 설정이다. 이 값만으로 고정 0.5초 지연이 발생한다고 단정하지 않는다.
- Windows에서 하드웨어 디코딩 결과를 CPU 메모리로 가져오는 경로가 남아 있다. zero-copy로 바꾸지 않았다.
- 일부 재연결 오류 경로의 1초 대기도 남아 있다.
- 화면 큐 처리에 통계를 추가했으며 큐 길이 정책을 새로 줄인 패치는 아니다.

즉, 패치 1·1.1의 완료 내용은 오류 수정·설정 전달·계측·빌드 자동화다. 수신 방식 전체 재설계나 지연 목표 달성을 완료했다고 보고하지 않는다.
