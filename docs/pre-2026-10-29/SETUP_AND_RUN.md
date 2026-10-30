# 설치·빌드·실행 방법

이 문서는 10월 28일 수정 소스 `15f511c`를 재현하는 방법이다. 아래의 재현용 명령은 저장소 소스와 기존 워크플로에서 정리했으며, 원래 PC에서 실행한 모든 터미널 기록을 복원한 것은 아니다.

## 1. 실행과 소스 빌드는 다르다

| 목적 | 필요한 것 |
|---|---|
| 만들어진 수신 프로그램 실행 | Windows x64, 대상 동글, 해당 장치의 USB 드라이버, 패키지 전체, 필요시 VC++ x64 런타임 |
| 수정 소스 직접 빌드 | 위 실행 조건과 별개로 Git·CMake·MSVC C++ 빌드 도구·vcpkg·의존 라이브러리 |
| GitHub Actions 빌드 파일 사용 | GitHub가 빌드한 아티팩트. 수신 PC에 컴파일러를 설치할 필요 없음 |

## 2. 이미 성공한 Windows 빌드 받기

[패치 1.1 Windows 빌드](https://github.com/0315KSW/aviateur/actions/runs/40653795188)

1. 위 실행 페이지에서 `Artifacts`를 찾는다.
2. `Aviateur_lowlat_windows_x86_64`를 내려받는다. 다운로드에 GitHub 로그인이 필요할 수 있다.
3. ZIP 전체를 한 폴더에 푼다. `aviateur.exe`만 따로 복사하지 않는다.
4. `BUILD_COMMIT.txt`가 `15f511c`인지 확인한다.
5. 패키지에 `assets`, DLL, `run_lowlat.bat`, `run_urbs8.bat` 등이 함께 있는지 확인한다.

이 아티팩트는 2026년 10월 30일 확인 시 만료되지 않은 상태였다. 나중에 만료되면 같은 소스로 다시 빌드해야 한다. 소스 ZIP을 받는 것과 Windows 실행 패키지를 받는 것은 다르다.

## 3. 동글 드라이버와 수신 설정

현재 작업 기록은 Zadig로 대상 동글의 드라이버를 WinUSB로 교체한 구성이다.

1. 동글의 정확한 제품명·칩셋·USB VID/PID를 확인한다.
2. Zadig의 `Options → List All Devices`에서 해당 장치를 선택한다.
3. 대상 장치가 맞는지 확인하고 WinUSB를 적용한다. USB ID를 임의로 `0BDA:8812`라고 가정하지 않는다.
4. Aviateur를 실행하고 해당 동글이 목록에 보이는지 확인한다.
5. 송신 측과 수신 측의 Key·Channel·Width를 맞춘다.

공식 Aviateur README는 libusb 드라이버 설치를 안내한다. 이 문서의 WinUSB는 사용자가 설명한 실제 적용 방식이다. WinUSB와 devourer는 동일한 드라이버가 아니며, WinUSB를 통해 USB에 접근하고 devourer가 앱에서 동글을 제어한다.

실제 운용 키 내용은 이 문서에 넣지 않았다. 기존 키를 사용하고, 새 키를 만들 때는 송수신 쌍을 함께 관리한다. 저장소의 샘플 키가 실제 시험 키였다고 가정하지 않는다.

## 4. 수정본 실행

### 패키지 폴더에서 실행

처음에는 기준 설정인 `run_urbs8.bat`를 실행한다.

```bat
run_urbs8.bat
```

같은 조건에서 비교할 때만 다음 실행기를 사용한다.

```bat
run_urbs16.bat
```

```bat
run_urbs32.bat
```

이는 8·16·32 중 최적값이 결정됐다는 뜻이 아니다. 한 번에 하나의 수신 앱만 실행하고 이전 앱은 창의 X로 정상 종료한다.

### 실제 배치 파일의 핵심

`run_urbs16.bat`:

```bat
@echo off
call "%~dp0run_lowlat.bat" 16
```

`run_lowlat.bat`에서 설정을 적용하는 부분:

```bat
set URBS=%1
if "%URBS%"=="" set URBS=8
set DEVOURER_RX_URBS=%URBS%
set DEVOURER_RX_RING_MS=1000
```

- `%~dp0`: 배치 파일이 있는 폴더.
- `%1`: 첫 번째 인자. 여기서는 USB 수신 요청 수.
- `DEVOURER_RX_RING_MS=1000`: 진단 주기를 1000ms로 설정.
- 앱 안의 `devourer_env.h`가 이 값을 읽어 장치 생성 설정에 전달한다.

## 5. 로그 확인

| 파일 | 내용 |
|---|---|
| `logs/aviateur_<시각>.log` | 앱 로그, 적용 설정, 통계·끊김·오류 이벤트 |
| `logs/link_<시각>.csv` | 약 1초 간격 수신·FEC·RTP·프레임·큐 통계 |
| `logs/console_<시각>_urbsN.txt` | 실행 배치가 저장한 콘솔 출력 |

로그에서 다음을 확인한다. 아래는 찾을 메시지 형식이며, 실제 시험 출력으로 제시한 값은 아니다.

```text
[DRIVER] devourer RX config: urbs=...
Using hardware decoder: ...
[STATS] ...
[EVENT] link lost: ...
[EVENT] link restored: ...
[EVENT] recovery: first rendered frame ...
```

`Found hardware decoder`는 사용 가능한 후보 목록이다. 실제 선택은 `Using hardware decoder`와 재생 성공을 함께 확인한다. 기존 사용자 설명의 DXVA2 사용도 이 로그로 증빙한다.

로그 시각은 코드상 PC 로컬 시각이다. 문서의 KST 시험표와 비교하려면 PC 시간대와 시계를 확인한다. 서로 다른 PC의 시각을 빼서 지연을 계산하려면 시계 동기화 오차도 고려해야 한다.

## 6. 소스 직접 빌드 — 재현 절차

아래는 새 개발 폴더에서 사용하는 PowerShell 예시다. Windows 개발 PC에 Git, CMake, MSVC C++ 빌드 도구와 Windows SDK가 필요하다. `x64 Native Tools` 또는 Developer PowerShell에서 실행한다. 기존 성공 CI는 `windows-2025` 러너를 사용했다.

### 6.1 정확한 수정 소스 받기

```powershell
git clone --branch receiver-lowlat https://github.com/0315KSW/aviateur.git aviateur-pre1029
cd aviateur-pre1029
git checkout --detach 15f511c474db3fb0ab90c19c3d5233304fc443a5
git submodule update --init --recursive -- 3rd/json 3rd/mINI 3rd/SDL 3rd/vecgui
git submodule update --init -- 3rd/devourer
```

- `--branch receiver-lowlat`: 수정 브랜치를 선택.
- `checkout --detach <SHA>`: 문서 기준 코드로 고정. 이후 문서 추가나 upstream 변경과 분리해 재현.
- `submodule update`: 해당 커밋에 기록된 외부 소스를 받음.
- devourer의 내부 서브모듈까지 무조건 재귀 초기화하는 방식 대신, 기존 성공 워크플로의 명령을 따른다.

### 6.2 vcpkg와 의존 라이브러리

소스 폴더 안에서 실행하는 예시다.

```powershell
git clone https://github.com/Microsoft/vcpkg.git vcpkg
.\vcpkg\bootstrap-vcpkg.bat
$env:VCPKG_ROOT = (Resolve-Path .\vcpkg).Path
.\vcpkg\vcpkg.exe install libusb:x64-windows ffmpeg:x64-windows libsodium:x64-windows
```

SDL3·vecgui·JSON·mINI는 저장소 서브모듈로도 제공된다. 실제 연결 관계는 `CMakeLists.txt`에 있다. 당시 vcpkg의 정확한 커밋과 라이브러리 버전 목록은 이 자료에 없어 바이트 단위로 동일한 실행 파일 생성까지 보장하는 재현 절차는 아니다.

### 6.3 구성·컴파일

```powershell
cmake -S . -B build -A x64 "-DCMAKE_TOOLCHAIN_FILE=$env:VCPKG_ROOT/scripts/buildsystems/vcpkg.cmake" -DVECGUI_VULKAN=OFF
cmake --build build --config Release --target aviateur
```

- CMake가 빌드 구성을 만들고 MSVC가 C++ 소스를 컴파일한다.
- `Release`는 최적화 빌드다.
- `VECGUI_VULKAN=OFF`는 기존 lowlat Windows 워크플로와 같은 옵션이다.
- 이 옵션은 FFmpeg 하드웨어 디코딩 사용 여부와 별개다.

### 6.4 배포 폴더 구성

저장소의 `.github/workflows/lowlat-windows.yml` 패키징 단계 기준:

1. 빌드된 `aviateur.exe`가 있는 폴더의 출력물을 모은다.
2. `build/bin/assets`를 같은 배포 폴더의 `assets`로 복사한다.
3. vcpkg `installed/x64-windows/bin`의 필요한 DLL을 함께 배치한다.
4. `lowlat/*.bat`와 `README_LOWLAT.md`를 복사한다.
5. `BUILD_COMMIT.txt`에 소스 커밋을 남긴다.

로컬 빌드는 이 문서를 작성하며 새로 실행하지 않았다. 확인한 빌드 성공 근거는 10월 28일 GitHub Actions 기록이다.

## 7. 송신 실행 방법에서 아직 필요한 자료

수신기 저장소에는 당시 실제 송신 명령·송신 영상 파일·WFB-ng 설정 파일 전체가 없다. 따라서 임의의 `wfb_tx` 명령이나 GStreamer 파이프라인을 과거에 사용한 코드처럼 넣지 않았다.

추가할 자료:

- 실제 사용한 송신 스크립트 또는 셸 명령.
- 영상 입력 장치·원본 파일과 인코딩 설정.
- 동글 모델·드라이버·인터페이스, 채널·채널 폭·MCS·FEC·출력.
- link ID·radio port·UDP 포트와 키 파일 지정 방법.
- 초기 AGX Orin GStreamer 시험의 송신·수신 명령 원본.

수집할 때 키 바이트·비밀번호 등 인증정보는 문서에 넣지 않고 경로·설정 방법만 기록한다.
