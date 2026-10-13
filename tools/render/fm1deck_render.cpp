// SPDX-License-Identifier: GPL-3.0-only
#include "Fm1FirmwareApi.h"

#include <dlfcn.h>
#include <cctype>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iostream>
#include <map>
#include <sstream>
#include <string>
#include <vector>
#include <memory>
#include <stdexcept>

namespace {

// Simple lightweight JSON parser
enum class JsonType { Object, Array, String, Number, Boolean, Null };

struct JsonValue {
    JsonType type = JsonType::Null;
    std::string str;
    double num = 0.0;
    bool b = false;
    std::map<std::string, JsonValue> obj;
    std::vector<JsonValue> arr;

    bool contains(const std::string& key) const {
        return type == JsonType::Object && obj.find(key) != obj.end();
    }
    const JsonValue& operator[](const std::string& key) const {
        static JsonValue nullVal;
        if (type != JsonType::Object) return nullVal;
        auto it = obj.find(key);
        return it != obj.end() ? it->second : nullVal;
    }
    const JsonValue& operator[](size_t idx) const {
        static JsonValue nullVal;
        if (type != JsonType::Array || idx >= arr.size()) return nullVal;
        return arr[idx];
    }
};

class JsonParser {
public:
    explicit JsonParser(const std::string& src) : src_(src), pos_(0) {}

    JsonValue parse() {
        skipWs();
        return parseValue();
    }

private:
    const std::string& src_;
    size_t pos_;

    void skipWs() {
        while (pos_ < src_.size() && (std::isspace(src_[pos_]) || src_[pos_] == '\r' || src_[pos_] == '\n')) {
            pos_++;
        }
    }

    char peek() const {
        return pos_ < src_.size() ? src_[pos_] : '\0';
    }

    char get() {
        return pos_ < src_.size() ? src_[pos_++] : '\0';
    }

    std::string parseString() {
        get(); // consume '"'
        std::string s;
        while (pos_ < src_.size()) {
            char c = get();
            if (c == '"') break;
            if (c == '\\' && pos_ < src_.size()) {
                c = get();
                if (c == 'n') s += '\n';
                else if (c == 't') s += '\t';
                else if (c == 'r') s += '\r';
                else s += c;
            } else {
                s += c;
            }
        }
        return s;
    }

    JsonValue parseNumber() {
        size_t start = pos_;
        if (peek() == '-') get();
        while (std::isdigit(peek()) || peek() == '.') get();
        std::string s = src_.substr(start, pos_ - start);
        JsonValue v;
        v.type = JsonType::Number;
        v.num = std::stod(s);
        return v;
    }

    JsonValue parseObject() {
        get(); // consume '{'
        JsonValue v;
        v.type = JsonType::Object;
        skipWs();
        if (peek() == '}') { get(); return v; }
        while (pos_ < src_.size()) {
            skipWs();
            if (peek() != '"') break;
            std::string key = parseString();
            skipWs();
            if (peek() == ':') get();
            skipWs();
            v.obj[key] = parseValue();
            skipWs();
            if (peek() == ',') { get(); continue; }
            if (peek() == '}') { get(); break; }
        }
        return v;
    }

    JsonValue parseArray() {
        get(); // consume '['
        JsonValue v;
        v.type = JsonType::Array;
        skipWs();
        if (peek() == ']') { get(); return v; }
        while (pos_ < src_.size()) {
            skipWs();
            v.arr.push_back(parseValue());
            skipWs();
            if (peek() == ',') { get(); continue; }
            if (peek() == ']') { get(); break; }
        }
        return v;
    }

