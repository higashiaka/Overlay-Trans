# OverLay-Trans (가칭)

해외 스트리머 방송의 언어 장벽을 무너뜨리는 **온디바이스 실시간 AI 자막/번역 애플리케이션**, OverLay-Trans의 저장소입니다.

## 목차

1. [프로젝트 소개](#1-프로젝트-소개)
2. [기술 스택](#2-기술-스택)
3. [단계별 개발 순서 (로드맵)](#3-️-단계별-개발-순서-로드맵)
4. [프로젝트 폴더 구조](#4-프로젝트-폴더-구조)
5. [화면 목록 & 플로우 정리](#5--화면-목록--플로우-정리)
6. [개발 컨벤션](#6--개발-컨벤션)
7. [Git 브랜치 및 커밋 전략](#7-git-브랜치-및-커밋-전략)
8. [PR 및 코드 리뷰 규칙 (Pn 룰)](#8-pr-및-코드-리뷰-규칙-pn-룰)
9. [빌드 및 실행 방식](#9-빌드-및-실행-방식)

---

## 1. 프로젝트 소개

- **앱 이름**: OverLay-Trans
- **한 줄 설명**: 스트리머 방송의 시스템 오디오를 캡처하여 실시간 온디바이스 STT 및 경량 LLM 번역을 거쳐 화면 최상단에 자막을 띄우는 오버레이 번역 서비스입니다.
- **서비스 목표**: 서버 통신 비용 없이 C/C++ 기반 100% 온디바이스 구동을 목표로 하며, 인터넷 방송 특유의 신조어와 화자 분리(Diarization) 맥락을 반영하여 시청자에게 완벽한 몰입감을 제공합니다.
- **개발 형태**: 1인 개발 (기획부터 C++ 코어, 안드로이드 포팅까지 전 영역 단독 진행)

## 2. 기술 스택

온디바이스 최적화 및 윈도우-안드로이드 크로스 플랫폼 포팅을 고려한 기술 스택입니다.

| 분류 | 기술 스택 | 상세 환경 / 주요 라이브러리 |
| --- | --- | --- |
| Core Language | C++20 | 핵심 비즈니스 로직 및 AI 추론 엔진 구동 |
| App Language | Kotlin | v2.0.x 기반 (Android 포팅용) |
| STT Engine | whisper.cpp | 온디바이스 음성 인식 (GPU/NPU 가속은 아래 가속기 항목 참조) |
| LLM Engine | llama.cpp | GGUF 경량 모델 기반 문맥 번역 및 화자 분리 텍스트 가공 |
| GPU/NPU 가속 | Vulkan (`ggml-vulkan`) / Qualcomm QNN / Android NNAPI | Radeon(x86_64)·Adreno(ARM64, Windows·Android 공통) 기본 백엔드는 Vulkan, 퀄컴 칩셋(스냅드래곤X·스냅드래곤 모바일)은 Hexagon NPU(QNN) 추가 가속, 비퀄컴 안드로이드 기기는 NNAPI로 폴백 |
| Audio Capture | miniaudio (WASAPI) / AudioPlaybackCapture | Windows 루프백 스테레오 캡처 / 안드로이드 시스템 사운드 캡처 |
| UI Framework | Dear ImGui / WindowManager | Windows 투명 오버레이 / Android System Alert Window |
| Target Architecture | x86_64 + ARM64 (Windows), ARM64 (Android) | 라데온 x64 데스크톱과 스냅드래곤X ARM64 노트북 동시 지원 |
| Build System | CMake (x64 / ARM64 멀티 툴체인) / Gradle | C++ 라이브러리 관리 및 안드로이드 NDK 빌드 |

## 3. 🗺️ 단계별 개발 순서 (로드맵)

프로젝트는 코어 엔진을 검증하는 **Phase 1 (Windows)**과 실제 모바일로 이식하는 **Phase 2 (Android)**로 나누어 진행됩니다.

### Phase 1: Windows Core Development (구현 및 검증)

> 개발/검증은 x64 라데온 데스크톱과 ARM64 스냅드래곤X 노트북 두 환경에서 병행합니다.

- **Step 0: 멀티 아키텍처 빌드 환경 구성**
  - x64(MSVC) / ARM64(MSVC 또는 Clang) 듀얼 툴체인용 CMake 프리셋(`CMakePresets.json`) 작성
  - ggml Vulkan 백엔드(`ggml-vulkan`) 공통 빌드 옵션 구성 — 라데온(x64)·Adreno(ARM64) 양쪽에서 동일 경로로 동작 확인
  - 스냅드래곤X 전용 Qualcomm QNN(Hexagon NPU) 백엔드 빌드 옵션 스파이크(PoC) — ggml QNN 백엔드 성숙도 및 지원 연산자 범위 확인
  - CI 없이 로컬에서 두 아키텍처 빌드를 모두 검증하는 체크리스트 작성 (1인 개발 환경 고려)
- **Step 1: 오디오 루프백 캡처**
  - `IMMDeviceEnumerator` / `IAudioClient`를 공유 모드(Shared Mode) + 루프백(Loopback) 플래그로 초기화
  - `miniaudio.h` 디바이스 콜백에서 PCM 프레임 수신 및 포맷 변환 (float32 ↔ int16)
  - 화자 분리를 위한 스테레오(L/R) 채널 분리 및 독립 버퍼 관리
  - Lock-free 고리형 버퍼(Ring Buffer) 구현 및 오버플로우 정책 정의
  - 디버깅용 raw PCM/WAV 덤프 로깅 기능 추가
  - 검증: 캡처 지연 시간 및 샘플 드랍(drop) 유무 측정
- **Step 2: VAD 및 슬라이딩 윈도우 구현**
  - VAD 알고리즘 선정 및 이식 (WebRTC VAD 또는 Silero VAD 경량화 버전)
  - 슬라이딩 윈도우 크기/오버랩 정의 (예: 30ms frame, 10ms hop)
  - 발화 시작(Speech Onset)/종료(Speech Offset) 감지 및 무음 구간 트리밍
  - 게임 효과음/BGM 오탐 방지를 위한 임계값(threshold) 튜닝
  - 검출된 발화 세그먼트를 STT 큐로 전달하는 파이프라인 연결
- **Step 3: whisper.cpp 통합 (STT)**
  - whisper.cpp 서브모듈 연동 및 CMake 빌드 설정 (Vulkan 백엔드 활성화, 스냅드래곤X는 QNN 백엔드 옵션 병행)
  - base 다국어 GGML 모델 다운로드 및 로딩 루틴 구현
  - 청크 단위 스트리밍 추론 구조 설계 (이전 컨텍스트 유지, 문장 경계 처리)
  - 언어 자동 감지(auto-detect) 및 언어 강제 지정 옵션 제공
  - 벤치마크: RTF(Real-Time Factor) 측정 및 두 기기 간(라데온 Vulkan vs 스냅드래곤X Vulkan/QNN) 성능·배터리 소모 비교, 한/일/영 인식 정확도 테스트
- **Step 4: llama.cpp 연동 (문맥 번역)**
  - llama.cpp 서브모듈 연동 및 번역용 경량 GGUF 모델 선정/양자화
  - 화자 정보(L/R 채널)와 이전 대화 맥락 N턴을 포함한 System Prompt 설계
  - 인터넷 방송 신조어·은어 처리를 위한 프롬프트 가이드 및 사전(glossary) 구성
  - Token-by-Token 스트리밍 추론 구현 (KV 캐시 재사용으로 지연 시간 최소화)
  - STT → 번역 파이프라인 연결 및 번역 품질 회귀 테스트 케이스 작성
- **Step 5: 투명 오버레이 UI 개발**
  - `WS_EX_LAYERED | WS_EX_TRANSPARENT | WS_EX_TOPMOST` 속성의 클릭 관통형 오버레이 창 생성
  - Dear ImGui + DirectX/OpenGL 렌더링 백엔드 연동
  - 자막 텍스트 렌더링(폰트, 스타일, 페이드 인/아웃 애니메이션) 구현
  - 자막 위치/크기/투명도 조절이 가능한 사용자 설정 UI 추가
  - E2E 통합 테스트: 캡처 → VAD → STT → LLM → 오버레이 전체 파이프라인 지연 시간(목표 500ms 이하) 측정

### Phase 2: Android Porting & Optimization (안드로이드 이식)

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

## 4. 프로젝트 폴더 구조

Windows C++ 개발과 향후 안드로이드 통합(JNI)을 고려한 Core-driven 패키지 구조로 구성합니다.

```
.
├── core/                          # [C++] 플랫폼 독립적인 핵심 엔진 (Phase 1 & 2 공통)
│   ├── audio/                     # 오디오 버퍼링 및 VAD 알고리즘 구현체
│   ├── inference/                 # whisper.cpp 및 llama.cpp 래핑 클래스
│   └── models/                    # GGUF 양자화 모델 파일 보관 (gitignore)
├── windows/                       # [Windows] Phase 1 개발용 데스크톱 쉘
│   ├── capture/                   # WASAPI 루프백 캡처 (miniaudio)
│   ├── ui/                        # Dear ImGui 기반 투명 오버레이 렌더링
│   └── main.cpp                   # 윈도우 실행 진입점
└── android/                       # [Android] Phase 2 모바일 포팅 앱
    ├── app/src/main/cpp/          # core/ 디렉토리 심볼릭 링크 및 JNI 인터페이스
    ├── app/src/main/java/         # Kotlin 기반 안드로이드 UI 및 서비스
    │   ├── service/               # Foreground Service (화면 캡처 및 백그라운드 유지)
    │   └── ui/                    # Jetpack Compose 기반 설정 및 권한 허용 화면
    └── build.gradle.kts           # NDK CMake 연동 설정
```

## 5. 💻 화면 목록 & 플로우 정리

### 화면 목록

| 화면 이름 | 스크린 ID | 진입 경로 |
| --- | --- | --- |
| 권한 설정 및 대시보드 | MainScreen | 앱 최초 진입 시 |
| 백그라운드 제어 센터 | ControlService | 메인 화면에서 서비스 시작 선택 시 |
| 투명 자막 오버레이 | OverlayWindow | 서비스 구동 후 스트리밍 앱 진입 시 |
| 모델 다운로드 및 설정 | ModelConfigScreen | 메인 화면 우측 상단 톱니바퀴 선택 |

### 네비게이션 플로우

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

## 6. 🧑‍💻 개발 컨벤션

### 네이밍 규칙

C++ 및 Kotlin 공식 스타일 가이드를 플랫폼별로 준수합니다.

| 대상 | 규칙 | 예시 |
| --- | --- | --- |
| 클래스/구조체 (C++) | PascalCase | `AudioCapture`, `InferenceEngine` |
| 일반 함수 / 변수 (C++) | snake_case | `start_audio_stream()`, `buffer_size` |
| 상수 / 매크로 (C++) | UPPER_SNAKE_CASE | `MAX_TOKEN_LENGTH` |
| 클래스/인터페이스 (Kotlin) | PascalCase | `OverlayService`, `JniBridge` |
| 일반 함수 / 변수 (Kotlin) | camelCase | `startOverlay()`, `audioBuffer` |

## 7. Git 브랜치 및 커밋 전략

### 브랜치 전략 (Git Flow 기반)

- `main`: 안정적인 빌드가 가능한 릴리즈 브랜치
- `develop`: 메인 개발 통합 브랜치
- `feature/[기능명]`: 단위 기능 개발 브랜치 (예: `feature/wasapi-loopback`, `feature/jni-bridge`)

### 커밋 Type

| Type | 사용 상황 | 예시 |
| --- | --- | --- |
| feat | 새로운 기능 추가 | `feat/1-miniaudio-capture` |
| fix | 버그 수정 | `fix/5-memory-leak-whisper` |
| refactor | 기능 변화 없는 코드 개선 | `refactor/12-ring-buffer` |
| chore | 설정, 빌드, 패키지, 환경 작업 | `chore/3-cmake-setup` |
| docs | 문서 수정 | `docs/readme-update` |

### 커밋 컨벤션

```
[Type]: [Title]

[Body - Optional]
```

예시:

```
feat: WASAPI 기반 시스템 사운드 루프백 캡처 추가
chore: whisper.cpp 서브모듈 연결 및 CMake 리스트 업데이트
fix: 안드로이드 권한 거부 시 앱 크래시 문제 수정
```

## 8. PR 및 코드 리뷰 규칙 (Pn 룰)

- **PR 조건**: 로컬 CMake/Gradle 빌드 검증 완료 후 개설
- **코드 리뷰 Pn 규칙** (1인 개발 환경이므로 스스로 체크리스트 목적으로 사용)
  - `P1`: 반드시 해결해야 하는 치명적 이슈 (메모리 릭, JNI 크래시 등)
  - `P2`: 최적화 및 리팩토링 고려 대상 (Latency 개선 등)
  - `P3`: 나중에 처리해도 되는 UI 디테일

### PR 작성 템플릿

```markdown
type: 작업 내용 (예: feat: 안드로이드 MediaProjection 권한 로직 구현)

## 개요
<!-- 이번 PR에서 어떤 작업을 했는지 간단히 설명해주세요. -->

## 작업 내용 (커밋 로그 기반)
-
-

## 테스트 방법
- [ ] 데스크톱/모바일 환경 빌드 통과
- [ ] 오디오 캡처 시 레이턴시 500ms 미만 유지 여부
- [ ] 메모리 누수 발생 여부 확인 (작업 관리자 / Android Profiler)

## 관련 이슈
close #
```

## 9. 빌드 및 실행 방식

```bash
# 1. 저장소 및 서브모듈 클론
$ git clone --recursive https://github.com/[Your-Repository]/overlay-trans.git

# 2. Windows 빌드 (CMake)
$ cd overlay-trans/windows
$ mkdir build && cd build
$ cmake ..
$ cmake --build . --config Release

# 3. Android 빌드 (Android Studio)
# 안드로이드 스튜디오에서 /android 폴더를 Open 한 후 Gradle Sync 진행
# local.properties 내 NDK 경로 설정 확인 후 빌드
```
