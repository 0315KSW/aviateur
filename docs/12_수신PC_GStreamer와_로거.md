# 12. 수신 PC (GStreamer)와 링크 로거 — 11/3~11/6

11/2 회의에서 업체가 "FFmpeg 기반(Aviateur)은 지연을 못 줄인다, GStreamer를 그대로 써라"고 해서 PC 수신을 GStreamer로 바꿨습니다.
결과적으로 PC 수신은 지연의 주원인이 아니었습니다(바꾼 뒤에도 거의 같음 → [14](14_11-05~06_지연측정.md)). 그래도 GPU에서 디코딩·표시까지 끝나는 경로라 이쪽을 기준으로 씁니다.

## 설치 (Windows PC, RTX 4060)
1. 공식 사이트 <https://gstreamer.freedesktop.org/download/> 에서 **MSVC 64-bit runtime 1.28.8** 설치 파일 `gstreamer-1.0-msvc-x86_64-1.28.8.exe`를 받습니다.
2. "모든 사용자용"으로 설치합니다. 설치 위치: `C:\Program Files\gstreamer\1.0\msvc_x86_64`
3. PATH는 자동으로 잡히지 않습니다. 배치 파일은 `bin` 폴더로 이동한 뒤 실행하도록 만들었습니다.
4. 디코더 확인:
```
"C:\Program Files\gstreamer\1.0\msvc_x86_64\bin\gst-inspect-1.0.exe" d3d11h264dec
```

## 수신 파이프라인
```
gst-launch-1.0 -v udpsrc port=5600 buffer-size=2097152 caps="application/x-rtp,media=video,clock-rate=90000,encoding-name=H264,payload=96" ! rtph264depay ! h264parse ! d3d11h264dec compliance=flexible ! d3d11videosink sync=false
```

| 요소 | 이유 |
|---|---|
| `udpsrc buffer-size=2097152` | 소켓 수신 버퍼 2MB. 순간적으로 몰리는 패킷을 OS가 버리지 않게 함 (Aviateur도 2MB) |
| `rtph264depay ! h264parse` | RTP를 H.264 프레임으로 조립 |
| `d3d11h264dec` | GPU(D3D11) 하드웨어 디코딩. 출력이 D3D11 메모리라 프레임이 CPU로 내려오지 않고 GPU에서 바로 표시됨 |
| `compliance=flexible` | 디코더를 저지연 모드로 둠. 이 값이 없으면 재정렬 정보가 없는 High 프로파일 스트림에서 DPB 크기(1080p L4.0 기준 4프레임)만큼 쌓아 두고 내보낼 수 있음. Pi 인코더는 B프레임 0이라 재정렬이 필요 없음 |
| `d3d11videosink sync=false` | 타임스탬프 맞추느라 기다리지 않고 들어오는 즉시 표시 |

화면 fps 확인용(`-v` 출력에 초당 fps가 찍힘):
```
gst-launch-1.0 -v udpsrc port=5600 buffer-size=2097152 caps="application/x-rtp,media=video,clock-rate=90000,encoding-name=H264,payload=96" ! rtph264depay ! h264parse ! d3d11h264dec compliance=flexible ! fpsdisplaysink video-sink=d3d11videosink text-overlay=false sync=false
```

## 실행 파일 (`receiver/windows/`)
| 파일 | 하는 일 |
|---|---|
| `gst_rx_5600.bat` | AGX가 넘겨 주는 무선 경로 영상(UDP 5600)을 표시 |
| `gst_rx_5610.bat` | 카메라가 PC로 직접 보내는 유선 비교 경로(UDP 5610)를 표시 |
| `agx_test.bat` | 시험 한 번을 통째로 실행: Aviateur 종료 → `gst_rx_5600.bat` 창 띄움 → `wfb_link_log.py`를 AGX에 복사 → AGX에서 로거 실행 → Ctrl+C로 끝내면 AGX의 CSV를 bat 폴더로 가져옴 |

`agx_test.bat`은 실행할 때 AGX 계정 이름을 묻습니다. AGX 주소는 192.168.0.68로 고정돼 있습니다.
AGX 비밀번호를 여러 번 묻는 것(scp, ssh, sudo)은 정상입니다.

## 링크 로거 (`tools/wfb_link_log.py`)
WFB-ng 서비스의 JSON 통계 API에 붙어 1초마다 무선 상태를 CSV로 남깁니다. 표준 라이브러리만 써서 설치할 것이 없습니다.

| 실행 위치 | 명령 | 결과 |
|---|---|---|
| 수신 AGX (gs) | `python3 wfb_link_log.py` | `~/wfb_logs/wfb_<시각>.csv` (+ 원본 `.jsonl`) |
| 송신 미니PC (drone) | `python3 wfb_link_log.py --port 8102` | `~/wfb_logs/wfb_<시각>_tx.csv` |

수신 쪽 주요 값
| 값 | 뜻 |
|---|---|
| `recv` | 안테나로 받은 전체 패킷 (FEC 여분 포함) |
| `out` | 복호·FEC 복구 후 PC로 내보낸 패킷 |
| `lost` | FEC로도 못 살린 패킷. **화면이 깨지는지는 이 값으로 판단** |
| `fec_rec` | FEC로 살린 패킷. 많아도 `lost`가 0이면 화면은 정상 |
| `RSSI` | 안테나별 평균 수신 세기 |

송신 쪽 주요 값: `in`(카메라에서 들어온 패킷), `injected`(무선에 쏜 패킷, FEC 포함), `dropped`(송신 단계에서 버린 패킷).

손실률은 이 문서들에서 `lost ÷ (out + lost)`로 계산합니다.

### 주의
- Windows 콘솔에서 마우스로 글자를 선택하면(빠른 편집 모드) 출력이 멈춥니다. Esc를 누르면 다시 흐릅니다.
- PyPI의 `gstreamer-plugins` 1.28.8은 Windows용이 free-threaded 파이썬 빌드뿐이라 쓰지 않았습니다. 그래서 PC가 아니라 AGX의 WFB 통계를 기록합니다.

## 지연 재는 법 (11/6에 정한 방법)
- **한 시계만 씁니다.** 수신 PC 모니터에 시계 페이지(예: <https://time.navyism.com/>)를 띄우고, 카메라가 그 모니터를 찍게 합니다. 같은 모니터에 수신 영상 창을 띄워 화면을 녹화하면, 녹화 한 장면 안에서 "실제 시계"와 "영상 속 시계"의 차이가 지연입니다.
- 두 PC의 시계를 비교하는 방법은 쓰지 않습니다. 시계 페이지를 새로 고칠 때마다 두 PC 사이가 수백 ms까지 어긋났습니다(측정값 690ms, 385ms가 이 때문에 나옴).
- 한 녹화 안에서 두 수신 창(예: 5600과 5610)을 비교하는 것도 같은 시계 기준이라 믿을 수 있습니다.
