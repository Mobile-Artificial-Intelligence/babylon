#include "babylon.h"
#include <nlohmann/json.hpp>

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <map>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

using json = nlohmann::json;

#ifdef _WIN32
  #ifndef WIN32_LEAN_AND_MEAN
    #define WIN32_LEAN_AND_MEAN
  #endif
  #include <winsock2.h>
  #include <ws2tcpip.h>
  #pragma comment(lib, "ws2_32.lib")
  typedef SOCKET sock_t;
  #define INVALID_SOCK INVALID_SOCKET
  #define sock_close(s) closesocket(s)
  #define sock_valid(s) ((s) != INVALID_SOCKET)
#else
  #include <arpa/inet.h>
  #include <netdb.h>
  #include <netinet/in.h>
  #include <sys/socket.h>
  #include <unistd.h>
  typedef int sock_t;
  #define INVALID_SOCK (-1)
  #define sock_close(s) close(s)
  #define sock_valid(s) ((s) >= 0)
#endif

// ─── Config ───────────────────────────────────────────────────────────────────

struct Config {
    std::string phonemizer_model;
    std::string dictionary;
    std::string kokoro_model;
    std::string kokoro_voice;
    std::string kokoro_voices;
    std::string vits_model;
    std::string host = "127.0.0.1";
    int port = 8775;
};

// Returns the directory that contains the running executable (from argv[0]).
// Falls back to "." if argv[0] has no directory component.
static std::string exe_dir(const char* argv0) {
    std::filesystem::path p(argv0);
    auto dir = p.parent_path();
    return dir.empty() ? "." : dir.string();
}

static Config load_config(const std::string& path, bool warn_if_missing = true) {
    Config cfg;
    std::ifstream f(path);
    if (!f) {
        if (warn_if_missing)
            std::cerr << "Warning: cannot open config: " << path << "\n";
        return cfg;
    }
    try {
        json j = json::parse(f);
        if (j.contains("phonemizer_model")) cfg.phonemizer_model = j["phonemizer_model"];
        if (j.contains("dictionary"))       cfg.dictionary       = j["dictionary"];
        if (j.contains("kokoro_model"))     cfg.kokoro_model     = j["kokoro_model"];
        if (j.contains("kokoro_voice"))     cfg.kokoro_voice     = j["kokoro_voice"];
        if (j.contains("kokoro_voices"))    cfg.kokoro_voices    = j["kokoro_voices"];
        if (j.contains("vits_model"))       cfg.vits_model       = j["vits_model"];
        if (j.contains("host"))             cfg.host             = j["host"];
        if (j.contains("port"))             cfg.port             = j["port"];
    } catch (const json::exception& e) {
        std::cerr << "Warning: failed to parse config " << path << ": " << e.what() << "\n";
    }
    return cfg;
}

// ─── Help ─────────────────────────────────────────────────────────────────────

static void print_help_global() {
    std::cout <<
        "Usage: babylon <command> [options]\n"
        "\n"
        "Commands:\n"
        "  phonemize  Convert text to IPA phonemes\n"
        "  tts        Convert text to speech and write a WAV file\n"
        "  serve      Start a REST API server\n"
        "\n"
        "Global options:\n"
        "  --config <path>          Load settings from a JSON config file\n"
        "  --phonemizer-model <path> Phonemizer model (.onnx)\n"
        "  --dictionary <path>      Pronunciation dictionary (.json)\n"
        "  --kokoro-model <path>    Kokoro TTS model (.onnx)\n"
        "  --kokoro-voice <path>    Kokoro voice style file (.bin)\n"
        "  --kokoro-voices <dir>    Directory of Kokoro voice style files\n"
        "  --vits-model <path>      VITS TTS model (.onnx)\n"
        "  -h, --help               Show this help\n"
        "\n"
        "Config file format (JSON):\n"
        "  {\n"
        "    \"phonemizer_model\":      \"models/open-phonemizer.onnx\",\n"
        "    \"dictionary\":    \"models/dictionary.json\",\n"
        "    \"kokoro_model\":  \"models/kokoro.onnx\",\n"
        "    \"kokoro_voice\":  \"models/voices/heart.bin\",\n"
        "    \"kokoro_voices\": \"models/voices\",\n"
        "    \"vits_model\":    \"models/vits.onnx\",\n"
        "    \"host\":          \"127.0.0.1\",\n"
        "    \"port\":          8775\n"
        "  }\n"
        "\n"
        "Run 'babylon <command> --help' for command-specific help.\n";
}

