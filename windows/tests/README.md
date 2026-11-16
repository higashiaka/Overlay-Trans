# CLI 번역 검증

실제 계정이나 모델 호출 없이 CLI 프로세스 실행, Windows 인자 인용, UTF-8 입출력, 최근 번역 이력, 오류 처리, 로그인 상태, HTTP 인증과 설정 저장을 검증합니다. 느린 번역 중에도 다음 인식 결과를 받는지, 밀린 CLI 발화를 빠짐없이 순서대로 묶는지, 기기 내 번역은 개별 발화를 유지하는지도 확인합니다.

```powershell
cmake --preset windows-x64 -DOVERLAY_BUILD_CLI_TESTS=ON
cmake --build build/windows-x64 --config Release --target overlay_cli_tests
ctest --test-dir build/windows-x64/windows -C Release --output-on-failure
node --test extension/tests/cli-settings.test.cjs
```

ARM64 프리셋에서는 빌드 폴더를 `build/arm64`로 바꾸고 `--config Release`와 `-C Release`를 생략합니다. Codex의 상주 프로세스·대화 재사용, 스트리밍, 맥락 초기화, 실패 후 재연결도 실제 모델 호출 없이 검증합니다.

로그인한 Codex CLI가 있는 환경에서는 실제 번역도 확인할 수 있습니다. 이 검증은 해당 계정으로 모델을 한 번 호출합니다.

```powershell
build/windows-x64/windows/Release/overlay_cli_tests.exe --smoke-codex
# 모델을 지정해 동일한 세션에서 3번 호출하고 첫 출력·완료 시간을 측정
build/windows-x64/windows/Release/overlay_cli_tests.exe --smoke-codex gpt-6.1-sol 3
```

앱 설정에 저장된 DeepL 키와 Codex 계정으로 동일한 일본어 문장 5개를 비교할 수 있습니다. DeepL과 Codex를 실제로 호출하며, 번역 결과·초안·첫 표시 시간·최종 완료 시간을 `build/cli-comparison/deepl-codex.json`에 기록합니다. 키는 보고서에 기록하지 않습니다.

```powershell
build/windows-x64/windows/Release/overlay_cli_tests.exe --compare-deepl-codex gpt-6.1-sol
```
