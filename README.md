# 360도 카메라 드론 영상 저지연 무선 전송 — 진행 기록 (~2026-10-28)

경운대학교 소프트웨어학과 학생 팀이 업체와 진행 중인 **드론 영상 저지연 무선 전송** 과제의 10월 28일까지 작업 기록입니다.
10월 29일 회의에서 "지금까지 한 내용을 정리해 Git에 올려 달라"는 요청에 따라 작성했습니다.

> 이 저장소의 모든 수치는 수신 로그·CSV(`data/`), 송신 측 최종 보고(GX10), 회의 녹취록에서 확인한 값입니다.
> 확인하지 못한 내용은 "미확인" 또는 "추정"으로 표시했습니다.

## 한 줄 요약

GX10(Ubuntu)에서 카메라 영상을 **H.265 하드웨어 인코딩**해 **WFB-ng**로 5GHz 무선 송신하고, Windows PC에서 **Aviateur**로 받아 **하드웨어 디코딩(DXVA2)**해 화면에 표시하는 경로를 연결했습니다.
720p는 표시됐고 4K(3840×2160)도 표시됐지만, 4K에서는 패킷 손실로 화면이 깨졌습니다. 손실 위치를 재기 위해 수신 프로그램(Aviateur)을 포크해 계측 기능을 추가했습니다.

## 구성

```
[송신: GX10, Ubuntu ARM64]                                   [수신: Windows PC]
Femto Bolt 4K ─USB─▶ FFmpeg ─▶ H.265 RTP ─UDP 5602─▶ WFB-ng ─▶ RTL8812AU
 (MJPEG)        mjpeg_cuvid   hevc_nvenc              암호화+FEC     │ 5GHz ch36 / 20MHz / MCS1 / FEC 8/12
                                                                     ▼
                                       RTL8812AU ─WinUSB(Zadig)─▶ Aviateur(devourer 드라이버 내장)
                                                                   WFB-ng 복호화·FEC ─▶ RTP ─▶ FFmpeg
                                                                   H.265 DXVA2 하드웨어 디코딩 ─▶ 화면
```

- TCP는 쓰지 않습니다. 무선 구간은 공유기 접속 없이 WFB-ng가 RTP 패킷을 와이파이 프레임에 직접 실어 뿌립니다(재전송 대신 FEC).
- 상세: [docs/02_전체구성.md](docs/02_전체구성.md)

## 현재 상태

| 항목 | 상태 |
|---|---|
| 무선 수신·WFB 복호화·RTP 수신 | 완료 |
| H.265 하드웨어 인코딩(NVENC) / 디코딩(DXVA2) | 완료 |
| 720p 화면 표시 | 완료 (10/28 12:46 로그: 1280×720, 약 15초 중 손실 1회) |
| 4K(3840×2160) 화면 표시 | 표시는 됨, **패킷 손실로 깨짐** |
| 손실 원인 | 미확정. GX10 WFB-ng 이후 구간(송신 동글·전파·수신 동글·USB)으로 좁힘 |
| 지연 200~300ms 측정 | 미측정 (손실 안정화 후 진행) |
| 목표 비트레이트 20~25Mbps | 미달. 현재 무선 설정(MCS 1·20MHz)에서 3Mbps로 시험 |

## 문서

| 문서 | 내용 |
|---|---|
| [01 요구사항 (10/22 회의)](docs/01_요구사항_10-22회의.md) | 업체 요구사항과 녹취록 근거 |
| [02 전체 구성](docs/02_전체구성.md) | 장비·프로그램·데이터 경로·프로토콜 |
| [03 송신 (GX10)](docs/03_송신_GX10.md) | 드라이버, WFB-ng, FFmpeg/NVENC 설정, 송신 쪽 진행 |
| [04 수신 (Windows)](docs/04_수신_Windows.md) | Zadig, Aviateur 설치·설정·로그 받는 법, 수신 쪽 진행 |
| [05 수신기 수정 (패치 1·1.1)](docs/05_수신기_패치.md) | Aviateur 포크, 코드 변경, 빌드, 실행 방법 |
| [06 시험 결과](docs/06_시험결과.md) | 시험별 조건과 손실 수치 |
| [07 문제와 해결](docs/07_문제와해결.md) | 부딪힌 문제, 원인, 조치 |
| [08 타임라인](docs/08_타임라인.md) | 10/27~10/28 시각별 기록 |
| [09 10/29 회의와 다음 단계](docs/09_10-29회의_다음단계.md) | 바뀐 구조(게이트웨이·양방향·150ms)와 계획 |

## 코드와 데이터

| 위치 | 내용 |
|---|---|
| [0315KSW/aviateur, `receiver-lowlat` 브랜치](https://github.com/0315KSW/aviateur/tree/receiver-lowlat) | 수정한 수신기 소스 (Aviateur v0.3.3 포크) |
| [`tools/analyze_aviateur_log.py`](tools/analyze_aviateur_log.py) | Aviateur 로그에서 세션·디코더·RTP 손실·참조 프레임 오류 집계 |
| [`tools/analyze_link_csv.py`](tools/analyze_link_csv.py) | 패치 1 CSV를 회차별·시간 구간별로 집계 |
| [`data/receiver_logs/`](data/receiver_logs) | 수신 로그 원본 6개 |
| [`data/receiver_csv/`](data/receiver_csv) | 패치 1 초당 통계 CSV (10/28 16:32~16:37) |
| `tx/` | 송신(GX10) 스크립트·설정 — **송신 담당이 추가 예정** (원본: GX10 `/home/ikw/wfb-gx10/`) |

암호 키(`gs.key`, `drone.key`)는 저장소에 올리지 않습니다.

## 참고한 공개 프로젝트

- [svpcom/wfb-ng](https://github.com/svpcom/wfb-ng) — 무선 전송
- [svpcom/rtl8812au](https://github.com/svpcom/rtl8812au) — RTL8812AU WFB용 Linux 드라이버
- [OpenIPC/aviateur](https://github.com/OpenIPC/aviateur) — Windows 수신 프로그램 (v0.3.3)
- [OpenIPC/devourer](https://github.com/OpenIPC/devourer) — Aviateur에 들어 있는 사용자 공간 RTL8812AU 드라이버
- [Zadig](https://github.com/pbatard/libwdi) — Windows USB 드라이버 교체 도구