static void print_help_tts() {
    std::cout <<
        "Usage: babylon tts [options] \"<text>\"\n"
        "\n"
        "Convert text to speech.\n"
        "\n"
        "Options:\n"
        "  --kokoro                Use Kokoro TTS engine (default)\n"
        "  --vits                  Use VITS TTS engine\n"
        "  --engine <kokoro|vits>  TTS engine to use (longhand)\n"
        "  -v, --voice <name>      Kokoro voice name (filename without .bin)\n"
        "  --kokoro-voice <name>   Same as --voice\n"
        "  --speed <float>         Speech speed for Kokoro (default: 1.0)\n"
        "  -o <path>               Output WAV file (default: output.wav)\n"
        "  -h, --help              Show this help\n"
        "\n"
        "Voice names are looked up in the kokoro_voices directory from config.\n"
        "  e.g. --voice heart  →  <kokoro_voices>/heart.bin\n"
        "\n"
        "Global model flags (--phonemizer-model, --kokoro-model, etc.) also apply.\n";
}

static void print_help_serve() {
    std::cout <<
        "Usage: babylon serve [options]\n"
        "\n"
        "Start a REST API server.\n"
        "\n"
        "Options:\n"
        "  --host <host>   Bind address (default: 127.0.0.1)\n"
        "  --port <port>   Port (default: 8775)\n"
        "  -h, --help      Show this help\n"
        "\n"
        "Endpoints:\n"
        "  GET  /health       Returns {\"status\":\"ok\"}\n"
        "  GET  /voices       Lists available Kokoro voices\n"
        "  POST /phonemize    Convert text to IPA phonemes\n"
        "  POST /tts          Synthesise speech, returns audio/wav\n"
        "\n"
        "POST /phonemize body (JSON):\n"
        "  {\n"
        "    \"text\":    \"Hello world\",  (required)\n"
        "    \"tokens\":  false           (optional, return token IDs instead of IPA)\n"
        "  }\n"
        "\n"
        "POST /tts body (JSON):\n"
        "  {\n"
        "    \"text\":   \"Hello world\",  (required)\n"
        "    \"engine\": \"kokoro\",       (optional, default: kokoro)\n"
        "    \"voice\":  \"en-US-heart\",  (optional)\n"
        "    \"speed\":  1.0             (optional, default: 1.0)\n"
        "  }\n";
}

static void print_help_phonemize() {
    std::cout <<
        "Usage: babylon phonemize [options] \"<text>\"\n"
        "\n"
        "Convert text to IPA phonemes using the phonemizer model.\n"
        "\n"
        "Options:\n"
        "  --tokens        Print Kokoro token IDs instead of IPA string\n"
        "  -h, --help      Show this help\n";
}

// ─── Utilities ────────────────────────────────────────────────────────────────

static std::vector<uint8_t> read_file_bytes(const std::string& path) {
    std::ifstream f(path, std::ios::binary);
    if (!f) return {};
    return std::vector<uint8_t>((std::istreambuf_iterator<char>(f)),
                                 std::istreambuf_iterator<char>());
}

static std::string tmp_wav_path() {
    char buf[64];
    std::snprintf(buf, sizeof(buf), "babylon_tmp_%lld.wav", (long long)std::time(nullptr));
    return buf;
}

// Strip a known suffix from a string if present.
static std::string strip_suffix(const std::string& s, const std::string& suffix) {
    if (s.size() >= suffix.size() && s.substr(s.size() - suffix.size()) == suffix)
        return s.substr(0, s.size() - suffix.size());
    return s;
}

static std::vector<std::string> list_voice_names(const std::string& dir) {
    std::vector<std::string> voices;
    if (dir.empty()) return voices;
    try {
        for (auto& e : std::filesystem::directory_iterator(dir))
            if (e.is_regular_file() && e.path().extension() == ".bin")
                voices.push_back(e.path().stem().string());
        std::sort(voices.begin(), voices.end());
    } catch (...) {}
    return voices;
}

