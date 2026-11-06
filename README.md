# 360도 카메라 드론 영상 저지연 무선 전송 — 진행 기록 (~2026-11-06)

경운대학교 소프트웨어학과 학생 팀이 업체와 진행 중인 **드론 영상 저지연 무선 전송** 과제의 작업 기록입니다.
10/22~10/28 기록(문서 01~09)에 10/30~11/6 진행(문서 10~14)을 더했습니다.

> 이 저장소의 수치는 수신 로그·CSV·콘솔 출력(`data/`), 화면 녹화, 송신 측 보고, 회의 녹음에서 확인한 값입니다.
> 확인하지 못한 내용은 "미확인", "추정", "확인 중"으로 표시했습니다.

## 한 줄 요약

카메라 Pi에서 H.264를 하드웨어 인코딩해 미니PC의 **WFB-ng**로 5GHz 무선 송신하고, **AGX Orin 게이트웨이**가 받아 LAN으로 PC에 넘기면 PC가 **GStreamer(D3D11 GPU 디코딩)**로 표시합니다.
무선 손실은 채널을 48로 옮겨 1% 아래로 줄였고, 전체 지연은 11/2 약 700ms에서 **11/6 약 174ms(720p40)**까지 줄였습니다. 목표는 약 150ms입니다.

## 구성 (11/6 기준)

```
[Air]                                                                [Ground]
카메라 Pi ─(지금 Wi-Fi, LAN 예정)─▶ 미니PC gx10-5227 ~~WFB-ng 5GHz ch48~~▶ AGX Orin ─LAN─▶ Windows PC
libcamerasrc → v4l2h264enc            WFB-ng drone                         WFB-ng gs        GStreamer
720p40 H.264 2Mbps, RTP/UDP          FEC 8/12, MCS2, 20MHz                 → UDP 5600       d3d11h264dec → 화면
        └──────────────── 비교용 직접 경로: 카메라 Pi → PC UDP 5610 ───────────────────────────────┘
제어(예정): PC ↔ AGX(10.5.0.1) ↔ WFB-ng 터널 ↔ 미니PC(10.5.0.2) ↔ 카메라·드론
```

- TCP는 쓰지 않습니다. WFB-ng가 RTP 패킷을 와이파이 프레임에 직접 실어 보내고, 재전송 대신 FEC로 복구합니다.
- 10/28까지의 구성(GX10 → Windows Aviateur 직접 수신)은 [02](docs/02_전체구성.md)에 있습니다.

## 현재 상태

| 항목 | 상태 |
|---|---|
| 카메라 → 송신 → 무선 → 게이트웨이 → PC 네 장비 연결 | 완료 (11/2) |
| 무선 설정 | ch48 · 20MHz · MCS2 · STBC0 · 출력 인덱스 1 · FEC 8/12 · 2Mbps. 손실 0.35~0.9% (11/3) |
| PC 수신 | GStreamer `d3d11h264dec compliance=flexible`, `sync=false` (GPU 디코딩·표시) |
| 전체 지연 | **약 174ms (120~200), 720p40** (11/6, 한 시계 방식). 목표 약 150ms |
| 무선 도착률 | 81~87%. 원인 미확정 (하드웨어 쪽 추정) |
| 카메라 Pi LAN 연결 | 케이블 없음. 지금은 Wi-Fi(wlan0)로 송신 |
| `dmabuf-import` 효과 | 확인 중 (켬/끔 A/B 필요) |
| 전체 화각 모드(1640×1232) | 시험 안 함 |
| 양방향 제어(터널) | 시험 안 함 |
| 업체 GCS용 PC 수신 툴, 부팅 시 자동 시작 | 시작 안 함 (AGX WFB-ng 서비스 자동 시작만 됨) |

## 문서

