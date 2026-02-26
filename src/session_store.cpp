#include "session_store.h"

std::mutex g_sess_mtx;
std::unordered_map<std::string, SessVal> g_sessions;