    JsonValue parseValue() {
        skipWs();
        char c = peek();
        if (c == '{') return parseObject();
        if (c == '[') return parseArray();
        if (c == '"') {
            JsonValue v;
            v.type = JsonType::String;
            v.str = parseString();
            return v;
        }
        if (c == '-' || std::isdigit(c)) return parseNumber();
        if (c == 't' || c == 'f') {
            JsonValue v;
            v.type = JsonType::Boolean;
            v.b = (c == 't');
            while (std::isalpha(peek())) get();
            return v;
        }
        if (c == 'n') {
            while (std::isalpha(peek())) get();
            JsonValue v;
            v.type = JsonType::Null;
            return v;
        }
        return JsonValue{};
    }
};

int buttonNameToIndex(const std::string& name) {
    if (name == "FX") return 0;
    if (name == "SCL" || name == "SEL") return 1;
    if (name == "ENV") return 2;
    if (name == "LFO") return 3;
    if (name == "EDIT") return 4;
    if (name == "GLO") return 5;
    if (name == "HOME") return 6;
    if (name == "SAVE") return 7;
    if (name == "ARP") return 8;
    if (name == "SEQ") return 9;
    if (name == "PLAY") return 10;
    if (name == "REC") return 11;
    if (name == "OCTDN" || name == "OCT-" || name == "OCT_DN") return 12;
    if (name == "OCTUP" || name == "OCT+" || name == "OCT_UP") return 13;
    return -1;
}

int encoderRoleToIndex(const std::string& role) {
    if (role == "SELECT" || role == "SEL") return 0;
    if (role == "ALGO") return 1;
    if (role == "PRESET" || role == "PRESETS") return 2;
    if (role == "K1") return 3;
    if (role == "K2") return 4;
    if (role == "K3") return 5;
    if (role == "K4") return 6;
    return -1;
}

int parseNote(const std::string& str) {
    if (str.empty()) return -1;
    char letter = static_cast<char>(std::toupper(str[0]));
    int semi = 0;
    switch (letter) {
        case 'C': semi = 0; break;
        case 'D': semi = 2; break;
        case 'E': semi = 4; break;
        case 'F': semi = 5; break;
        case 'G': semi = 7; break;
        case 'A': semi = 9; break;
        case 'B': semi = 11; break;
        default: return -1;
    }
    size_t i = 1;
    if (i < str.size() && (str[i] == '#' || std::toupper(str[i]) == 'B')) {
        if (str[i] == '#') semi += 1;
        else semi -= 1;
        i++;
    }
    if (i >= str.size()) return -1;
    int oct = std::stoi(str.substr(i));
    int midi = (oct + 1) * 12 + semi;
    int idx = midi - 53;
    if (idx < 0 || idx >= 27) return -1;
    return idx;
}

struct ScriptEvent {
    uint32_t ms = 0;
    bool hasButtons = false;
    uint32_t buttonMask = 0;
    bool hasKeys = false;
    uint32_t keyMask = 0;
    struct Enc { uint32_t role; int32_t steps; };
    std::vector<Enc> encs;
    bool hasMaster = false;
    uint32_t master = 0;
    bool hasMidi = false;
    uint32_t midiStatus = 0, midiD1 = 0, midiD2 = 0;
    std::string call;
};

struct GatedEvent {
    bool isKeys;
    uint32_t mask;
};

} // namespace