static std::string resolve_voice(const std::string& voice, const std::string& voices_dir) {
    if (voice.empty()) return voice;
    // Absolute / relative path — use directly
    if (voice.find('/') != std::string::npos || voice.find('\\') != std::string::npos)
        return voice;
    if (voices_dir.empty()) return voice;
    std::string base = strip_suffix(voice, ".bin");
    return voices_dir + "/" + base + ".bin";
}

// ─── Global state ─────────────────────────────────────────────────────────────

static Config g_cfg;
static bool   g_g2p_ready    = false;
static bool   g_kokoro_ready = false;
static bool   g_vits_ready   = false;

static bool init_g2p() {
    if (g_g2p_ready) return true;
    if (g_cfg.phonemizer_model.empty()) { std::cerr << "Error: --phonemizer-model not set.\n"; return false; }
    babylon_g2p_options_t opts{
        g_cfg.dictionary.empty() ? nullptr : g_cfg.dictionary.c_str(),
        1
    };
    if (babylon_g2p_init(g_cfg.phonemizer_model.c_str(), opts) != 0) {
        std::cerr << "Error: failed to load G2P model: " << g_cfg.phonemizer_model << "\n";
        return false;
    }
    g_g2p_ready = true;
    return true;
}

static bool init_kokoro() {
    if (g_kokoro_ready) return true;
    if (g_cfg.kokoro_model.empty()) { std::cerr << "Error: --kokoro-model not set.\n"; return false; }
    if (!init_g2p()) return false;
    if (babylon_kokoro_init(g_cfg.kokoro_model.c_str()) != 0) {
        std::cerr << "Error: failed to load Kokoro model: " << g_cfg.kokoro_model << "\n";
        return false;
    }
    g_kokoro_ready = true;
    return true;
}

static bool init_vits() {
    if (g_vits_ready) return true;
    if (g_cfg.vits_model.empty()) { std::cerr << "Error: --vits-model not set.\n"; return false; }
    if (!init_g2p()) return false;
    if (babylon_tts_init(g_cfg.vits_model.c_str()) != 0) {
        std::cerr << "Error: failed to load VITS model: " << g_cfg.vits_model << "\n";
        return false;
    }
    g_vits_ready = true;
    return true;
}

// ─── Phonemize command ────────────────────────────────────────────────────────

static int cmd_phonemize(int argc, char** argv) {
    bool tokens = false;
    std::string text;

    for (int i = 0; i < argc; ++i) {
        std::string a = argv[i];
        if      (a == "-h" || a == "--help") { print_help_phonemize(); return 0; }
        else if (a == "--tokens")             tokens = true;
        else if (a[0] != '-')                text   = a;
    }

    if (text.empty()) {
        std::cerr << "Error: no text provided.\n\n";
        print_help_phonemize();
        return 1;
    }

    if (!init_g2p()) return 1;

    if (tokens) {
        int* ids = babylon_g2p_tokens(text.c_str());
        if (!ids) { std::cerr << "Error: phonemization failed.\n"; return 1; }
        for (int i = 0; ids[i] != -1; ++i)
            std::cout << (i ? " " : "") << ids[i];
        std::cout << "\n";
        free(ids);
    } else {
        char* phonemes = babylon_g2p(text.c_str());
        if (!phonemes) { std::cerr << "Error: phonemization failed.\n"; return 1; }
        std::cout << phonemes << "\n";
        free(phonemes);
    }

    return 0;
}

// ─── TTS command ──────────────────────────────────────────────────────────────

