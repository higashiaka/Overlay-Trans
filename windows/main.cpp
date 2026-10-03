#include "audio/ring_buffer.h"
#include "audio/vad_segmenter.h"
#include "audio/wav_writer.h"
#include "capture/audio_file_reader.h"
#include "capture/loopback_capture.h"
#include "inference/recognition_worker.h"
#include "inference/speaker_identifier.h"
#include "inference/speech_recognizer.h"
#include "inference/translator.h"
#include "server/pairing_token.h"
#include "server/subtitle_server.h"
#include "text/script.h"
#include "tray/tray_icon.h"

#define NOMINMAX
#include <windows.h>

#include <shlobj.h>

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cwchar>
#include <deque>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <mutex>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <thread>
#include <utility>
#include <vector>

namespace {

using namespace overlay_trans;
using Clock = std::chrono::steady_clock;

constexpr const char* DEFAULT_VAD_MODEL = "ggml-silero-v6.2.0.bin";
constexpr const char* DEFAULT_STT_MODEL = "ggml-large-v3-turbo-q5_0.bin";
constexpr const char* DEFAULT_LLM_MODEL = "gemma-3-4b-it-Q4_K_M.gguf";
constexpr const char* DEFAULT_SPEAKER_MODEL = "3dspeaker_speech_campplus_sv_zh_en_16k-common_advanced.onnx";

struct Options {
    // 지정하지 않으면 모델 폴더(default_models_folder)의 기본 모델을 쓴다.
    std::filesystem::path vad_model;
    std::filesystem::path stt_model;
    std::filesystem::path llm_model;
    std::filesystem::path speaker_model;
    std::filesystem::path input_file;  // 비어 있으면 시스템 오디오를 캡처한다.
    std::filesystem::path dump_wav;    // 비어 있으면 덤프하지 않는다.
    std::filesystem::path glossary;    // 번역 용어집 파일. 비어 있으면 사용하지 않는다.
    std::string language = "auto";
    std::string stt_hint;  // STT에 미리 알려 줄 이름과 용어
    bool chat_context = false;  // 확장 프로그램이 보낸 채팅을 번역 맥락으로 쓸지
    bool early_stt = true;  // 발화가 끝났다고 확정되기 전에 인식을 미리 시작할지
    bool merge_sentences = true;  // 끊어서 내보낸 말이 이어지면 합쳐서 다시 번역할지
    bool speakers = true;  // 발화마다 화자를 구분해, 다른 사람의 말은 앞 문장에 합치지 않을지
    bool background = false;  // 시작한 뒤 콘솔 창을 숨기고 알림 영역 아이콘으로만 둘지
    bool stream_subtitles = true;  // 번역문이 다 만들어지기 전에 만들어진 부분부터 자막으로 보낼지
    bool use_gpu = true;  // Vulkan 프리셋으로 빌드한 경우에만 효과가 있다.
    int gpu_device = 0;
    int port = 47815;  // 확장 프로그램과 통신하는 로컬 포트
    int llm_gpu_layers = -1;  // 번역 모델에서 GPU에 올릴 층 수. -1이면 전부.
    // STT가 한 번에 계산하는 길이(한 칸에 20ms). 발화는 길어야 6초 남짓이므로 모델 기본값(1500칸=30초)의
    // 절반으로 줄여 인식 시간을 1/3로 줄인다. 0이면 모델 기본값을 쓴다.
    int stt_audio_context = 768;
};

// 콘솔 출력 코드 페이지(UTF-8)에 맞춰 경로를 문자열로 바꾼다.
std::string to_utf8(const std::filesystem::path& path) {
    const std::u8string text = path.u8string();
    return std::string(text.begin(), text.end());
}

// 기본 모델을 찾을 폴더. 설치된 앱은 실행 파일 옆의 models 폴더에 모델을 둔다.
// 그 폴더가 없으면 저장소 루트에서 실행한 것으로 보고 core/models를 쓴다.
std::filesystem::path default_models_folder() {
    wchar_t exe_path[MAX_PATH];
    const DWORD length = GetModuleFileNameW(nullptr, exe_path, MAX_PATH);
    if (length > 0 && length < MAX_PATH) {
        const std::filesystem::path installed = std::filesystem::path(exe_path).parent_path() / "models";
        std::error_code error;
        if (std::filesystem::is_directory(installed, error)) {
            return installed;
        }
    }
    return "core/models";
}

Options parse_options(int argc, wchar_t** argv) {
    Options options;
    for (int i = 1; i + 1 < argc; i += 2) {
        const std::wstring_view name = argv[i];
        const std::wstring_view value = argv[i + 1];
        if (name == L"--vad-model") {
            options.vad_model = value;
        } else if (name == L"--stt-model") {
            options.stt_model = value;
        } else if (name == L"--llm-model") {
            options.llm_model = value;
        } else if (name == L"--input") {
            options.input_file = value;
        } else if (name == L"--dump-wav") {
            options.dump_wav = value;
        } else if (name == L"--glossary") {
            options.glossary = value;
        } else if (name == L"--language") {
            options.language = to_utf8(value);
        } else if (name == L"--chat-context") {
            options.chat_context = value == L"on";
        } else if (name == L"--speakers") {
            options.speakers = value == L"on";
        } else if (name == L"--speaker-model") {
            options.speaker_model = value;
        } else if (name == L"--background") {
            options.background = value == L"on";
        } else if (name == L"--stream-subtitles") {
            options.stream_subtitles = value != L"off";
        } else if (name == L"--merge-sentences") {
            options.merge_sentences = value != L"off";
        } else if (name == L"--early-stt") {
            options.early_stt = value != L"off";
        } else if (name == L"--stt-hint") {
            options.stt_hint = to_utf8(value);
        } else if (name == L"--device") {
            options.use_gpu = value != L"cpu";
        } else if (name == L"--port") {
            options.port = static_cast<int>(std::wcstol(argv[i + 1], nullptr, 10));
        } else if (name == L"--gpu-device") {
            options.gpu_device = static_cast<int>(std::wcstol(argv[i + 1], nullptr, 10));
        } else if (name == L"--stt-audio-ctx") {
            options.stt_audio_context = static_cast<int>(std::wcstol(argv[i + 1], nullptr, 10));
        } else if (name == L"--llm-gpu-layers") {
            options.llm_gpu_layers = static_cast<int>(std::wcstol(argv[i + 1], nullptr, 10));
        }
    }

    const std::filesystem::path models_folder = default_models_folder();
    if (options.vad_model.empty()) {
        options.vad_model = models_folder / DEFAULT_VAD_MODEL;
    }
    if (options.stt_model.empty()) {
        options.stt_model = models_folder / DEFAULT_STT_MODEL;
    }
    if (options.llm_model.empty()) {
        options.llm_model = models_folder / DEFAULT_LLM_MODEL;
    }
    if (options.speaker_model.empty()) {
        options.speaker_model = models_folder / DEFAULT_SPEAKER_MODEL;
    }
    return options;
}

// 페어링 키를 저장하는 파일. 사용자별 로컬 앱 데이터 폴더 아래에 둔다.
std::filesystem::path pairing_token_path() {
    std::filesystem::path folder = ".";
    PWSTR local_app_data = nullptr;
    if (SUCCEEDED(SHGetKnownFolderPath(FOLDERID_LocalAppData, 0, nullptr, &local_app_data))) {
        folder = local_app_data;
    }
    CoTaskMemFree(local_app_data);
    return folder / "OverlayTrans" / "pairing-token.txt";
}

// UTF-8 텍스트 파일 전체를 읽는다. 열 수 없으면 false를 반환한다.
bool read_text_file(const std::filesystem::path& path, std::string& text) {
    std::ifstream file(path, std::ios::binary);
    if (!file) {
        return false;
    }
    text.assign(std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>());
    return true;
}

// 번역 지시문에 넣을 방송 설명을 만든다.
std::string build_stream_info(const StreamContext& context) {
    std::string info;
    if (!context.channel.empty()) {
        info += "Channel: " + context.channel + ". ";
    }
    if (!context.category.empty()) {
        info += "Game or category: " + context.category + ". ";
    }
    if (!context.title.empty()) {
        info += "Title: " + context.title + ".";
    }
    return info;
}

// 확장 프로그램에서 받은 채팅 한 줄("이름: 내용")과 받은 시각.
struct ReceivedChat {
    Clock::time_point received_at;
    std::string line;
};

constexpr size_t MAX_PENDING_CHAT = 30;
// 발화 하나에 맥락으로 붙이는 채팅 수. 많이 넣을수록 번역이 느려진다.
constexpr size_t MAX_CHAT_LINES = 3;
// 채팅 한 줄에서 맥락으로 쓰는 최대 길이(바이트). 긴 채팅은 앞부분만 쓴다.
constexpr size_t MAX_CHAT_LINE_BYTES = 120;
// 이보다 오래된 채팅은 지금 하는 말과 관련이 없다고 본다.
constexpr auto MAX_CHAT_AGE = std::chrono::seconds(30);

// 긴 말은 자막이 늦지 않게 여러 구간으로 끊어서 내보낸다. 뒤 구간이 앞 구간에서 이어진 말이면,
// 앞 구간과 합친 문장을 다시 번역해 화면의 자막을 바꾼다. 이렇게 합치고 있는 문장을 담는다.
struct OpenSentence {
    std::string source;             // 지금까지 합친 원문
    std::vector<std::string> chat;  // 번역 맥락으로 쓴 채팅
    size_t fragment_count = 0;      // 합친 구간 수. 0이면 합치고 있는 문장이 없다.
    int64_t subtitle = 0;           // 화면에 내보낸 자막의 번호
    std::string translation;        // 화면에 내보낸 번역문
    int speaker = SpeakerIdentifier::UNKNOWN;  // 이 문장을 말한 화자. 확실히 구분된 구간이 없으면 UNKNOWN.
};

// 화자 구분을 믿을 수 있는 최소 구간 길이(초). 이보다 짧은 구간은 같은 사람이어도 다른 화자로 잡히는 일이 잦아서,
// 다른 화자라도 문장을 나누는 데 쓰지 않는다.
constexpr double MIN_SPEAKER_SPLIT_SECONDS = 1.5;

// 앞 구간과의 사이가 이보다 짧게 비었으면 이어진 말로 본다. 말하다 잠깐 쉬는 것까지 잡도록
// 발화를 끊는 기준(min_silence_ms)보다 길게 둔다. 서로 다른 짧은 문장이 합쳐지는 일도 있지만 번역에는 지장이 없다.
constexpr uint32_t MAX_SENTENCE_GAP_MS = 1000;
// 한 문장으로 합치는 최대 구간 수와 원문 길이(바이트). 넘으면 새 문장으로 시작한다.
// 길게 합칠수록 다시 번역하는 시간이 늘어난다.
constexpr size_t MAX_SENTENCE_FRAGMENTS = 3;
constexpr size_t MAX_SENTENCE_BYTES = 180;

double to_seconds(Clock::duration duration) {
    return std::chrono::duration<double>(duration).count();
}

// 파일을 처음부터 끝까지 가능한 한 빠르게 처리한다.
int run_file(const Options& options, const CaptureConfig& config, VadSegmenter& vad, RecognitionWorker& worker) {
    AudioFileReader reader;
    if (!reader.open(options.input_file, config.sample_rate, config.channels)) {
        std::fprintf(stderr, "Failed to open audio file: %s (%s)\n", to_utf8(options.input_file).c_str(),
                     reader.last_error().c_str());
        return 1;
    }

    const size_t chunk_frames = config.sample_rate / 10;
    std::vector<float> chunk(chunk_frames * config.channels);
    uint64_t total_frames = 0;
    const auto started_at = Clock::now();

    while (true) {
        const size_t frame_count = reader.read(chunk.data(), chunk_frames);
        if (frame_count == 0) {
            break;
        }
        total_frames += frame_count;
        vad.process(chunk.data(), frame_count * config.channels);
    }
    vad.flush();
    worker.wait_until_idle();

    std::printf("Processed %.1f s of audio in %.1f s.\n",
                static_cast<double>(total_frames) / static_cast<double>(config.sample_rate),
                to_seconds(Clock::now() - started_at));
    return 0;
}

// 앱을 끝내라는 신호. 콘솔에서 Enter나 Ctrl+C를 누르거나, 알림 영역 메뉴에서 종료를 고르면 켜진다.
HANDLE quit_event() {
    static const HANDLE event = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    return event;
}

BOOL WINAPI on_console_event(DWORD event) {
    SetEvent(quit_event());
    // 콘솔 창을 닫으면 이 함수가 끝나는 대로 프로세스가 강제로 끝난다. 정리(알림 영역 아이콘 제거)할 시간을 준다.
    if (event == CTRL_CLOSE_EVENT || event == CTRL_LOGOFF_EVENT || event == CTRL_SHUTDOWN_EVENT) {
        Sleep(3000);
    }
    return TRUE;
}

// 시스템 오디오를 캡처해 종료 신호(quit_event)가 올 때까지 처리한다.
int run_live(const Options& options, const CaptureConfig& config, const VadConfig& vad_config, VadSegmenter& vad,
             RecognitionWorker& worker, const std::string& pairing_code) {
    const size_t samples_per_second = static_cast<size_t>(config.sample_rate) * config.channels;

    WavWriter wav_dump;
    if (!options.dump_wav.empty() &&
        !wav_dump.open(options.dump_wav, config.sample_rate, static_cast<uint16_t>(config.channels))) {
        std::fprintf(stderr, "Failed to open WAV file: %s\n", to_utf8(options.dump_wav).c_str());
        return 1;
    }

    // 소비 스레드가 잠시 멈춰도 버틸 수 있도록 10초 분량을 확보한다.
    RingBuffer ring(samples_per_second * 10);

    LoopbackCapture capture;
    const bool started = capture.start(config, [&](const float* frames, uint32_t frame_count) {
        ring.write(frames, static_cast<size_t>(frame_count) * config.channels);
    });
    if (!started) {
        std::fprintf(stderr, "Failed to start loopback capture: %s\n", capture.last_error().c_str());
        return 1;
    }

    std::jthread consumer([&](std::stop_token stop) {
        constexpr auto IDLE_SLEEP = std::chrono::milliseconds(10);
        std::vector<float> chunk(samples_per_second / 10);
        auto idle_time = std::chrono::milliseconds(0);

        while (true) {
            const size_t read_count = ring.read(chunk.data(), chunk.size());
            if (read_count == 0) {
                // 종료 요청을 받아도 버퍼에 남은 샘플은 모두 처리한 뒤 끝낸다.
                if (stop.stop_requested()) {
                    break;
                }

                // 재생 중인 소리가 없으면 루프백 입력이 끊기므로, 진행 중인 발화를 여기서 마무리한다.
                std::this_thread::sleep_for(IDLE_SLEEP);
                idle_time += IDLE_SLEEP;
                if (idle_time >= std::chrono::milliseconds(vad_config.min_silence_ms)) {
                    vad.flush();
                    idle_time = std::chrono::milliseconds(0);
                }
                continue;
            }

            idle_time = std::chrono::milliseconds(0);
            if (wav_dump.is_open()) {
                wav_dump.write(chunk.data(), read_count);
            }
            vad.process(chunk.data(), read_count);
        }

        vad.flush();
    });

    TrayIcon tray;
    const bool tray_ready = tray.start({
        .tooltip = L"OverLay-Trans " OVERLAY_TRANS_VERSION_W,
        .pairing_code = std::wstring(pairing_code.begin(), pairing_code.end()),
        .on_quit = [] { SetEvent(quit_event()); },
    });
    if (!tray_ready) {
        std::fprintf(stderr, "Failed to create the tray icon. Press Enter in this window to stop.\n");
    } else if (options.background) {
        set_console_visible(false);
        tray.notify(L"OverLay-Trans 실행 중",
                    L"알림 영역의 아이콘을 오른쪽 클릭하면 페어링 코드 복사, 로그 보기, 종료를 할 수 있습니다.");
    }

    std::printf("Capturing system audio (%u Hz, %u ch). Press Enter to stop.\n", config.sample_rate,
                config.channels);
    SetConsoleCtrlHandler(on_console_event, TRUE);
    // Enter를 기다리는 스레드는 끝낼 방법이 없으므로 떼어 놓는다. 프로세스가 끝나면 함께 끝난다.
    std::thread([] {
        std::getchar();
        SetEvent(quit_event());
    }).detach();
    WaitForSingleObject(quit_event(), INFINITE);

    capture.stop();
    consumer.request_stop();
    consumer.join();
    worker.wait_until_idle();
    wav_dump.close();

    std::printf("Dropped %llu samples.\n", static_cast<unsigned long long>(ring.dropped_samples()));
    return 0;
}

}  // namespace