| 문서 | 내용 |
|---|---|
| [01 요구사항 (10/22 회의)](docs/01_요구사항_10-22회의.md) | 업체 요구사항과 녹취록 근거 |
| [02 전체 구성](docs/02_전체구성.md) | 10/28까지 장비·프로그램·데이터 경로·프로토콜 |
| [03 송신 (GX10)](docs/03_송신_GX10.md) | 드라이버, WFB-ng, FFmpeg/NVENC 설정, 송신 쪽 진행 |
| [04 수신 (Windows)](docs/04_수신_Windows.md) | Zadig, Aviateur 설치·설정·로그 받는 법 |
| [05 수신기 수정 (패치 1·1.1·1.2)](docs/05_수신기_패치.md) | Aviateur 포크, 코드 변경, 빌드, 실행 방법 |
| [06 시험 결과](docs/06_시험결과.md) | 10/28 시험별 조건과 손실 수치 |
| [07 문제와 해결](docs/07_문제와해결.md) | 부딪힌 문제, 원인, 조치, 고친 판단 |
| [08 타임라인](docs/08_타임라인.md) | 10/22~11/6 날짜·시각별 기록 |
| [09 10/29 회의와 다음 단계](docs/09_10-29회의_다음단계.md) | 바뀐 구조(게이트웨이·양방향·150ms)와 계획 |
| [10 11/2 3일차 회의](docs/10_11-02_3일차회의.md) | 업체 의견(GStreamer, UDP, zero-copy), 최종 결과물 |
| [11 수신 게이트웨이 (AGX)](docs/11_수신게이트웨이_AGX.md) | AGX 설치·설정, 부딪힌 문제, 11/2 결과 |
| [12 수신 PC (GStreamer)와 로거](docs/12_수신PC_GStreamer와_로거.md) | GStreamer 설치·파이프라인, 실행 파일, 링크 로거, 지연 재는 법 |
| [13 11/3 무선 손실 시험](docs/13_11-03_무선손실시험.md) | 채널·출력·MCS·비트레이트별 손실 |
| [14 11/5~11/6 지연 측정](docs/14_11-05~06_지연측정.md) | 바꾼 것과 효과, 센서 모드, 다음 할 일 |

## 코드와 데이터

| 위치 | 내용 |
|---|---|
| [0315KSW/aviateur, `receiver-lowlat` 브랜치](https://github.com/0315KSW/aviateur/tree/receiver-lowlat) | 수정한 Aviateur 수신기 소스 (v0.3.3 포크, 패치 1.2까지) |
| [`receiver/windows/`](receiver/windows) | PC 수신 실행 파일: `gst_rx_5600.bat`(무선 경로), `gst_rx_5610.bat`(직접 경로), `agx_test.bat`(수신 창 + AGX 로거 + CSV 가져오기) |
| [`tools/wfb_link_log.py`](tools/wfb_link_log.py) | WFB-ng JSON API 링크 로거 (수신 8103 / 송신 8102 → CSV) |
| [`tools/analyze_aviateur_log.py`](tools/analyze_aviateur_log.py) | Aviateur 로그에서 세션·디코더·RTP 손실·참조 프레임 오류 집계 |
| [`tools/analyze_link_csv.py`](tools/analyze_link_csv.py) | 패치 1 CSV를 회차별·시간 구간별로 집계 |
| [`data/link_console/`](data/link_console) | 11/3 무선 시험 콘솔 출력 14개 (`_TX`는 송신 미니PC) |
| [`data/receiver_logs/`](data/receiver_logs) | 10/27~10/28 Aviateur 수신 로그 6개 |
| [`data/receiver_csv/`](data/receiver_csv) | 패치 1 초당 통계 CSV (10/28 16:32~16:37) |
| `tx/` | 송신 스크립트·설정 — **송신 담당이 추가 예정** |

암호 키(`gs.key`, `drone.key`)는 저장소에 올리지 않습니다(`.gitignore`에 `*.key`).

## 참고한 공개 프로젝트

- [svpcom/wfb-ng](https://github.com/svpcom/wfb-ng) — 무선 전송
- [svpcom/rtl8812au](https://github.com/svpcom/rtl8812au) — RTL8812AU WFB용 Linux 드라이버
- [OpenIPC/aviateur](https://github.com/OpenIPC/aviateur) — Windows 수신 프로그램 (v0.3.3)
- [OpenIPC/devourer](https://github.com/OpenIPC/devourer) — Aviateur에 들어 있는 사용자 공간 RTL8812AU 드라이버
- [Zadig](https://github.com/pbatard/libwdi) — Windows USB 드라이버 교체 도구
- [GStreamer](https://gstreamer.freedesktop.org/) — PC 수신(1.28.8), 카메라 Pi 송신(libcamerasrc, v4l2h264enc)