static int cmd_tts(int argc, char** argv) {
    std::string engine = "kokoro";
    std::string voice  = g_cfg.kokoro_voice;
    std::string output = "output.wav";
    float speed        = 1.0f;
    std::string text;

    for (int i = 0; i < argc; ++i) {
        std::string a = argv[i];
        if      (a == "-h" || a == "--help")                        { print_help_tts(); return 0; }
        else if (a == "--kokoro")                                    engine = "kokoro";
        else if (a == "--vits")                                      engine = "vits";
        else if (a == "--engine"                     && i+1 < argc)  engine = argv[++i];
        else if ((a == "-v" || a == "--voice" ||
                  a == "--kokoro-voice")             && i+1 < argc)  voice  = argv[++i];
        else if (a == "--speed"                      && i+1 < argc)  speed  = std::stof(argv[++i]);
        else if (a == "-o"                           && i+1 < argc)  output = argv[++i];
        else if (a[0] != '-')                                        text   = a;
    }

    if (text.empty()) {
        std::cerr << "Error: no text provided.\n\n";
        print_help_tts();
        return 1;
    }

    if (engine == "vits") {
        if (!init_vits()) return 1;
        babylon_tts(text.c_str(), output.c_str());
    } else {
        if (!init_kokoro()) return 1;
        std::string voice_path = resolve_voice(voice, g_cfg.kokoro_voices);
        if (voice_path.empty()) {
            std::cerr << "Error: --kokoro-voice is required for Kokoro engine.\n";
            return 1;
        }
        babylon_kokoro_tts(text.c_str(), voice_path.c_str(), speed, output.c_str());
    }

    std::cout << "Output: " << output << "\n";
    return 0;
}

// ─── Minimal HTTP/1.1 server ──────────────────────────────────────────────────

struct HttpRequest {
    std::string method;
    std::string path;
    std::map<std::string, std::string> headers;
    std::string body;
};

struct HttpResponse {
    int status = 200;
    std::string content_type = "application/json";
    std::vector<uint8_t> body;

    void text_body(const std::string& s) { body.assign(s.begin(), s.end()); }
};

static std::string recv_line(sock_t s) {
    std::string line;
    char c;
    while (true) {
        int r = recv(s, &c, 1, 0);
        if (r <= 0) break;
        if (c == '\n') break;
        if (c != '\r') line += c;
    }
    return line;
}

static HttpRequest parse_request(sock_t s) {
    HttpRequest req;
    std::istringstream iss(recv_line(s));
    std::string version;
    iss >> req.method >> req.path >> version;

    int content_length = 0;
    while (true) {
        std::string line = recv_line(s);
        if (line.empty()) break;
        auto colon = line.find(':');
        if (colon == std::string::npos) continue;
        std::string key = line.substr(0, colon);
        std::string val = line.substr(colon + 1);
        while (!val.empty() && val[0] == ' ') val.erase(0, 1);
        std::transform(key.begin(), key.end(), key.begin(), ::tolower);
        req.headers[key] = val;
        if (key == "content-length") {
            try { content_length = std::stoi(val); } catch (...) {}
        }
    }

    if (content_length > 0) {
        req.body.resize(content_length);
        int recvd = 0;
        while (recvd < content_length) {
            int r = recv(s, &req.body[recvd], content_length - recvd, 0);
            if (r <= 0) break;
            recvd += r;
        }
    }
    return req;
}

static void send_response(sock_t s, const HttpResponse& res) {
    const char* reason = "OK";
    if (res.status == 400) reason = "Bad Request";
    else if (res.status == 404) reason = "Not Found";
    else if (res.status == 500) reason = "Internal Server Error";

    std::string hdr;
    hdr += "HTTP/1.1 " + std::to_string(res.status) + " " + reason + "\r\n";
    hdr += "Content-Type: " + res.content_type + "\r\n";
    hdr += "Content-Length: " + std::to_string(res.body.size()) + "\r\n";
    hdr += "Access-Control-Allow-Origin: *\r\n";
    hdr += "Connection: close\r\n\r\n";

    send(s, hdr.c_str(), (int)hdr.size(), 0);
    if (!res.body.empty())
        send(s, (const char*)res.body.data(), (int)res.body.size(), 0);
}

// ─── Route handlers ───────────────────────────────────────────────────────────

static HttpResponse route_health() {
    HttpResponse res;
    res.text_body("{\"status\":\"ok\"}");
    return res;
}

static HttpResponse route_voices() {
    HttpResponse res;
    json j = list_voice_names(g_cfg.kokoro_voices);
    res.text_body(j.dump());
    return res;
}

