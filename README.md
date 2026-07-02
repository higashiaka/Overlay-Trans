OverLay-Trans (가칭)해외 스트리머 방송의 언어 장벽을 무너뜨리는 온디바이스 실시간 AI 자막/번역 애플리케이션 OverLay-Trans의 저장소입니다.1. 프로젝트 소개앱 이름: OverLay-Trans한 줄 설명: 스트리머 방송의 시스템 오디오를 캡처하여 실시간 온디바이스 STT 및 경량 LLM 번역을 거쳐 화면 최상단에 자막을 띄우는 오버레이 번역 서비스입니다.서비스 목표: 서버 통신 비용 없이 C/C++ 기반 100% 온디바이스 구동을 목표로 하며, 인터넷 방송 특유의 신조어와 화자 분리(Diarization) 맥락을 반영하여 시청자에게 완벽한 몰입감을 제공합니다.2. 팀원 소개 및 역할 분담닉네임/이름파트역할 분담개발자/김동혁C++ & Android (1인)Windows 루프백 캡처 및 C++ 코어(STT, LLM) 엔진 통합안드로이드 NDK 포팅 및 오버레이 UI 구현3. 기술 스택온디바이스 최적화 및 윈도우-안드로이드 크로스 플랫폼 포팅을 고려한 기술 스택입니다.분류기술 스택상세 환경 / 주요 라이브러리Core LanguageC++20핵심 비즈니스 로직 및 AI 추론 엔진 구동App LanguageKotlinv2.0.x 기반 (Android 포팅용)STT Enginewhisper.cpp온디바이스 음성 인식 (CUDA / 안드로이드 NNAPI 가속)LLM Enginellama.cppGGUF 경량 모델 기반 문맥 번역 및 화자 분리 텍스트 가공Audio Captureminiaudio (WASAPI) / AudioPlaybackCaptureWindows 루프백 스테레오 캡처 / 안드로이드 시스템 사운드 캡처UI FrameworkDear ImGui / WindowManagerWindows 투명 오버레이 / Android System Alert WindowBuild SystemCMake / GradleC++ 라이브러리 관리 및 안드로이드 NDK 빌드4. 🗺️ 단계별 개발 순서 (로드맵)프로젝트는 코어 엔진을 검증하는 Phase 1 (Windows)과 실제 모바일로 이식하는 Phase 2 (Android)로 나누어 진행됩니다.Phase 1: Windows Core Development (구현 및 검증)Step 1: 오디오 루프백 캡처miniaudio.h를 이용한 WASAPI 기반 시스템 사운드 가로채기화자 분리를 위한 스테레오(L/R) 채널 분리 데이터 로깅 및 고리형 버퍼(Ring Buffer) 구현Step 2: VAD 및 슬라이딩 윈도우 구현초경량 C++ VAD 엔진을 적용해 사람 목소리가 있는 구간만 버퍼링 (게임 소리 필터링)Step 3: whisper.cpp 통합 (STT)base 다국어 모델 적용 및 실시간 원문(한/일/영) 추출 벤치마크 테스트Step 4: llama.cpp 연동 (문맥 번역)화자 정보와 이전 대화 맥락을 포함한 System Prompt 설계지연 시간 최소화를 위한 Token-by-Token 스트리밍 추론 구현Step 5: 투명 오버레이 UI 개발Windows API를 활용한 마우스 클릭 관통형 자막 오버레이 창 구현 (Dear ImGui 활용)Phase 2: Android Porting & Optimization (안드로이드 이식)Step 1: NDK 기반 JNI 브릿지 설계Phase 1에서 완성된 C++ 코어(whisper, llama, VAD)를 안드로이드 src/main/cpp로 이식Step 2: 모바일 오디오 캡처 연동MediaProjection 권한 획득 및 AudioPlaybackCapture 스트림을 C++ 버퍼로 연결Step 3: 모바일 최적화 및 하드웨어 가속안드로이드 NNAPI 및 4비트 양자화 모델을 통한 배터리 소모 및 발열 제어Step 4: 안드로이드 오버레이 UI 구현System Alert Window를 통한 스트리밍 앱 최상단 자막 표시5. 프로젝트 폴더 구조Windows C++ 개발과 향후 안드로이드 통합(JNI)을 고려한 Core-driven 패키지 구조로 구성합니다..
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
6. 💻 화면 목록 & 플로우 정리화면 목록화면 이름스크린 ID진입 경로담당자권한 설정 및 대시보드MainScreen앱 최초 진입 시동혁백그라운드 제어 센터ControlService메인 화면에서 서비스 시작 선택 시동혁투명 자막 오버레이OverlayWindow서비스 구동 후 스트리밍 앱 진입 시동혁모델 다운로드 및 설정ModelConfigScreen메인 화면 우측 상단 톱니바퀴 선택동혁네비게이션 플로우[앱 최초 진입]
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
7. 🧑‍💻 개발 컨벤션네이밍 규칙C++ 및 Kotlin 공식 스타일 가이드를 플랫폼별로 준수합니다.대상규칙예시클래스/구조체 (C++)PascalCaseAudioCapture, InferenceEngine일반 함수 / 변수 (C++)snake_casestart_audio_stream(), buffer_size상수 / 매크로 (C++)UPPER_SNAKE_CASEMAX_TOKEN_LENGTH클래스/인터페이스 (Kotlin)PascalCaseOverlayService, JniBridge일반 함수 / 변수 (Kotlin)camelCasestartOverlay(), audioBuffer8. Git 브랜치 및 커밋 전략브랜치 전략 (Git Flow 기반)main: 안정적인 빌드가 가능한 릴리즈 브랜치develop: 메인 개발 통합 브랜치feature/[기능명]: 단위 기능 개발 브랜치 (예: feature/wasapi-loopback, feature/jni-bridge)Type사용 상황예시feat새로운 기능 추가feat/1-miniaudio-capturefix버그 수정fix/5-memory-leak-whisperrefactor기능 변화 없는 코드 개선refactor/12-ring-bufferchore설정, 빌드, 패키지, 환경 작업chore/3-cmake-setupdocs문서 수정docs/readme-update커밋 컨벤션[Type]: [Title]

[Body - Optional]
예시feat: WASAPI 기반 시스템 사운드 루프백 캡처 추가
chore: whisper.cpp 서브모듈 연결 및 CMake 리스트 업데이트
fix: 안드로이드 권한 거부 시 앱 크래시 문제 수정
9. PR 및 코드 리뷰 규칙 (Pn 룰)PR 조건: 로컬 CMake/Gradle 빌드 검증 완료 후 개설코드 리뷰 Pn 규칙: (1인 개발 환경이므로 스스로 체크리스트 목적으로 사용)P1: 반드시 해결해야 하는 치명적 이슈 (메모리 릭, JNI 크래시 등)P2: 최적화 및 리팩토링 고려 대상 (Latency 개선 등)P3: 나중에 처리해도 되는 UI 디테일PR 작성 룰Markdowntype: 작업 내용 (예: feat: 안드로이드 MediaProjection 권한 로직 구현)

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
10. 빌드 및 실행 방식Bash# 1. 저장소 및 서브모듈 클론
$ git clone --recursive https://github.com/[Your-Repository]/overlay-trans.git

# 2. Windows 빌드 (CMake)
$ cd overlay-trans/windows
$ mkdir build && cd build
$ cmake ..
$ cmake --build . --config Release

# 3. Android 빌드 (Android Studio)
# 안드로이드 스튜디오에서 /android 폴더를 Open 한 후 Gradle Sync 진행
# local.properties 내 NDK 경로 설정 확인 후 빌드
