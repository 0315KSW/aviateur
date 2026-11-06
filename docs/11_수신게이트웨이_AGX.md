# 11. 수신 게이트웨이 (AGX Orin) — 10/30~11/2

10/29 회의에서 정한 구조대로 무선 수신을 Windows PC에서 떼어 내 리눅스 게이트웨이로 옮겼습니다.
게이트웨이는 무선으로 받은 영상을 디코딩하지 않고 UDP로 PC에 넘기기만 합니다.

## 구성
```
카메라 Pi ─LAN/Wi-Fi─▶ 미니PC(gx10-5227, WFB-ng drone) ~~5GHz~~▶ AGX Orin(WFB-ng gs) ─LAN─▶ Windows PC
                                                                   192.168.0.68              192.168.0.48
                                                                   터널 10.5.0.1  ◀──▶  미니PC 터널 10.5.0.2
```

| 항목 | 값 |
|---|---|
| 장비 | NVIDIA Jetson AGX Orin (Ubuntu, 사용자 `nvidiaagxorin`) |
| 무선 동글 | RTL8812AU (AC1200) |
| 드라이버 | [svpcom/rtl8812au](https://github.com/svpcom/rtl8812au) (DKMS) |
| WFB-ng | 26.10.2, 저장소의 `install_gs.sh`로 설치, 프로필 `gs` |
| 서비스 | `wifibroadcast@gs` (부팅 시 자동 시작으로 `enable`) |
| 영상 출력 | `peer = connect://192.168.0.48:5600` → PC UDP 5600 |
| JSON 통계 API | 127.0.0.1:8103 (송신 drone 쪽은 8102) |
| AGX 내장 Wi-Fi | `wlP1p1s0`, ch161 · 80MHz (WFB 채널과 겹치지 않음) |

## 설치하며 부딪힌 문제
| # | 문제 | 원인 | 조치 |
|---|---|---|---|
| 1 | 동글 인식이 끊겼다 다시 잡힘 | USB3 재열거 | 재연결 후 인식 확인 |
| 2 | 무선 규제 국가가 BO로 잡혀 있음 | 기본 설정 | 규제 국가를 KR로 설정 |
| 3 | 세션 복호화 실패 | 설치 스크립트가 키를 새로 만들어 기존 키를 덮어씀 | 송신(drone.key)과 짝인 `gs.key`를 AGX에 복사 |
| 4 | 재부팅 후 수신이 안 올라옴 | 서비스 미등록 | `wifibroadcast.service` enable |
| 5 | PC가 영상을 못 받음 | 출력 대상 미설정 | `peer = connect://192.168.0.48:5600` |
| 6 | 포트가 이미 쓰이는 중 | 이전 시험의 gst 프로세스가 남아 있었음 | 프로세스 정리 후 재시작 |
| 7 | `sed` 오류 | 명령 예시에 자리표시자(<...>)를 그대로 둠 | 실제 값으로 바꾼 명령으로 다시 실행. 이후 명령 블록에는 자리표시자를 쓰지 않음 |

키 파일(`gs.key`, `drone.key`)은 이 저장소에 올리지 않습니다(`.gitignore`에 `*.key`).

## 무선 설정에서 확인한 것
- 송수신을 STBC0(끔)으로 맞췄습니다.
- MCS는 2가 기준입니다(11/3 시험에서 MCS4는 손실 11%로 제외 → [13](13_11-03_무선손실시험.md)).
- 채널은 11/3에 36 → 48로 옮겼습니다. 바꾼 명령:
```
ssh -t nvidiaagxorin@192.168.0.68 "sudo sed -i 's/^wifi_channel *=.*/wifi_channel = 48/' /etc/wifibroadcast.cfg && sudo systemctl restart wifibroadcast@gs && grep ^wifi_channel /etc/wifibroadcast.cfg"
```

## Aviateur 패치 1.2
게이트웨이가 넘겨 주는 RTP를 Aviateur(GStreamer 없는 FFmpeg 경로)로 받으려고 SDP를 고쳤습니다. 내용은 [05](05_수신기_패치.md) "패치 1.2"를 보세요.

## 11/2 결과
- 카메라 Pi → 미니PC → AGX → PC 네 장비 연결로 영상이 PC에 나왔습니다.
- Aviateur로 본 전체 지연은 약 0.7초였습니다. 이 값을 11/2 회의에서 보고했습니다 → [10](10_11-02_3일차회의.md).
- 이후 PC 수신 프로그램과 카메라 쪽 파이프라인을 바꿔 지연을 줄였습니다 → [12](12_수신PC_GStreamer와_로거.md), [14](14_11-05~06_지연측정.md).
