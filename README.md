# OverLay-Trans (가칭)

해외 스트리머 방송의 언어 장벽을 무너뜨리는 **온디바이스 실시간 AI 자막/번역 애플리케이션**, OverLay-Trans의 저장소입니다.

## 목차

1. [프로젝트 소개](#1-프로젝트-소개)
2. [기술 스택](#2-기술-스택)
3. [단계별 개발 순서 (로드맵)](#3-️-단계별-개발-순서-로드맵)
4. [프로젝트 폴더 구조](#4-프로젝트-폴더-구조)
5. [개발 컨벤션](#5--개발-컨벤션)
6. [Git 브랜치 및 커밋 전략](#6-git-브랜치-및-커밋-전략)
7. [PR 및 코드 리뷰 규칙 (Pn 룰)](#7-pr-및-코드-리뷰-규칙-pn-룰)
8. [빌드 및 실행 방식](#8-빌드-및-실행-방식)
9. [추후 지원 예정 (현재 범위 제외)](#9-추후-지원-예정-현재-범위-제외)

---

## 1. 프로젝트 소개

- **앱 이름**: OverLay-Trans
- **한 줄 설명**: 스트리머 방송의 시스템 오디오를 캡처하여 실시간 온디바이스 STT 및 경량 LLM 번역을 거쳐, 브라우저의 방송 영상 위에 자막을 띄우는 오버레이 번역 서비스입니다.
- **서비스 목표**: 서버 통신 비용 없이 C/C++ 기반 100% 온디바이스 구동을 목표로 하며, 인터넷 방송 특유의 신조어와 화자 분리(Diarization) 맥락을 반영하여 시청자에게 완벽한 몰입감을 제공합니다.
- **개발 형태**: 1인 개발 (기획부터 C++ 코어, 오버레이 UI까지 전 영역 단독 진행)

## 2. 기술 스택

Windows x64 온디바이스 구동을 기준으로 한 기술 스택입니다. 핵심 로직은 플랫폼 독립적인 C++ 코어로 분리해 추후 다른 아키텍처·플랫폼으로 확장할 수 있도록 합니다.

| 분류 | 기술 스택 | 상세 환경 / 주요 라이브러리 |
| --- | --- | --- |
| Core Language | C++20 | 핵심 비즈니스 로직 및 AI 추론 엔진 구동 |
| VAD | Silero VAD | whisper.cpp 내장 VAD 사용 (발화 구간 검출) |
| STT Engine | whisper.cpp | 온디바이스 음성 인식 |
| LLM Engine | llama.cpp | GGUF 경량 모델 기반 문맥 번역 |
| 목소리 비교 | sherpa-onnx | 앞뒤 발화의 목소리 특징(3D-Speaker CAM++)을 비교해 다른 사람의 말은 합치지 않음. 빌드할 때 공식 배포 바이너리를 내려받음 |
| GPU 가속 | Vulkan (`ggml-vulkan`) | Radeon(x86_64) 데스크톱 GPU 백엔드. STT/LLM을 CPU와 GPU 중 어디에 둘지는 벤치마크로 결정 |
| Audio Capture | miniaudio (WASAPI) | Windows 루프백 캡처 (16kHz 모노) |
| 자막 표시 | 브라우저 확장 프로그램 (Manifest V3) | 앱의 로컬 서버에서 자막을 받아 방송 영상 위에 표시 (트위치, 유튜브) |
| 배포 | Inno Setup, GitHub Actions | 태그를 올리면 Windows 설치 프로그램을 빌드해 릴리즈 초안에 등록 |
| Target Architecture | x86_64 (Windows) | 라데온 x64 데스크톱 |
| Build System | CMake (아키텍처별 프리셋) | C++ 라이브러리 관리 및 빌드 |

## 3. 🗺️ 단계별 개발 순서 (로드맵)

프로젝트는 Windows x64에서 코어 엔진과 오버레이를 구현·검증하는 **Phase 1**을 진행합니다. Windows ARM64와 Android는 현재 범위에서 제외했으며, 계획은 [9. 추후 지원 예정](#9-추후-지원-예정-현재-범위-제외)에 정리했습니다.

### Phase 1: Windows Core Development (구현 및 검증)

> 개발/검증은 x64 라데온 데스크톱 환경에서 진행합니다.

- **Step 0: 빌드 환경 구성**
  - x64(MSVC) CMake 프리셋(`CMakePresets.json`) 작성 — 아키텍처별 프리셋 구조로 두어 추후 프리셋 추가만으로 확장할 수 있게 구성
  - ggml Vulkan 백엔드(`ggml-vulkan`) 빌드 옵션 구성 및 라데온(x64)에서 동작 확인
  - CI 없이 로컬에서 빌드를 검증하는 체크리스트 작성 (1인 개발 환경 고려)
- **Step 1: 오디오 루프백 캡처**
  - `IMMDeviceEnumerator` / `IAudioClient`를 공유 모드(Shared Mode) + 루프백(Loopback) 플래그로 초기화
  - `miniaudio.h` 디바이스 콜백에서 PCM 프레임 수신 및 포맷 변환 (float32 ↔ int16)
  - whisper.cpp 입력에 맞춘 16kHz 모노 변환
  - Lock-free 고리형 버퍼(Ring Buffer) 구현 및 오버플로우 정책 정의
  - 디버깅용 raw PCM/WAV 덤프 로깅 기능 추가
  - 검증: 캡처 지연 시간 및 샘플 드랍(drop) 유무 측정
- **Step 2: VAD 및 슬라이딩 윈도우 구현**
  - whisper.cpp 서브모듈 연동 (VAD와 STT가 함께 사용)
  - Silero VAD 적용 (whisper.cpp 내장 VAD, 모델 파일은 `core/models/`에 보관)
  - 슬라이딩 윈도우 크기/오버랩 정의
  - 발화 시작(Speech Onset)/종료(Speech Offset) 감지 및 무음 구간 트리밍
  - 게임 효과음/BGM 오탐 방지를 위한 임계값(threshold) 튜닝
  - 검출된 발화 세그먼트를 STT 큐로 전달하는 파이프라인 연결
- **Step 3: whisper.cpp 통합 (STT)**
  - whisper.cpp STT 빌드 설정 (CPU 프리셋 `windows-x64`, Vulkan 프리셋 `windows-x64-vulkan`)
  - base 다국어 GGML 모델 다운로드 및 로딩 루틴 구현
  - 청크 단위 스트리밍 추론 구조 설계 (이전 컨텍스트 유지, 문장 경계 처리)
  - 언어 자동 감지(auto-detect) 및 언어 강제 지정 옵션 제공
  - 벤치마크: CPU / Vulkan 구성별 RTF(Real-Time Factor) 측정, 한/일/영 인식 정확도 테스트
- **Step 4: llama.cpp 연동 (문맥 번역)**
  - llama.cpp 서브모듈 연동 및 번역용 경량 GGUF 모델 선정/양자화
  - 이전 대화 맥락 N턴을 포함한 System Prompt 설계
  - 인터넷 방송 신조어·은어 처리를 위한 프롬프트 가이드 및 사전(glossary) 구성
  - Token-by-Token 스트리밍 추론 구현 (KV 캐시 재사용으로 지연 시간 최소화)
  - STT → 번역 파이프라인 연결 및 번역 품질 회귀 테스트 케이스 작성
- **Step 5: 자막 오버레이 (브라우저 확장 프로그램)**
  - 방송을 브라우저로 보므로, 별도 창 대신 확장 프로그램이 영상 위에 자막을 그리는 방식으로 구현
  - 앱의 로컬 서버(페어링 키 인증)에서 자막을 받아 표시하고, 방송 정보와 채팅을 앱으로 전달
  - 말이 이어지면 앞의 자막과 합쳐 다시 번역하고 화면의 자막을 교체
  - E2E 통합 테스트: 캡처 → VAD → STT → LLM → 오버레이 전체 파이프라인 지연 시간(목표: 발화가 끝난 뒤 약 1.5초 이내) 측정
- **Step 6: 화자 분리 (Speaker Diarization)**
  - sherpa-onnx 연동 및 화자 임베딩 모델 선정
  - 화자 번호 매기기(클러스터링)와 pyannote 화자 분할은 여러 사람이 빠르게 주고받는 방송에서 화자 바뀜을 3~35%밖에 찾지 못해 보류
  - 대신 앞뒤 발화의 맞닿은 부분 목소리를 비교해, 다른 사람의 말은 앞 문장에 합치지 않음
  - 검증: 화자별 색이 들어간 유튜브 자막(클립 7개)을 정답으로 채점

## 4. 프로젝트 폴더 구조

플랫폼 독립 코어(`core/`)와 플랫폼별 쉘(`windows/`)을 분리한 Core-driven 구조로 구성합니다. 다른 플랫폼을 추가할 때는 `windows/`와 같은 층에 쉘 디렉토리만 더합니다.

```
.
├── core/                          # [C++] 플랫폼 독립적인 핵심 엔진
│   ├── audio/                     # 오디오 버퍼링 및 VAD 알고리즘 구현체
│   ├── inference/                 # whisper.cpp 및 llama.cpp 래핑 클래스
│   ├── server/                    # 브라우저 확장 프로그램과 통신하는 로컬 서버
│   ├── text/                      # 번역 결과 후처리 (가타카나 표기, 글자 종류 판별)
│   └── models/                    # GGUF 양자화 모델 파일 보관 (gitignore)
├── windows/                       # [Windows] 데스크톱 쉘
│   ├── capture/                   # WASAPI 루프백 캡처 (miniaudio), 오디오 파일 입력
│   └── main.cpp                   # 윈도우 실행 진입점
├── extension/                     # [브라우저] 영상 위에 자막을 표시하는 확장 프로그램
│   ├── background.js              # 앱의 로컬 서버에서 자막을 받아 페이지로 전달
│   ├── content/                   # 자막 표시 (sites/ 아래에 사이트별 화면 구조 코드)
│   └── popup/                     # 페어링 코드 입력과 설정
├── installer/                     # [배포] Windows 설치 프로그램 (Inno Setup)과 실행 스크립트
└── third_party/                   # 서브모듈로 관리하는 외부 라이브러리 (miniaudio, whisper.cpp, llama.cpp)
```

## 5. 🧑‍💻 개발 컨벤션

### 네이밍 규칙

C++ 코드는 아래 네이밍 규칙을 따릅니다.

| 대상 | 규칙 | 예시 |
| --- | --- | --- |
| 클래스/구조체 (C++) | PascalCase | `AudioCapture`, `InferenceEngine` |
| 일반 함수 / 변수 (C++) | snake_case | `start_audio_stream()`, `buffer_size` |
| 상수 / 매크로 (C++) | UPPER_SNAKE_CASE | `MAX_TOKEN_LENGTH` |

## 6. Git 브랜치 및 커밋 전략

### 브랜치 전략 (Git Flow 기반)

- `main`: 안정적인 빌드가 가능한 릴리즈 브랜치
- `dev`: 메인 개발 통합 브랜치
- `<type>/<설명>`: 단위 작업 브랜치. `dev`에서 분기하고 `dev`로 PR을 보냅니다.
  - `<type>`은 아래 커밋 Type과 동일하게 사용합니다.
  - `<설명>`은 영문 소문자 kebab-case로 작성합니다. (예: `feat/wasapi-loopback`, `chore/cmake-x64-skeleton`)
  - 브랜치 하나는 PR 하나로 끝나는 작은 단위로 유지하고, merge 후 삭제합니다.

### 커밋 Type

| Type | 사용 상황 | 예시 |
| --- | --- | --- |
| feat | 새로운 기능 추가 | `feat: WASAPI 루프백 캡처 추가` |
| fix | 버그 수정 | `fix: whisper 컨텍스트 해제 누락으로 인한 메모리 누수 수정` |
| refactor | 기능 변화 없는 코드 개선 | `refactor: 링 버퍼 인덱스 계산 단순화` |
| perf | 성능 개선 (지연 시간, 메모리 등) | `perf: 번역 추론 시 KV 캐시 재사용` |
| test | 테스트 추가/수정 | `test: 링 버퍼 오버플로우 케이스 추가` |
| docs | 문서 수정 | `docs: 빌드 방법 갱신` |
| chore | 설정, 빌드, 패키지, 환경 작업 | `chore: whisper.cpp 서브모듈 추가` |

### 커밋 컨벤션

```
<type>: <제목>

- <무엇을 왜 바꿨는지>
- <무엇을 왜 바꿨는지>
```

- **제목**: 한국어, 50자 이내, 마침표 없이 "추가 / 수정 / 제거 / 개선" 같은 명사형으로 끝냅니다.
- **본문**: 선택 사항입니다. 제목만으로 부족할 때 둘째 줄을 비우고 `- ` 항목으로 작성합니다. (한 줄 72자 이내)
- 커밋 하나에는 하나의 논리적 변경만 담습니다.

커밋 메시지 템플릿은 [.gitmessage](.gitmessage)에 있습니다. 저장소를 clone한 뒤 한 번만 등록하면 `git commit` 실행 시 에디터에 자동으로 채워집니다.

```bash
$ git config commit.template .gitmessage
```

## 7. PR 및 코드 리뷰 규칙 (Pn 룰)

- **PR 조건**: 로컬 빌드 검증 완료 후 개설
- **PR 대상**: 작업 브랜치 → `dev` (`dev` → `main`은 릴리즈 시점에만). GitHub의 base 기본값은 `main`이므로 PR 생성 시 `dev`로 변경합니다.
- **PR 제목**: 커밋 제목과 같은 형식 (`<type>: <제목>`)
- **PR 본문**: [.github/PULL_REQUEST_TEMPLATE.md](.github/PULL_REQUEST_TEMPLATE.md) 양식을 사용합니다. GitHub에서 PR을 열면 자동으로 채워집니다.
  - `개요`: 무엇을 왜 했는지 1~3문장
  - `변경 사항`: 변경 내용을 항목별로 정리
  - `검증`: 실제로 확인한 항목만 체크하고, 해당 없는 항목은 삭제
  - `남은 이슈 (Pn)`: 이 PR에서 해결하지 않고 남긴 것을 아래 Pn 등급과 함께 기록
- **Merge 방식**: Create a merge commit. 작업 브랜치의 커밋을 그대로 남기고 merge 커밋 하나를 추가합니다.
- **충돌 해결**: `dev`를 작업 브랜치에 merge해 머지 커밋 하나로 해결합니다. (rebase / force push는 사용하지 않습니다.)
- **이슈**: GitHub 이슈는 사용하지 않습니다. 작업 단위는 로드맵과 PR로 추적합니다.
- **코드 리뷰 Pn 규칙** (1인 개발 환경이므로 스스로 체크리스트 목적으로 사용)
  - `P1`: 반드시 해결해야 하는 치명적 이슈 (메모리 릭, 크래시 등)
  - `P2`: 최적화 및 리팩토링 고려 대상 (Latency 개선 등)
  - `P3`: 나중에 처리해도 되는 UI 디테일

## 8. 빌드 및 실행 방식

### 설치 프로그램으로 설치 (Windows x64, ARM64)

1. [Releases](https://github.com/higashiaka/Overlay-Trans/releases)에서 `OverlayTrans-Setup-<버전>.exe`를 받아 실행합니다. 설치 프로그램이 PC의 프로세서를 확인해 맞는 앱을 내려받으므로 인터넷 연결이 필요합니다. 관리자 권한 없이 사용자 폴더(`%LOCALAPPDATA%\Programs\OverlayTrans`)에 설치됩니다.
2. 바탕 화면의 OverLay-Trans를 실행합니다. 처음 실행할 때 모델(약 3GB)을 내려받습니다. 중간에 끊겨도 다시 실행하면 이어서 받습니다.
3. 준비가 끝나면 창이 숨겨지고 알림 영역(시계 옆)에 아이콘이 생깁니다. 아이콘을 오른쪽 클릭해 "페어링 코드 복사"를 누른 뒤, 아래 [브라우저 확장 프로그램](#브라우저-확장-프로그램-트위치-유튜브)의 순서대로 확장 프로그램을 연결합니다. 확장 프로그램은 설치 폴더의 `extension` 폴더에 있습니다.
4. 아이콘 메뉴에서 로그 창을 보거나 앱을 종료할 수 있습니다.

설치 프로그램이 고르는 앱은 프로세서에 따라 다릅니다.

| 프로세서 | 받는 앱 | 음성 인식 | 번역 |
| --- | --- | --- | --- |
| x64 (AMD, Intel) | Vulkan 빌드 | large-v3-turbo, GPU | Gemma 3 4B, GPU |
| ARM64 (스냅드래곤 X) | CPU 빌드 | small, CPU | Gemma 3 4B, CPU |

- x64는 Vulkan을 지원하는 그래픽 드라이버가 필요합니다. (최근의 AMD, NVIDIA, Intel 드라이버에 포함)
- 스냅드래곤 X는 GPU를 쓰지 않습니다. Adreno GPU로는 CPU보다 느렸습니다. ([측정 결과](#측정-결과-snapdragon-x-x1-26-100-8코어--adreno-x1-45-전원-연결))
- 실행 옵션은 설치 폴더의 `options.txt`에 한 줄에 하나씩 적습니다. 기본으로 `--language ja`가 들어 있고, x64에는 `--llm-gpu-layers 16`(VRAM 8GB 기준), ARM64에는 `--stt-model models\ggml-small.bin`이 더 들어 있습니다. 창을 숨기지 않으려면 `--background off`를 적습니다.
- 음성 인식 모델을 바꾸려면 `--stt-model models\ggml-base.bin`처럼 whisper.cpp 모델 이름을 적습니다. 없는 모델은 다음에 실행할 때 내려받습니다.
- 제거는 Windows 설정의 "설치된 앱"에서 합니다. 내려받은 모델도 함께 지워집니다.

### 소스에서 빌드

```bash
# 1. 저장소 및 서브모듈 클론
$ git clone --recursive https://github.com/higashiaka/Overlay-Trans.git
$ cd Overlay-Trans

# 2. VAD / STT / 번역 모델 다운로드 (core/models/는 git에 포함되지 않음. 번역 모델은 약 2.5GB)
$ curl -L -o core/models/ggml-silero-v6.2.0.bin https://huggingface.co/ggml-org/whisper-vad/resolve/main/ggml-silero-v6.2.0.bin
$ curl -L -o core/models/ggml-large-v3-turbo-q5_0.bin https://huggingface.co/ggerganov/whisper.cpp/resolve/main/ggml-large-v3-turbo-q5_0.bin
$ curl -L -o core/models/3dspeaker_speech_campplus_sv_zh_en_16k-common_advanced.onnx https://github.com/k2-fsa/sherpa-onnx/releases/download/speaker-recongition-models/3dspeaker_speech_campplus_sv_zh_en_16k-common_advanced.onnx
$ curl -L -o core/models/gemma-3-4b-it-Q4_K_M.gguf https://huggingface.co/ggml-org/gemma-3-4b-it-GGUF/resolve/main/gemma-3-4b-it-Q4_K_M.gguf

# 3. Windows x64 빌드 (Visual Studio 2022, CMake 3.21 이상)
$ cmake --preset windows-x64
$ cmake --build --preset windows-x64

# 4. 실행 (저장소 루트에서 실행)
$ ./build/windows-x64/windows/Release/OverlayTransWin.exe

# 언어를 지정하면 자동 감지보다 빠름. 모델 경로는 --stt-model, --llm-model, --vad-model로 바꿀 수 있음
$ ./build/windows-x64/windows/Release/OverlayTransWin.exe --language ja

# STT에 자주 나오는 이름과 용어를 미리 알려 주면 그 표기로 인식될 확률이 올라감
$ ./build/windows-x64/windows/Release/OverlayTransWin.exe --language ja --stt-hint "ビッツ、サブスク、ギフト"

# 번역 용어집: UTF-8 텍스트 파일에 한 줄에 하나씩 "원문 = 번역" 형식으로 적음 (예: 明後日 = 모레)
$ ./build/windows-x64/windows/Release/OverlayTransWin.exe --language ja --glossary glossary.txt

# 오디오/영상 파일(MP4, M4A, MP3, WAV, FLAC 등)을 재생 없이 바로 처리. 같은 파일로 모델과 설정을 반복 비교할 때 사용
# 테스트용 파일은 sample/ 폴더에 두면 git에 포함되지 않음
$ ./build/windows-x64/windows/Release/OverlayTransWin.exe --language ja --input sample/recording.mp4

# 말이 잠깐 멈추면 발화가 끝났다고 확정되기 전에 인식을 미리 시작해 자막을 앞당김 (기본으로 켜짐). 끄려면 off
$ ./build/windows-x64/windows/Release/OverlayTransWin.exe --language ja --early-stt off

# 끊어서 내보낸 말이 1초 안에 이어지면, 앞의 말과 합쳐 다시 번역해 화면의 자막을 바꿈 (기본으로 켜짐). 끄려면 off
$ ./build/windows-x64/windows/Release/OverlayTransWin.exe --language ja --merge-sentences off

# 번역문이 다 만들어지기 전에, 만들어진 부분부터 자막으로 보냄 (기본으로 켜짐). 끄려면 off
$ ./build/windows-x64/windows/Release/OverlayTransWin.exe --language ja --stream-subtitles off

# STT가 한 번에 계산하는 길이 (한 칸에 20ms). 기본은 768칸으로, 모델 전체(1500칸=30초)를 계산할 때보다
# 인식 시간이 1/3 정도로 줄지만 인식 결과가 일부 달라짐. 발화가 더 길면 그 발화만 자동으로 늘림. 0이면 모델 전체를 계산
$ ./build/windows-x64/windows/Release/OverlayTransWin.exe --language ja --stt-audio-ctx 0

# 시작한 뒤 콘솔 창을 숨기고 알림 영역 아이콘으로만 둠 (설치본은 기본으로 켜짐). 아이콘 메뉴로 다시 볼 수 있음
$ ./build/windows-x64/windows/Release/OverlayTransWin.exe --language ja --background on

# 이어진 말을 합칠 때 앞뒤 목소리를 비교해, 다른 사람(게임 음성, 함께 방송하는 사람)의 말은 합치지 않음 (기본으로 켜짐)
# 로그에 "voice 유사도"와 합치지 않은 경우 "other voice"가 표시됨. 끄려면 off
$ ./build/windows-x64/windows/Release/OverlayTransWin.exe --language ja --voice-match off

# (디버깅) 캡처한 오디오를 WAV 파일로 저장
$ ./build/windows-x64/windows/Release/OverlayTransWin.exe --dump-wav capture.wav
```

### 브라우저 확장 프로그램 (트위치, 유튜브)

`extension/` 폴더의 확장 프로그램이 앱에서 자막을 받아 트위치와 유튜브 영상 위에 표시합니다. 크롬 기반 브라우저(크롬, 웨일, 엣지)에서 동작합니다.

1. 브라우저 주소창에 `chrome://extensions`(웨일은 `whale://extensions`)를 입력해 엽니다.
2. "개발자 모드"를 켜고 "압축해제된 확장 프로그램을 로드합니다"를 눌러 `extension` 폴더를 선택합니다.
3. 앱을 실행하고, 콘솔에 출력된 `Pairing code: ...` 값을 복사합니다. (알림 영역 아이콘 메뉴의 "페어링 코드 복사"로도 복사할 수 있습니다.)
4. 브라우저의 확장 프로그램 아이콘을 눌러 페어링 코드를 입력하고 "연결"을 누릅니다. (한 번만 하면 됩니다.)
5. 트위치나 유튜브에서 방송을 열면 영상 아래쪽에 자막이 표시됩니다. 이미 열려 있던 탭은 새로 고칩니다.

사이트 화면 구조에 의존하는 코드는 `extension/content/sites/`에 사이트별 파일로 분리되어 있습니다.

앱은 실행되면 이 PC 안에서만 접속할 수 있는 로컬 서버(`127.0.0.1:47815`)를 열고, 시작할 때 페어링 코드를 출력합니다.

- 페어링 코드는 `포트-키` 형식이며, 키는 `%LOCALAPPDATA%\OverlayTrans\pairing-token.txt`에 저장되어 다음 실행에도 그대로 쓰입니다.
- 키가 맞지 않는 요청과 일반 웹페이지에서 온 요청은 거부합니다.
- 포트는 `--port <번호>`로 바꿀 수 있습니다. 포트가 이미 쓰이고 있으면 앱이 시작되지 않습니다.

| 요청 | 설명 |
| --- | --- |
| `GET /v1/ping` | 연결과 페어링 키 확인 |
| `GET /v1/subtitles?after=<번호>` | 해당 번호 이후의 자막(원문, 번역). 없으면 새 자막이 나올 때까지 최대 20초 기다림. 자막의 `replaces`가 0이 아니면 화면에 있는 그 번호의 자막을 이 자막으로 바꿈 |
| `POST /v1/context` | 방송 정보 전달 (JSON: `channel`, `title`, `category`). 번역 지시문에 쓰임 |
| `POST /v1/chat` | 새로 올라온 채팅 전달 (JSON: `messages[{name, text}]`). 앱을 `--chat-context on`으로 실행한 경우에만 직후 발화의 번역 맥락으로 쓰임 (기본은 꺼짐) |

모든 요청에 `Authorization: Bearer <키>` 헤더가 필요합니다.

### GPU(Vulkan) 가속 빌드

[Vulkan SDK](https://vulkan.lunarg.com/sdk/home) 설치가 필요합니다. (`winget install KhronosGroup.VulkanSDK`)

```bash
$ cmake --preset windows-x64-vulkan
$ cmake --build --preset windows-x64-vulkan

# 기본으로 GPU 0번을 사용. --gpu-device <번호>로 장치를 바꾸고, --device cpu로 CPU를 강제할 수 있음
$ ./build/vk/windows/Release/OverlayTransWin.exe --language ja
```

- Vulkan 빌드는 폴더가 깊어 Windows 경로 길이 제한(260자)에 걸릴 수 있습니다. 저장소를 짧은 경로에 clone하세요. 빌드 폴더 이름이 `build/vk`로 짧은 것도 이 때문입니다.

#### VRAM이 모자랄 때

STT 모델과 번역 모델의 합이 남은 VRAM을 넘으면, 넘친 부분이 일반 메모리로 밀려나 추론이 몇 배 느려집니다. 다른 프로그램(브라우저 등)이 쓰는 VRAM도 함께 계산해야 합니다. 이때는 번역 모델을 GPU에 일부만 올리는 편이 빠릅니다.

```bash
# 번역 모델의 층 중 16개만 GPU에 올림 (-1이면 전부, 0이면 CPU만)
$ ./build/vk/windows/Release/OverlayTransWin.exe --language ja --llm-gpu-layers 16
```

#### 측정 결과 (Ryzen 9 9900X / Radeon RX 6600 8GB, 실제 방송 음성)

발화 구간 하나당 STT 처리 시간 (Vulkan, 언어 지정 시):

| STT 모델 | 처리 시간 | VRAM |
| --- | --- | --- |
| base | 약 0.08초 | 약 0.3GB |
| small | 약 0.22초 | - |
| large-v3-turbo-q5_0 | 약 0.81초 | 약 2.0GB |

STT를 large-v3-turbo로 두고, 다른 프로그램이 VRAM을 약 3.5GB 쓰는 상태에서 잰 번역 시간:

| 번역 모델 | GPU에 올린 층 | 번역 평균 |
| --- | --- | --- |
| Gemma 3 4B | 전부 (VRAM 초과) | 약 0.81초 |
| Gemma 3 4B | 16 | 약 0.51초 |
| Qwen 3.5 4B | 12 | 약 0.74초 |
| Qwen 3.5 2B | 전부 | 약 0.13초 |

### Windows ARM64 빌드 (스냅드래곤 X)

스냅드래곤 X PC에서 직접 빌드합니다. Visual Studio 2022(또는 Build Tools)의 ARM64 C++ 도구, Clang, Python이 필요합니다. (`winget install LLVM.LLVM`) Visual Studio의 컴파일러(cl)로는 빌드되지 않습니다.

`cmake`, `ninja`, `clang`을 PATH에서 찾을 수 있어야 합니다. CMake와 Ninja는 Visual Studio에 들어 있는 것을 써도 됩니다.

```bash
# CPU만 사용
$ cmake --preset windows-arm64
$ cmake --build --preset windows-arm64

# 번역을 Adreno GPU(OpenCL)로 처리. OpenCL 헤더와 로더는 빌드할 때 내려받음
$ cmake --preset windows-arm64-opencl
$ cmake --build --preset windows-arm64-opencl

$ ./build/cl/windows/OverlayTransWin.exe --language ja
```

- OpenCL 빌드에서도 음성 인식은 CPU로 처리합니다. whisper.cpp가 OpenCL 백엔드에 모델을 올리지 못하기 때문입니다.
- 스냅드래곤 X(ARMv8.7) 전용으로 빌드되므로 그보다 오래된 ARM 프로세서에서는 실행되지 않습니다.
- 기본 STT 모델(large-v3-turbo)은 CPU에서 발화 하나에 9초 가까이 걸려 쓸 수 없습니다. `--stt-model`로 small을 지정하세요.

```bash
$ curl -L -o core/models/ggml-small.bin https://huggingface.co/ggerganov/whisper.cpp/resolve/main/ggml-small.bin
$ ./build/arm64/windows/OverlayTransWin.exe --language ja --stt-model core/models/ggml-small.bin
```

#### 측정 결과 (Snapdragon X X1-26-100 8코어 / Adreno X1-45, 전원 연결)

일본어 문장 8개를 이은 98초 분량의 파일을 처리한 결과입니다. 번역 시간은 앞 문장에 합치지 않은 새 문장 기준입니다.

발화 구간 하나당 STT 처리 시간 (CPU):

| STT 모델 | 처리 시간 | 인식 품질 |
| --- | --- | --- |
| base | 약 0.4초 | 낱말을 자주 틀림 (凍結 → 統結, H2A → A12a) |
| small | 약 1.4초 | 대체로 맞음 |
| large-v3-turbo-q5_0 | 약 8.7초 | - |

번역 시간:

| 번역 모델 | 장치 | 번역 평균 | 번역 품질 |
| --- | --- | --- | --- |
| Gemma 3 4B | CPU | 약 1.4초 | 자연스러움 |
| Gemma 3 4B | Adreno GPU | 약 2.5초 | 위와 같음 |
| Qwen 3.5 2B | CPU | 약 0.8초 | 뜻이 자주 틀리고 따옴표와 "라고 말합니다"를 덧붙임 |
| Qwen 3.5 2B | Adreno GPU | 약 1.1초 | 위와 같음 |

- 이 GPU에서는 번역을 GPU에 올려도 CPU보다 느리고, 음성 인식도 0.4초에서 1.1초로 느려집니다. CPU 빌드(`windows-arm64`)에 STT small, 번역 Gemma 3 4B 조합을 권합니다.
- 앞 문장에 합쳐 다시 번역하면 평균 3.5~4초가 걸립니다. 특히 시작한 뒤 첫 문장에 합치는 경우에는 지시문 전체(약 350토큰)를 다시 계산해 4~6초가 걸립니다.
- 스레드는 코어 수에서 두 개를 뺀 만큼 씁니다. 8코어에서 같은 파일을 처리한 시간은 4개 59초, 6개 51초, 8개 105초였습니다.

### 릴리즈 만들기

`.github/workflows/release.yml`이 패키지와 설치 프로그램을 만듭니다. 패키지는 프로세서별 앱을 묶은 zip이고(`x64-vulkan`, `arm64-cpu`), 설치 프로그램은 그중 PC에 맞는 것을 릴리즈에서 내려받아 설치합니다.

1. `CMakeLists.txt`의 `project(... VERSION x.y.z)`를 올리고 `dev`를 `main`에 merge합니다.
2. `main`에 `v<버전>` 태그를 올립니다. (`git tag v1.0.0` → `git push origin v1.0.0`) 태그와 프로젝트 버전이 다르면 빌드가 실패합니다.
3. Actions가 패키지 zip 두 개와 설치 프로그램을 릴리즈 초안에 올립니다. 내용을 확인한 뒤 GitHub에서 공개합니다. 공개하기 전에는 설치 프로그램이 패키지를 내려받지 못합니다.

- 설치 프로그램에는 패키지의 SHA-256이 들어 있어, 릴리즈의 zip을 나중에 바꿔 올리면 설치가 거부됩니다. zip을 고치려면 설치 프로그램도 다시 만들어야 합니다.
- 설치 프로그램이 고르는 규칙은 `installer/OverlayTrans.iss`의 `SelectPackage`에 있습니다.

태그 없이 Actions 탭에서 Release 워크플로를 직접 실행하면, 만들기만 해서 실행 결과(Artifacts)에 올립니다.

로컬에서 만들 때는 빌드한 뒤 아래를 실행합니다. ([Inno Setup 6](https://jrsoftware.org/isinfo.php) 필요)

```bash
# 패키지: build/installer/OverlayTrans-<버전>-<아키텍처>-<백엔드>.zip
$ powershell -ExecutionPolicy Bypass -File installer/package.ps1
$ powershell -ExecutionPolicy Bypass -File installer/package.ps1 -BuildDir build/arm64 -Arch arm64 -Backend cpu

# 설치 프로그램: build/installer/OverlayTrans-Setup-<버전>.exe (위에서 만든 패키지만 지원)
$ powershell -ExecutionPolicy Bypass -File installer/make-installer.ps1

# 릴리즈에 올리기 전에 시험할 때는 내려받는 대신 로컬 패키지를 지정
$ build/installer/OverlayTrans-Setup-1.2.0.exe /PACKAGE=C:\path\OverlayTrans-1.2.0-arm64-cpu.zip
```

## 9. 추후 지원 예정 (현재 범위 제외)

현재는 Windows x64만 대상으로 개발합니다. 아래 항목은 x64 파이프라인을 완성한 뒤 검토합니다. `core/`를 플랫폼 독립으로 유지해, 프리셋·가속 백엔드·플랫폼 쉘만 추가하면 붙일 수 있도록 합니다.

### Windows ARM64 (스냅드래곤X)

- ARM64(MSVC 또는 Clang) CMake 프리셋 추가
- Adreno GPU에서 Vulkan 백엔드 동작 확인 (x64와 동일 코드 경로)
- Qualcomm QNN(Hexagon NPU) 백엔드 빌드 옵션 스파이크(PoC) — ggml QNN 백엔드 성숙도 및 지원 연산자 범위 확인
- 라데온 Vulkan vs 스냅드래곤X Vulkan/QNN 성능·배터리 소모 비교

### Android 포팅

#### 기술 스택

| 분류 | 기술 스택 | 상세 환경 / 주요 라이브러리 |
| --- | --- | --- |
| App Language | Kotlin | v2.0.x 기반 |
| GPU/NPU 가속 | Vulkan / Qualcomm QNN / Android NNAPI | 기본 백엔드는 Vulkan(Windows와 동일 코드 경로), 퀄컴 칩셋은 Hexagon NPU(QNN) 추가 가속, 비퀄컴 기기는 NNAPI로 폴백 |
| Audio Capture | AudioPlaybackCapture | 안드로이드 시스템 사운드 캡처 |
| UI Framework | WindowManager / Jetpack Compose | System Alert Window 기반 자막 오버레이 |
| Build System | Gradle + NDK (CMake) | 안드로이드 NDK 빌드 |

#### 개발 순서

- **Step 1: NDK 기반 JNI 브릿지 설계**
  - Phase 1에서 완성된 C++ 코어(whisper, llama, VAD)를 안드로이드 `src/main/cpp`로 이식
  - Android용 `CMakeLists.txt` 분리 및 `ANDROID` 매크로 기반 플랫폼 분기 처리
  - Kotlin ↔ C++ 함수 매핑을 위한 JNI 인터페이스 설계 (`JNIEXPORT`/`JNICALL` 함수 정의)
  - Native 라이브러리 로드 순서 및 초기화 흐름 정의, JNI 크래시 방지용 예외 처리/Logcat 연동
- **Step 2: 모바일 오디오 캡처 연동**
  - Foreground Service 기반 MediaProjection 권한 요청 플로우 구현
  - `AudioPlaybackCaptureConfiguration` 설정 (Android 10+) 및 `AudioRecord` 캡처 스트림 구성
  - 캡처한 PCM 데이터를 JNI를 통해 C++ Ring Buffer로 전달
  - 백그라운드 서비스 안정성 확보 (배터리 최적화 예외 처리, 서비스 재시작 로직)
- **Step 3: 모바일 최적화 및 하드웨어 가속**
  - whisper.cpp/llama.cpp에 Vulkan 백엔드 연동 (Windows와 동일 코드 경로 재사용)
  - 퀄컴 스냅드래곤 탑재 기기는 QNN(Hexagon NPU) 가속 경로 추가 적용, 비퀄컴 기기는 NNAPI/GPU delegate로 폴백
  - 4비트 양자화(Q4_K_M 등) 모델 적용 및 정확도/속도 트레이드오프 검증
  - Android Profiler를 통한 발열·배터리·메모리 사용량 프로파일링
  - Doze 모드 및 백그라운드 프로세스 우선순위 대응
- **Step 4: 안드로이드 오버레이 UI 구현**
  - `SYSTEM_ALERT_WINDOW` 권한 요청 및 `TYPE_APPLICATION_OVERLAY` 윈도우 생성
  - Jetpack Compose 기반 자막 뷰 구현 및 `WindowManager` 연동
  - 터치 패스스루 처리(제스처 충돌 방지) 및 화면 회전 대응
  - 최종 E2E 테스트: 실제 스트리밍 앱(YouTube/Twitch) 위에서 자막 표시 및 전체 파이프라인 검증

#### 폴더 구조

```
android/                           # [Android] 모바일 포팅 앱
├── app/src/main/cpp/              # core/ 디렉토리 심볼릭 링크 및 JNI 인터페이스
├── app/src/main/java/             # Kotlin 기반 안드로이드 UI 및 서비스
│   ├── service/                   # Foreground Service (화면 캡처 및 백그라운드 유지)
│   └── ui/                        # Jetpack Compose 기반 설정 및 권한 허용 화면
└── build.gradle.kts               # NDK CMake 연동 설정
```

#### 화면 목록

| 화면 이름 | 스크린 ID | 진입 경로 |
| --- | --- | --- |
| 권한 설정 및 대시보드 | MainScreen | 앱 최초 진입 시 |
| 백그라운드 제어 센터 | ControlService | 메인 화면에서 서비스 시작 선택 시 |
| 투명 자막 오버레이 | OverlayWindow | 서비스 구동 후 스트리밍 앱 진입 시 |
| 모델 다운로드 및 설정 | ModelConfigScreen | 메인 화면 우측 상단 톱니바퀴 선택 |

#### 네비게이션 플로우

```
[앱 최초 진입]
       │
       ▼
[MainScreen (권한 체크 및 STT/LLM 모델 검증)]
       │
       ├─► (모델 미설치 시) ─► [ModelConfigScreen] (GGUF 다운로드 관리)
       │
       ▼ (서비스 시작)
[Foreground Service 백그라운드 전환] ──┐
                                       │
       ┌───────────────────────────────┘
       ▼
[스트리밍 앱 (YouTube/Twitch) 실행]
       │
       ▼ (오디오 캡처 & JNI 추론 파이프라인 가동)
[OverlayWindow (화면 최상단 실시간 번역 자막 렌더링)]
```

#### 빌드

```bash
# 안드로이드 스튜디오에서 /android 폴더를 Open 한 후 Gradle Sync 진행
# local.properties 내 NDK 경로 설정 확인 후 빌드
```
