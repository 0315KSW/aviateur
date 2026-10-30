# tx/ — 송신(GX10) 스크립트와 설정

송신 담당이 GX10의 `/home/ikw/wfb-gx10/`에서 아래 파일을 옮겨 올릴 예정입니다.

| 올릴 것 | 내용 |
|---|---|
| 드라이버 설치 절차 | `88XXau_wfb` DKMS 설치, Secure Boot 키 등록 명령 |
| WFB-ng 설정 | 송신(drone) 프로필, 채널·MCS·FEC·출력 설정 파일 (키 제외) |
| 영상 송신 스크립트 | FFmpeg(`mjpeg_cuvid` → `hevc_nvenc` → RTP, UDP 5602) 전체 명령, GStreamer 녹화 파일 송신 명령 |
| 시험 기록 스크립트 | 초당 입력·주입·드롭 기록 방법 |

`drone.key`, `gs.key`는 올리지 않습니다(`.gitignore`에 `*.key`).