static HttpResponse route_phonemize(const std::string& body) {
    HttpResponse res;

    std::string text;
    bool tokens = false;
    try {
        json j = json::parse(body);
        text   = j.value("text",   "");
        tokens = j.value("tokens", false);
    } catch (...) {
        res.status = 400;
        res.text_body("{\"error\":\"invalid JSON body\"}");
        return res;
    }

    if (text.empty()) {
        res.status = 400;
        res.text_body("{\"error\":\"'text' is required\"}");
        return res;
    }

    if (!init_g2p()) {
        res.status = 500;
        res.text_body("{\"error\":\"G2P not initialized\"}");
        return res;
    }

    if (tokens) {
        int* ids = babylon_g2p_tokens(text.c_str());
        if (!ids) {
            res.status = 500;
            res.text_body("{\"error\":\"phonemization failed\"}");
            return res;
        }
        json arr = json::array();
        for (int i = 0; ids[i] != -1; ++i) arr.push_back(ids[i]);
        free(ids);
        res.text_body(json{{"tokens", arr}}.dump());
    } else {
        char* phonemes = babylon_g2p(text.c_str());
        if (!phonemes) {
            res.status = 500;
            res.text_body("{\"error\":\"phonemization failed\"}");
            return res;
        }
        std::string result(phonemes);
        free(phonemes);
        res.text_body(json{{"phonemes", result}}.dump());
    }

    return res;
}

static HttpResponse route_tts(const std::string& body) {
    HttpResponse res;

    std::string text, engine, voice;
    float speed = 1.0f;
    try {
        json j  = json::parse(body);
        text    = j.value("text",   "");
        engine  = j.value("engine", "");
        voice   = j.value("voice",  "");
        speed   = j.value("speed",  1.0f);
    } catch (...) {
        res.status = 400;
        res.text_body("{\"error\":\"invalid JSON body\"}");
        return res;
    }

    if (engine.empty()) engine = "kokoro";
    if (voice.empty())  voice  = g_cfg.kokoro_voice;

    if (text.empty()) {
        res.status = 400;
        res.text_body("{\"error\":\"'text' is required\"}");
        return res;
    }

    std::string out = tmp_wav_path();

    if (engine == "vits") {
        if (!init_vits()) {
            res.status = 500;
            res.text_body("{\"error\":\"VITS engine not available\"}");
            return res;
        }
        babylon_tts(text.c_str(), out.c_str());
    } else {
        if (!init_kokoro()) {
            res.status = 500;
            res.text_body("{\"error\":\"Kokoro engine not available\"}");
            return res;
        }
        std::string voice_path = resolve_voice(voice, g_cfg.kokoro_voices);
        if (voice_path.empty()) {
            res.status = 400;
            res.text_body("{\"error\":\"'voice' is required for Kokoro engine\"}");
            return res;
        }
        babylon_kokoro_tts(text.c_str(), voice_path.c_str(), speed, out.c_str());
    }

    auto wav = read_file_bytes(out);
    std::remove(out.c_str());

    if (wav.empty()) {
        res.status = 500;
        res.text_body("{\"error\":\"TTS synthesis failed\"}");
        return res;
    }

    res.content_type = "audio/wav";
    res.body = std::move(wav);
    return res;
}

static HttpResponse dispatch(const HttpRequest& req) {
    if (req.method == "GET"  && req.path == "/health")     return route_health();
    if (req.method == "GET"  && req.path == "/voices")     return route_voices();
    if (req.method == "POST" && req.path == "/phonemize")  return route_phonemize(req.body);
    if (req.method == "POST" && req.path == "/tts")        return route_tts(req.body);

    // OPTIONS preflight (CORS)
    if (req.method == "OPTIONS") {
        HttpResponse res;
        res.status = 204;
        res.text_body("");
        return res;
    }

    HttpResponse res;
    res.status = 404;
    res.text_body("{\"error\":\"not found\"}");
    return res;
}

// ─── Serve command ────────────────────────────────────────────────────────────