int main(int argc, char** argv) {
    if (argc < 3) {
        std::cerr << "Usage: fm1deck_render <script.json> [libfw.so] <out.raw>\n";
        return 1;
    }

    std::string scriptPath = argv[1];
    std::string libPath;
    std::string outPath;

    if (argc >= 4) {
        libPath = argv[2];
        outPath = argv[3];
    } else {
        outPath = argv[2];
    }

    std::ifstream sf(scriptPath);
    if (!sf) {
        std::cerr << "Cannot open script: " << scriptPath << "\n";
        return 1;
    }
    std::string jsonStr((std::istreambuf_iterator<char>(sf)), std::istreambuf_iterator<char>());
    JsonParser parser(jsonStr);
    JsonValue root = parser.parse();

    std::string fwId = root["firmware"].str;
    if (libPath.empty()) {
        libPath = "build/fw-host/libfw_" + fwId + ".so";
    }

    uint32_t seconds = root.contains("seconds") ? static_cast<uint32_t>(root["seconds"].num) : 12;
    uint32_t masterVal = root.contains("master") ? static_cast<uint32_t>(root["master"].num) : 700;
    uint32_t seedVal = root.contains("seed") ? static_cast<uint32_t>(root["seed"].num) : 0;

    std::vector<ScriptEvent> events;
    if (root.contains("events") && root["events"].type == JsonType::Array) {
        for (const auto& evVal : root["events"].arr) {
            ScriptEvent ev;
            ev.ms = evVal.contains("ms") ? static_cast<uint32_t>(evVal["ms"].num) : 0;
            if (evVal.contains("buttons") && evVal["buttons"].type == JsonType::Array) {
                ev.hasButtons = true;
                ev.buttonMask = 0;
                for (const auto& b : evVal["buttons"].arr) {
                    int bidx = buttonNameToIndex(b.str);
                    if (bidx >= 0) ev.buttonMask |= (1u << bidx);
                }
            }
            if (evVal.contains("keys") && evVal["keys"].type == JsonType::Array) {
                ev.hasKeys = true;
                ev.keyMask = 0;
                for (const auto& k : evVal["keys"].arr) {
                    int kidx = parseNote(k.str);
                    if (kidx >= 0) ev.keyMask |= (1u << kidx);
                }
            }
            if (evVal.contains("enc") && evVal["enc"].type == JsonType::Object) {
                for (const auto& kv : evVal["enc"].obj) {
                    int role = encoderRoleToIndex(kv.first);
                    if (role >= 0) {
                        ev.encs.push_back({static_cast<uint32_t>(role), static_cast<int32_t>(kv.second.num)});
                    }
                }
            }
            if (evVal.contains("master")) {
                ev.hasMaster = true;
                ev.master = static_cast<uint32_t>(evVal["master"].num);
            }
            if (evVal.contains("midi") && evVal["midi"].type == JsonType::Array && evVal["midi"].arr.size() >= 3) {
                ev.hasMidi = true;
                ev.midiStatus = static_cast<uint32_t>(evVal["midi"][0].num);
                ev.midiD1 = static_cast<uint32_t>(evVal["midi"][1].num);
                ev.midiD2 = static_cast<uint32_t>(evVal["midi"][2].num);
            }
            if (evVal.contains("call")) {
                ev.call = evVal["call"].str;
            }
            events.push_back(ev);
        }
    }

    void* handle = dlopen(libPath.c_str(), RTLD_NOW);
    if (!handle) {
        std::cerr << "dlopen failed for " << libPath << ": " << dlerror() << "\n";
        return 1;
    }

    auto entry = reinterpret_cast<Fm1FirmwareEntry>(dlsym(handle, FM1DECK_FW_ENTRY));
    if (!entry) {
        std::cerr << "dlsym failed for " << FM1DECK_FW_ENTRY << ": " << dlerror() << "\n";
        dlclose(handle);
        return 1;
    }

    const Fm1FirmwareApi* api = entry();
    if (!api || api->abi != FM1DECK_FW_ABI) {
        std::cerr << "Invalid firmware ABI\n";
        dlclose(handle);
        return 1;
    }

    // Boot order matching worklet.js
    if (api->nor_erase) api->nor_erase();
    if (seedVal != 0 && (api->caps & FM1_CAP_SEED) && api->seed) api->seed(seedVal);
    if (api->master) api->master(masterVal);
    if (api->boot) api->boot();

    FILE* outF = std::fopen(outPath.c_str(), "wb");
    if (!outF) {
        std::cerr << "Cannot open output file: " << outPath << "\n";
        dlclose(handle);
        return 1;
    }

    const uint32_t QUANTUM = 128;
    const uint32_t totalFrames = seconds * 44100;
    const uint32_t HOLD_MS = 24;

    std::vector<GatedEvent> inputQueue;
    uint32_t nextOkMs = 0;
    size_t eventIdx = 0;
    uint32_t framesRendered = 0;

    std::vector<float> bufL(QUANTUM);
    std::vector<float> bufR(QUANTUM);
    std::vector<float> interleaved(QUANTUM * 2);

    while (framesRendered < totalFrames) {
        uint32_t nowMs = api->now_ms ? api->now_ms() : 0;

        // Ingest events due by now
        while (eventIdx < events.size() && events[eventIdx].ms <= nowMs) {
            const auto& ev = events[eventIdx];
            if (ev.hasButtons) {
                inputQueue.push_back({false, ev.buttonMask});
            }
            if (ev.hasKeys) {
                inputQueue.push_back({true, ev.keyMask});
            }
            for (const auto& enc : ev.encs) {
                if (api->enc) api->enc(enc.role, enc.steps);
            }
            if (ev.hasMaster) {
                if (api->master) api->master(ev.master);
            }
            if (ev.hasMidi) {
                if (api->midi) api->midi(ev.midiStatus, ev.midiD1, ev.midiD2);
            }
            if (ev.call == "test_heavy") {
                if ((api->caps & FM1_CAP_TEST_HEAVY) && api->test_heavy) {
                    api->test_heavy();
                }
            }
            eventIdx++;
        }

        // Apply gated inputs per HOLD_MS
        nowMs = api->now_ms ? api->now_ms() : 0;
        while (!inputQueue.empty() && static_cast<int32_t>(nowMs - nextOkMs) >= 0) {
            auto ev = inputQueue.front();
            inputQueue.erase(inputQueue.begin());
            if (ev.isKeys) {
                if (api->keys) api->keys(ev.mask);
            } else {
                if (api->buttons) api->buttons(ev.mask);
            }
            nextOkMs = nowMs + HOLD_MS;
        }

        uint32_t framesThisStep = std::min(QUANTUM, totalFrames - framesRendered);
        api->render(framesThisStep, bufL.data(), bufR.data());
        for (uint32_t i = 0; i < framesThisStep; ++i) {
            interleaved[2 * i] = bufL[i];
            interleaved[2 * i + 1] = bufR[i];
        }
        std::fwrite(interleaved.data(), sizeof(float), 2 * framesThisStep, outF);
        framesRendered += framesThisStep;
    }

    std::fclose(outF);
    dlclose(handle);

    std::cout << "Rendered " << framesRendered << " frames (" << (framesRendered / 44100.0) << " s) to " << outPath << "\n";
    return 0;
}
