# 04. 수신 (Windows PC)

## 1. 수신 PC

| 항목 | 값 |
|---|---|
| OS | Windows (랩실 PC) |
| GPU / CPU / RAM | NVIDIA GeForce RTX 4060 / Intel Core Ultra 7 265KF / 16GB |
| 무선 동글 | AC1200 RTL8812AU, USB ID `0BDA:8812` |
| 연결 | USB 2.0 속도로 동작 (Aviateur 로그: `Running USB bus at 480MBit/s`) |

## 2. 수신 프로그램 선택

| 후보 | 결과 |
|---|---|
| OpenIPC **Aviateur** v0.3.3 | **선택.** 공식 GitHub Releases에 Windows 빌드가 있고, WFB-ng 수신 + RTL8812AU 사용자 공간 드라이버(devourer) + FFmpeg 하드웨어 디코딩이 한 프로그램에 들어 있음. 리눅스 드라이버나 듀얼부팅 없이 Windows에서 바로 수신 가능 |
| fpv4win | 제외. 확인 당시 Releases에 배포 파일이 없고 Actions 실행 기록도 0건이라 공식 빌드를 받을 수 없었음 |

Windows에는 리눅스처럼 모니터 모드를 지원하는 커널 드라이버가 없어서, Aviateur는 동글을 WinUSB로 잡고 devourer가 USB로 칩을 직접 제어합니다. 그래서 Zadig로 드라이버를 바꿔야 합니다.

## 3. 설치 (공식 출처만 사용)