static int cmd_serve(int argc, char** argv) {
    std::string host = g_cfg.host;
    int port         = g_cfg.port;

    for (int i = 0; i < argc; ++i) {
        std::string a = argv[i];
        if      (a == "-h" || a == "--help")       { print_help_serve(); return 0; }
        else if (a == "--host" && i+1 < argc)      host = argv[++i];
        else if (a == "--port" && i+1 < argc)      port = std::stoi(argv[++i]);
    }

#ifdef _WIN32
    WSADATA wsa;
    WSAStartup(MAKEWORD(2, 2), &wsa);
#endif

    sock_t server = socket(AF_INET, SOCK_STREAM, 0);
    if (!sock_valid(server)) { std::cerr << "Failed to create socket.\n"; return 1; }

    int opt = 1;
    setsockopt(server, SOL_SOCKET, SO_REUSEADDR, (const char*)&opt, sizeof(opt));

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port   = htons((uint16_t)port);
    if (inet_pton(AF_INET, host.c_str(), &addr.sin_addr) <= 0) {
        std::cerr << "Invalid host address: " << host << "\n";
        return 1;
    }

    if (bind(server, (sockaddr*)&addr, sizeof(addr)) < 0) {
        std::cerr << "Failed to bind on " << host << ":" << port << "\n";
        return 1;
    }
    listen(server, 16);

    // Pre-load all configured models
    if (!g_cfg.phonemizer_model.empty())     init_g2p();
    if (!g_cfg.kokoro_model.empty()) init_kokoro();
    if (!g_cfg.vits_model.empty())   init_vits();

    std::cout << "babylon serve  http://" << host << ":" << port << "\n";

    while (true) {
        sockaddr_in client_addr{};
#ifdef _WIN32
        int client_len = sizeof(client_addr);
#else
        socklen_t client_len = sizeof(client_addr);
#endif
        sock_t client = accept(server, (sockaddr*)&client_addr, &client_len);
        if (!sock_valid(client)) continue;

        HttpRequest req = parse_request(client);
        if (!req.method.empty()) {
            std::cout << req.method << " " << req.path << "\n";
            HttpResponse res = dispatch(req);
            send_response(client, res);
        }
        sock_close(client);
    }

    sock_close(server);
#ifdef _WIN32
    WSACleanup();
#endif
    return 0;
}

// ─── Main ─────────────────────────────────────────────────────────────────────

int main(int argc, char** argv) {
    if (argc < 2) { print_help_global(); return 0; }

    // Find subcommand index
    int subcmd_idx = -1;
    for (int i = 1; i < argc; ++i) {
        std::string a = argv[i];
        if (a == "phonemize" || a == "tts" || a == "serve") { subcmd_idx = i; break; }
    }

    // Pass 0: auto-load config.json from the executable's directory (silent if absent)
    {
        auto auto_cfg = exe_dir(argv[0]) + "/config.json";
        if (std::filesystem::exists(auto_cfg))
            g_cfg = load_config(auto_cfg, false);
    }

    // Pass 1: find --config and load it (overrides auto-loaded values)
    for (int i = 1; i < argc; ++i) {
        if (std::string(argv[i]) == "--config" && i + 1 < argc) {
            g_cfg = load_config(argv[i + 1]);
            break;
        }
    }

    // Pass 2: apply all global flags from argv (override config)
    for (int i = 1; i < argc; ++i) {
        std::string a = argv[i];
        if      (a == "-h" || a == "--help")           { if (subcmd_idx == -1) { print_help_global(); return 0; } }
        else if (a == "--phonemizer-model"       && i+1 < argc) g_cfg.phonemizer_model       = argv[++i];
        else if (a == "--dictionary"     && i+1 < argc) g_cfg.dictionary     = argv[++i];
        else if (a == "--kokoro-model"   && i+1 < argc) g_cfg.kokoro_model   = argv[++i];
        else if (a == "--kokoro-voice"   && i+1 < argc) g_cfg.kokoro_voice   = argv[++i];
        else if (a == "--kokoro-voices"  && i+1 < argc) g_cfg.kokoro_voices  = argv[++i];
        else if (a == "--vits-model"     && i+1 < argc) g_cfg.vits_model     = argv[++i];
        else if (a == "--config"         && i+1 < argc) ++i; // already handled
    }

    if (subcmd_idx == -1) {
        std::cerr << "Error: unknown command '" << argv[1] << "'\n\n";
        print_help_global();
        return 1;
    }

    std::string subcmd = argv[subcmd_idx];
    int  sub_argc = argc - subcmd_idx - 1;
    char** sub_argv = argv + subcmd_idx + 1;

    if (subcmd == "phonemize") return cmd_phonemize(sub_argc, sub_argv);
    if (subcmd == "tts")       return cmd_tts(sub_argc, sub_argv);
    if (subcmd == "serve")     return cmd_serve(sub_argc, sub_argv);

    return 0;
}
