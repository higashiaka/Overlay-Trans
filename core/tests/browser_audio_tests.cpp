#include "audio/browser_audio.h"
#include "server/subtitle_server.h"
#include <httplib.h>
#include <nlohmann/json.hpp>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <limits>
#include <thread>

using namespace overlay_trans;
using namespace std::chrono_literals;
#define CHECK(condition) do { if (!(condition)) { std::fprintf(stderr, "FAIL line %d: %s\n", __LINE__, #condition); std::exit(1); } } while (false)
int64_t now_ms() {
    return std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count();
}

int main() {
    BrowserAudioInput input;
    std::vector<float> samples(1600, 0.25f);
    const auto timestamp = now_ms();
    CHECK(!input.start("not-ready"));
    input.set_ready(true);
    CHECK(input.start("one"));
    CHECK(!input.start("two"));
    CHECK(!input.push("two", 0, timestamp, samples));
    CHECK(!input.push("one", 1, timestamp, samples));
    CHECK(!input.push("one", 0, timestamp - 10000, samples));
    samples[0] = std::numeric_limits<float>::quiet_NaN();
    CHECK(!input.push("one", 0, timestamp, samples));
    samples[0] = 0.25f;
    CHECK(input.push("one", 0, timestamp, samples));
    CHECK(!input.push("one", 0, timestamp + 100, samples));
    CHECK(!input.push("one", 1, timestamp, samples));
    BrowserAudioInput::Chunk chunk;
    CHECK(input.pop(chunk));
    CHECK(chunk.session == "one" && chunk.ended_at_ms == timestamp && chunk.samples == samples);
    CHECK(!input.pop(chunk));
    input.stop("two");
    CHECK(input.session() == "one");
    input.stop("one");
    CHECK(input.session().empty());
    CHECK(input.start("bounded"));
    for (int i = 0; i < 20; ++i) CHECK(input.push("bounded", i, timestamp + i, samples));
    CHECK(!input.push("bounded", 20, timestamp + 20, samples));
    input.stop("bounded");

    SubtitleServer server;
    ServerConfig config;
    config.token = "test-key";
    config.browser_audio = &input;
    bool bound = false;
    for (int port = 48650; port < 48670; ++port) {
        config.port = port;
        if (server.start(config)) { bound = true; break; }
    }
    CHECK(bound);
    httplib::Client client("127.0.0.1", config.port);
    client.set_connection_timeout(2, 0);
    client.set_read_timeout(2, 0);
    const httplib::Headers headers{{"Authorization", "Bearer test-key"}};
    auto response = client.Post("/v1/audio/start", R"({"session":"http"})", "application/json");
    CHECK(response && response->status == 401);
    response = client.Post("/v1/audio/start", headers, R"({"session":"http"})", "application/json");
    CHECK(response && response->status == 200);
    response = client.Post("/v1/audio/start", headers, R"({"session":"other"})", "application/json");
    CHECK(response && response->status == 409);
    const std::string pcm(reinterpret_cast<const char*>(samples.data()), samples.size() * sizeof(float));
    const auto path = "/v1/audio?session=http&sequence=0&ended_at_ms=" + std::to_string(now_ms());
    response = client.Post(path, headers, pcm, "application/octet-stream");
    CHECK(response && response->status == 200);
    CHECK(input.pop(chunk) && chunk.samples == samples);
    response = client.Post(path, headers, pcm, "application/octet-stream");
    CHECK(response && response->status == 409);
    response = client.Post(path, headers, "bad", "application/octet-stream");
    CHECK(response && response->status == 400);
    response = client.Post(path, httplib::Headers{{"Authorization", "Bearer test-key"}, {"Origin", "https://example.com"}}, pcm, "application/octet-stream");
    CHECK(response && response->status == 403);
    server.publish({"source", "translation", 0, "http", timestamp, 2300, true});
    response = client.Get("/v1/subtitles?after=0", headers);
    CHECK(response && response->status == 200);
    const auto subtitle = nlohmann::json::parse(response->body)["subtitles"][0];
    CHECK(subtitle["audio_session"] == "http" && subtitle["latency_ms"] == 2300 && subtitle["final"] == true);
    response = client.Post("/v1/audio/stop", headers, R"({"session":"http"})", "application/json");
    CHECK(response && response->status == 200 && input.session().empty());
    CHECK(input.start("lease"));
    std::this_thread::sleep_for(5100ms);
    CHECK(input.session().empty());
    CHECK(input.start("recovered"));
    input.set_ready(false);
    CHECK(input.session().empty() && !input.start("disabled"));
    server.stop();
    std::puts("Browser audio queue, authenticated HTTP ingestion, subtitle metadata and lease recovery passed.");
}