### 3-1. Zadig로 드라이버 교체
1. Zadig 공식 사이트(https://zadig.akeo.ie)에서 Zadig 2.9(사용 버전 2.9.788)를 받습니다.
2. 동글을 꽂고 Zadig 실행 → `Options > List All Devices`.
3. 목록에서 **`802.11n NIC`** (USB ID `0BDA 8812`)를 선택합니다.
4. 현재 드라이버 `RtlWlanu` → 대상 **WinUSB (v6.1.7600.16385)** 로 `Replace Driver`.

주의
- 교체한 동글은 Windows에서 일반 와이파이로 쓸 수 없습니다(수신 전용이 됨). PC의 다른 네트워크에는 영향 없습니다.
- 되돌리려면 장치 관리자에서 해당 장치 드라이버를 제거/롤백합니다.

### 3-2. Aviateur
1. 공식 릴리스 https://github.com/OpenIPC/aviateur/releases/tag/v0.3.3 에서 `Aviateur_0.3.3_windows_x86_64.zip`을 받습니다.
   - SHA-256: `2b93d8c00020b88476e30eaf8a08fc3f28bb2f75cf72bbe77dfc93f3ad6e74a4`
2. 압축을 풀고 `aviateur.exe` 실행.
3. 이후에는 이 원본 대신 수정본(패치 1.1)을 씁니다 → [05](05_수신기_패치.md).

### 3-3. 키
- 송신 쪽에서 `wfb_keygen`으로 만든 `gs.key`를 받아 수신 PC에 둡니다. 송신은 짝인 `drone.key`를 씁니다.
- 키는 저장소에 올리지 않습니다.

## 4. Aviateur 설정 (10/28 시험 기준)

| 항목 | 값 | 비고 |
|---|---|---|
| 장치 | `RTL8812AU [1:3]` | 설정 파일 `pid_vid` |
| 채널 | 5180MHz [36] | 송신과 같아야 함 |
| 채널 폭 | 20MHz | `channel_width_mode=0` |
| Key | `Open`으로 `gs.key` 선택 | |
| Codec | H265 | 설정 파일 `[localhost] codec=H265` |
| Forward RTP | 끔 | |
| Adaptive link (alink) | 끔 | `alink_enabled=false` |
| Force software decoding | **끔** | 켜 두면 CPU 디코딩. 12:46 시험은 켜진 상태였음 |
| 렌더러 | OpenGL | 설정 파일 `render_backend=opengl` |
| 시작 | `Start` (F5) | |

link ID와 영상 radio port는 Aviateur 기본값(7669206 / 0)이 송신과 같아서 따로 바꾸지 않았습니다.

## 5. 수신 확인 방법

정상 수신 시 로그 순서 (10/28 13:3x 로그에서 확인):

```
devourer [I] USB device 0bda:8812
devourer [I] Running USB bus at 480MBit/s
...
1124700873  SESSION  0:1:8:12                       ← WFB 세션 키 수신 (FEC 8/12)
[INFO] Generated SDP: Codec: H265, Payload type: 96 ← RTP 영상 확인
[INFO] SPS + PPS + Keyframe received. Starting decoder.
[INFO] Resizing render image to 3840x2160          ← 화면 표시 시작
```

- `SESSION` 줄 이전의 `Unable to decrypt packet`은 세션 키를 받기 전에 들어온 데이터라 시작 시 잠깐 나올 수 있습니다(13:3x 로그 71건 모두 SESSION 이전).
- 손실이 있으면 `RTP packets lost: N`, 참조 프레임이 깨지면 FFmpeg `Could not find ref with POC ...`가 나옵니다.

## 6. 로그 받는 법

### 원본 v0.3.3
로그가 숨겨진 콘솔로만 나가므로 cmd에서 리다이렉트합니다.
```bat
cmd /c "aviateur.exe > %USERPROFILE%\Downloads\aviateur_log.txt 2>&1"
```

### 수정본(패치 1.1)
`aviateur.exe` 옆 `logs\` 폴더에 자동 저장됩니다.

| 파일 | 내용 |
|---|---|
| `logs\aviateur_<시각>.log` | 시각이 찍힌 전체 로그 |
| `logs\link_<시각>.csv` | 1초마다 수신 경로 전 구간 통계 |
| `logs\console_<시각>_urbsN.txt` | 배치 파일(`run_urbs8.bat` 등)로 실행했을 때 콘솔 출력(드라이버 메시지 포함) |

## 7. 수신 쪽 진행 기록

| 날짜·시각 | 내용 | 결과 | 로그 |
|---|---|---|---|
| 10/27 | Zadig·Aviateur 설치, 동글 인식 | 동작 | — |
| 10/27 20:33 | 첫 수신 시도 | `Unable to read rx secret key` 로 중단 | `20261027_2033_key_read_error.txt` |
| 10/27 20:42 | 키 재생성 후 수신 | WFB 세션·RTP 수신 성공. 디코더가 시작 못 함(`PPS id out of range` 965회) | `20261027_2042_pps_missing.txt` |
| 10/28 12:31 | Femto 실시간 수신 | 손실 심함 (109회, 2,399패킷, 최대 연속 173) | `20261028_1231_heavy_loss.txt` |
| 10/28 12:33 | 계속 수신 | 화면 표시 시작. 손실 137회, 256패킷 | `20261028_1233_picture_shown.txt` |
| 10/28 12:46 | 720p 수신 | 1280×720 디코더 시작, 손실 1회(6패킷). 소프트웨어 디코딩 강제 상태 | `20261028_1246_720p_decoder_start.txt` |
| 10/28 13:2x | 4K 녹화 파일 재전송 | 3840×2160 표시되나 깨짐 (손실 1,248회, 2,324패킷) | 원본 로그 미보존 |
| 10/28 13:3x | 같은 파일, 송신 출력 15dBm 뒤 | 거의 그대로 (1,239회, 2,297패킷), DXVA2 하드웨어 디코딩 | `20261028_133x_4k_replay_txpower15.txt` |
| 10/28 15:11~15:40 | 수정본 패치 1 빌드 | 성공 | — |
| 10/28 16:32~16:37 | 패치 1로 세 회차 수신 | 회차마다 손실이 크게 다름 → [06](06_시험결과.md) | `link_20261028_163234.csv` |
| 10/28 17:00~17:37 | 패치 1.1 빌드 (로그 파일 빈 줄 수정) | 성공. 실제 장비 실행은 아직 | — |

시험 결과 상세는 [06 시험 결과](06_시험결과.md), 문제별 원인과 조치는 [07 문제와 해결](07_문제와해결.md).