// 한글/일본어가 들어간 파일 경로를 받을 수 있도록 유니코드 인자를 사용한다.
int wmain(int argc, wchar_t** argv) {
    // 인식 결과(UTF-8)가 콘솔에서 깨지지 않게 한다.
    SetConsoleOutputCP(CP_UTF8);
    std::printf("OverLay-Trans %s\n", OVERLAY_TRANS_VERSION);

    const Options options = parse_options(argc, argv);
    const CaptureConfig config;
    VadConfig vad_config;
    if (!options.early_stt) {
        vad_config.early_silence_ms = 0;
    }

    SpeechRecognizer recognizer;
    const SttConfig stt_config{
        .language = options.language,
        .vocabulary_hint = options.stt_hint,
        .use_gpu = options.use_gpu,
        .gpu_device = options.gpu_device,
        .audio_context = options.stt_audio_context,
    };
    if (!recognizer.init(options.stt_model, stt_config)) {
        std::fprintf(stderr, "Failed to load STT model: %s\n", to_utf8(options.stt_model).c_str());
        return 1;
    }

    Translator translator;
    TranslatorConfig translator_config{
        .gpu_layers = options.use_gpu ? options.llm_gpu_layers : 0,
        .gpu_device = options.gpu_device,
    };
    if (!options.glossary.empty() && !read_text_file(options.glossary, translator_config.glossary)) {
        std::fprintf(stderr, "Failed to read glossary file: %s\n", to_utf8(options.glossary).c_str());
        return 1;
    }
    if (!translator.init(options.llm_model, translator_config)) {
        std::fprintf(stderr, "Failed to load translation model: %s\n", to_utf8(options.llm_model).c_str());
        return 1;
    }

    // 브라우저 확장 프로그램이 자막을 받아 갈 로컬 서버. 페어링 코드를 확장 프로그램에 입력해 연결한다.
    const std::string token = load_or_create_pairing_token(pairing_token_path());

    // 확장 프로그램이 보낸 방송 정보는 서버 스레드에서 도착한다. 여기에 보관해 두었다가
    // 번역을 하는 스레드가 다음 발화를 처리하기 전에 적용한다.
    std::mutex context_mutex;
    std::optional<StreamContext> pending_context;
    std::deque<ReceivedChat> pending_chat;  // 아직 번역 맥락으로 쓰지 않은 채팅
    std::string applied_stream_info;        // 번역 지시문에 마지막으로 넣은 방송 설명

    SubtitleServer server;
    const ServerConfig server_config{
        .port = options.port,
        .token = token,
        .on_context =
            [&](StreamContext context) {
                std::lock_guard lock(context_mutex);
                pending_context = std::move(context);
            },
        .on_chat =
            [&](std::vector<ChatMessage> messages) {
                // 채팅을 번역 맥락으로 쓰는 기능은 기본으로 꺼져 있다. 꺼져 있으면 받은 채팅을 버린다.
                if (!options.chat_context) {
                    return;
                }
                std::lock_guard lock(context_mutex);
                for (ChatMessage& message : messages) {
                    std::string line = message.name + ": " + message.text;
                    if (line.size() > MAX_CHAT_LINE_BYTES) {
                        line.resize(MAX_CHAT_LINE_BYTES);
                        // UTF-8 글자 중간에서 잘렸으면 그 글자를 통째로 버린다.
                        while (!line.empty() && (static_cast<unsigned char>(line.back()) & 0xC0) == 0x80) {
                            line.pop_back();
                        }
                        if (!line.empty() && (static_cast<unsigned char>(line.back()) & 0xC0) == 0xC0) {
                            line.pop_back();
                        }
                    }
                    pending_chat.push_back({Clock::now(), std::move(line)});
                }
                while (pending_chat.size() > MAX_PENDING_CHAT) {
                    pending_chat.pop_front();
                }
            },
    };
    if (token.empty() || !server.start(server_config)) {
        std::fprintf(stderr, "Failed to start the local server on port %d.\n", options.port);
        return 1;
    }
    std::printf("Pairing code: %d-%s\n", options.port, token.c_str());

    // 인식과 번역은 오디오를 받는 스레드와 따로 돌린다. 인식하는 동안에도 발화 구간을 계속 찾아야
    // 다음 발화의 인식을 제때 미리 시작할 수 있다.
    // 게임 음성 등 진행자가 아닌 목소리가 진행자의 말에 합쳐져 함께 다시 번역되지 않게 화자를 구분한다.
    SpeakerIdentifier speaker_identifier;
    if (options.speakers && !speaker_identifier.init(options.speaker_model, {})) {
        std::fprintf(stderr, "Failed to load speaker model: %s\n", to_utf8(options.speaker_model).c_str());
        return 1;
    }

    std::string applied_channel;  // 화자 목록을 만든 방송의 채널

    const bool is_live = options.input_file.empty();
    OpenSentence sentence;
    RecognitionWorker worker(recognizer, [&](const RecognizedSpeech& speech) {
        const auto to_ms = [](Clock::duration duration) {
            return static_cast<long long>(std::chrono::duration_cast<std::chrono::milliseconds>(duration).count());
        };

        std::optional<StreamContext> context;
        {
            std::lock_guard lock(context_mutex);
            context.swap(pending_context);
        }
        if (context) {
            // 방송 화면이 아닌 페이지(목록, 검색 등)에서는 빈 정보가 온다. 그때는 직전 정보를 그대로 둔다.
            const std::string stream_info = build_stream_info(*context);
            if (!stream_info.empty() && stream_info != applied_stream_info) {
                translator.set_stream_info(stream_info);
                applied_stream_info = stream_info;
                std::printf("Stream context: %s\n", stream_info.c_str());
            }
            // 다른 방송으로 옮기면 앞 방송의 목소리는 더 나오지 않으므로 기억한 화자를 잊는다.
            if (!context->channel.empty() && context->channel != applied_channel) {
                speaker_identifier.reset();
                applied_channel = context->channel;
            }
        }

        const std::string& text = speech.text;

        // 로그에 붙일 화자 표시. "speaker 1 (0.72)"처럼 화자 번호와 그 화자와의 유사도를 적고, 진행자로 보이는 화자에는 *를 붙인다.
        std::string speaker_label;
        int speaker = SpeakerIdentifier::UNKNOWN;
        if (options.speakers) {
            float similarity = 0.0f;
            speaker = speaker_identifier.identify(speech.samples, similarity);
            char label[64];
            if (speaker == SpeakerIdentifier::UNKNOWN) {
                std::snprintf(label, sizeof(label), " | speaker ?");
            } else {
                std::snprintf(label, sizeof(label), " | speaker %d%s (%.2f)", speaker,
                              speaker == speaker_identifier.main_speaker() ? "*" : "", similarity);
            }
            speaker_label = label;
        }

        // 감탄사나 웃음소리만 있는 구간은 번역하지 않는다. 앞 문장에 합쳐 다시 번역하면 맞던 번역이 바뀌는 일이 있었다.
        // (体力上がるやすごー + ん -> "체력이 오르길 막막해") 합치고 있던 문장은 그대로 두어, 다음 구간이 이어서 합쳐질 수 있다.
        if (is_japanese_interjection(text)) {
            std::printf("[%.2f s audio | STT %lld ms%s%s | skipped]\n  %s\n",
                        static_cast<double>(speech.samples.size()) /
                            static_cast<double>(config.sample_rate * config.channels),
                        to_ms(speech.recognition_time), speech.started_early ? " (early)" : "",
                        speaker_label.c_str(), text.c_str());
            std::fflush(stdout);
            return;
        }

        const auto translation_started_at = Clock::now();

        // 이 발화 직전까지 올라온 채팅을 번역의 맥락으로 쓴다. 오래된 채팅은 버리고, 한 번 쓴 채팅은 다시 쓰지 않는다.
        std::vector<std::string> chat;
        {
            std::lock_guard lock(context_mutex);
            for (const ReceivedChat& received : pending_chat) {
                if (translation_started_at - received.received_at <= MAX_CHAT_AGE) {
                    chat.push_back(received.line);
                }
            }
            pending_chat.clear();
        }
        if (chat.size() > MAX_CHAT_LINES) {
            chat.erase(chat.begin(), chat.end() - static_cast<std::ptrdiff_t>(MAX_CHAT_LINES));
        }

        // 앞 구간에서 이어진 말이면 앞 구간과 합쳐서 다시 번역한다. 인식에 실패한 구간은 건너뛴다.
        // 화자 구분을 믿을 만큼 긴 구간이 앞 문장과 다른 사람의 말이면 합치지 않는다.
        const double seconds = static_cast<double>(speech.samples.size()) /
                               static_cast<double>(config.sample_rate * config.channels);
        const bool confident_speaker = speaker != SpeakerIdentifier::UNKNOWN && seconds >= MIN_SPEAKER_SPLIT_SECONDS;
        const bool other_speaker = confident_speaker && sentence.speaker != SpeakerIdentifier::UNKNOWN &&
                                   speaker != sentence.speaker;
        const bool continues = options.merge_sentences && speech.silence_before_ms < MAX_SENTENCE_GAP_MS &&
                               !text.empty() && sentence.fragment_count > 0 &&
                               sentence.fragment_count < MAX_SENTENCE_FRAGMENTS &&
                               sentence.source.size() + text.size() <= MAX_SENTENCE_BYTES;
        const bool merges = continues && !other_speaker;
        // 번역문이 만들어지는 동안 만들어진 부분부터 자막으로 보낸다. 긴 문장도 첫 낱말은 금방 뜬다.
        // 합친 문장을 다시 번역할 때는, 화면에 떠 있는 앞 구간의 번역보다 길어진 뒤에 바꿔서 자막이 줄었다 늘지 않게 한다.
        std::optional<Clock::time_point> first_shown_at;
        const Translator::PartialCallback show_partial = [&](std::string_view partial) {
            if (partial.size() <= sentence.translation.size()) {
                return;
            }
            sentence.translation = partial;
            sentence.subtitle = server.publish({sentence.source, sentence.translation, sentence.subtitle});
            if (!first_shown_at) {
                first_shown_at = Clock::now();
            }
        };

        std::string translation;
        if (!text.empty()) {
            if (!merges) {
                sentence = {};
            }
            // 쉬었다가 이어진 구간 사이는 띄워서 번역 모델이 경계를 알 수 있게 한다.
            // 쉬지 않고 이어진 구간은 일본어처럼 띄어 쓰지 않는 글이면 그대로 붙인다.
            if (!sentence.source.empty() && (speech.silence_before_ms >= vad_config.min_silence_ms ||
                                             static_cast<unsigned char>(sentence.source.back()) < 0x80)) {
                sentence.source += ' ';
            }
            sentence.source += text;
            sentence.chat.insert(sentence.chat.end(), chat.begin(), chat.end());
            if (sentence.chat.size() > MAX_CHAT_LINES) {
                sentence.chat.erase(sentence.chat.begin(),
                                    sentence.chat.end() - static_cast<std::ptrdiff_t>(MAX_CHAT_LINES));
            }
            ++sentence.fragment_count;
            if (confident_speaker && sentence.speaker == SpeakerIdentifier::UNKNOWN) {
                sentence.speaker = speaker;
            }
            translation = translator.translate(sentence.source, sentence.chat, merges,
                                               options.stream_subtitles ? show_partial : Translator::PartialCallback{});
        }
        const auto finished_at = Clock::now();
        if (!translation.empty() && !first_shown_at) {
            first_shown_at = finished_at;
        }

        std::printf("[%.2f s audio | STT %lld ms%s | translation %lld ms | chat %zu",
                    static_cast<double>(speech.samples.size()) /
                        static_cast<double>(config.sample_rate * config.channels),
                    to_ms(speech.recognition_time), speech.started_early ? " (early)" : "",
                    to_ms(finished_at - translation_started_at), chat.size());
        if (merges) {
            std::printf(" | merged %zu", sentence.fragment_count);
        } else if (continues) {
            std::printf(" | other speaker");
        }
        std::printf("%s", speaker_label.c_str());
        // 파일은 실제 속도보다 빠르게 처리하므로 지연 시간에 의미가 없다.
        // first는 첫 자막이 뜬 시점, delay는 번역문이 완성된 시점까지의 지연이다.
        if (is_live) {
            if (first_shown_at) {
                std::printf(" | first %lld ms", to_ms(*first_shown_at - speech.speech_ended_at));
            }
            std::printf(" | delay %lld ms", to_ms(finished_at - speech.speech_ended_at));
        }
        std::printf("]\n  %s\n  %s\n", (merges ? sentence.source : text).c_str(), translation.c_str());
        std::fflush(stdout);

        // 완성된 번역문으로 화면의 자막(만들어지던 중의 자막이나 합치기 전 앞 구간의 자막)을 바꾼다.
        if (!translation.empty()) {
            sentence.translation = translation;
            sentence.subtitle = server.publish({sentence.source, translation, sentence.subtitle});
        }
    });

    VadSegmenter vad;
    const VadCallbacks vad_callbacks{
        .on_segment = [&](const SpeechSegment& segment) { worker.submit(segment); },
        .on_pause = [&](std::span<const float> samples) { worker.begin_early(samples); },
        .on_resume = [&] { worker.cancel_early(); },
    };
    const bool vad_ready = vad.init(options.vad_model, vad_config, vad_callbacks);
    if (!vad_ready) {
        std::fprintf(stderr, "Failed to load VAD model: %s\n", to_utf8(options.vad_model).c_str());
        return 1;
    }

    if (!is_live) {
        return run_file(options, config, vad, worker);
    }
    return run_live(options, config, vad_config, vad, worker, std::to_string(options.port) + "-" + token);
}
