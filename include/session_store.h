#pragma once
#include <string>
#include <mutex>
#include <unordered_map>
#include <chrono>
#include <openssl/rand.h>

struct SessVal {
    std::string user;
    long long exp;
};

extern std::mutex g_sess_mtx;
extern std::unordered_map<std::string, SessVal> g_sessions;

inline std::string gen_sid() {
    unsigned char buf[32];
    RAND_bytes(buf, sizeof(buf));
    static const char* hex = "0123456789abcdef";
    std::string out;
    out.reserve(64);
    for (size_t i = 0; i < sizeof(buf); ++i) {
        out.push_back(hex[(buf[i] >> 4) & 0xF]);
        out.push_back(hex[buf[i] & 0xF]);
    }
    return out;
}

inline long long now_sec() {
    return std::chrono::duration_cast<std::chrono::seconds>(
               std::chrono::system_clock::now().time_since_epoch())
        .count();
}
