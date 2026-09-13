#include "bans.h"
#include "dom_engine.h"
#include "process_event.h"
#include "../config/config.h"
#include "../utils/json.h"
#include "../utils/logger.h"

#include <algorithm>
#include <ctime>
#include <fstream>
#include <mutex>
#include <sstream>

namespace DomBans {

namespace {

using namespace DomEngine;

std::mutex g_Mutex;
std::vector<BanEntry> g_Bans;

std::string Path() {
    return GetBaseDir() + "/bans.json";
}

std::string Lower(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(),
                   [](unsigned char c) { return (char)tolower(c); });
    return value;
}

std::string Now() {
    time_t now = time(nullptr);
    struct tm tmval;
    gmtime_r(&now, &tmval);
    char buf[32];
    strftime(buf, sizeof(buf), "%Y-%m-%dT%H:%M:%SZ", &tmval);
    return buf;
}

void SaveLocked() {
    std::ofstream file(Path(), std::ios::out | std::ios::trunc);
    if (!file.is_open()) {
        LogMessage("Bans: could not write " + Path());
        return;
    }

    file << "[\n";
    for (size_t i = 0; i < g_Bans.size(); i++) {
        const BanEntry& ban = g_Bans[i];
        file << "  {\"netId\":\"" << Json::Escape(ban.netId)
             << "\",\"name\":\"" << Json::Escape(ban.name)
             << "\",\"reason\":\"" << Json::Escape(ban.reason)
             << "\",\"bannedAt\":\"" << Json::Escape(ban.bannedAt) << "\"}";
        if (i + 1 < g_Bans.size()) file << ",";
        file << "\n";
    }
    file << "]\n";
}

}

void Load() {
    std::lock_guard<std::mutex> lock(g_Mutex);
    g_Bans.clear();

    std::ifstream file(Path());
    if (!file.is_open()) return;

    std::stringstream buffer;
    buffer << file.rdbuf();
    std::string content = buffer.str();

    size_t pos = 0;
    while ((pos = content.find('{', pos)) != std::string::npos) {
        size_t end = content.find('}', pos);
        if (end == std::string::npos) break;
        std::string object = content.substr(pos, end - pos + 1);
        pos = end + 1;

        BanEntry ban;
        Json::GetString(object, "netId", ban.netId);
        Json::GetString(object, "name", ban.name);
        Json::GetString(object, "reason", ban.reason);
        Json::GetString(object, "bannedAt", ban.bannedAt);
        if (!ban.netId.empty()) g_Bans.push_back(ban);
    }

    LogMessage("Bans: loaded " + std::to_string(g_Bans.size()) + " from " + Path());
}

std::vector<BanEntry> All() {
    std::lock_guard<std::mutex> lock(g_Mutex);
    return g_Bans;
}

bool IsBanned(const std::string& netId) {
    if (netId.empty()) return false;
    std::lock_guard<std::mutex> lock(g_Mutex);
    for (const BanEntry& ban : g_Bans) {
        if (ban.netId == netId) return true;
    }
    return false;
}

bool Ban(const std::string& player, const std::string& reason, std::string& outMessage) {
    Engine* engine = g_Engine;
    if (!engine || !engine->IsInitialized()) {
        outMessage = "engine not initialised";
        return false;
    }

    std::string netId;
    std::string name;
    std::string wanted = Lower(player);
    for (const PlayerInfo& p : engine->GetAllPlayers()) {
        if (std::to_string(p.playerId) == player || Lower(p.uniqueNetId) == wanted ||
            Lower(p.characterGuid) == wanted || Lower(p.characterName) == wanted ||
            Lower(p.name) == wanted) {
            netId = p.uniqueNetId;
            name = p.name;
            break;
        }
    }

    if (netId.empty()) {
        bool looksLikeSteamId = player.size() == 17 &&
                                player.find_first_not_of("0123456789") == std::string::npos;
        if (!looksLikeSteamId) {
            outMessage = "no connected player matches '" + player +
                         "'; to ban someone offline use their SteamID64";
            return false;
        }
        netId = player;
    }

    {
        std::lock_guard<std::mutex> lock(g_Mutex);
        for (const BanEntry& ban : g_Bans) {
            if (ban.netId == netId) {
                outMessage = (name.empty() ? netId : name) + " is already banned";
                return false;
            }
        }
        g_Bans.push_back({netId, name, reason, Now()});
        SaveLocked();
    }

    std::string kickError;
    bool online = engine->KickPlayer(netId, reason, kickError);

    outMessage = "banned " + (name.empty() ? netId : name) + " (" + reason + ")" +
                 (online ? "" : ", not currently online");
    LogMessage("Bans: " + outMessage);
    return true;
}

bool Unban(const std::string& identifier, std::string& outMessage) {
    std::string wanted = Lower(identifier);
    std::lock_guard<std::mutex> lock(g_Mutex);
    for (size_t i = 0; i < g_Bans.size(); i++) {
        if (Lower(g_Bans[i].netId) != wanted && Lower(g_Bans[i].name) != wanted) continue;

        outMessage = "unbanned " + (g_Bans[i].name.empty() ? g_Bans[i].netId : g_Bans[i].name);
        g_Bans.erase(g_Bans.begin() + (long)i);
        SaveLocked();
        LogMessage("Bans: " + outMessage);
        return true;
    }

    outMessage = "no ban matches '" + identifier + "'";
    return false;
}

void Enforce() {
    Engine* engine = g_Engine;
    if (!engine || !engine->IsInitialized() || !GameThread::IsReady()) return;

    std::vector<BanEntry> bans = All();
    if (bans.empty()) return;

    for (const PlayerInfo& player : engine->GetAllPlayers()) {
        if (player.uniqueNetId.empty()) continue;
        for (const BanEntry& ban : bans) {
            if (ban.netId != player.uniqueNetId) continue;
            LogMessage("Bans: '" + player.name + "' connected while banned, removing");
            std::string error;
            engine->KickPlayer(player.uniqueNetId, ban.reason, error);
            break;
        }
    }
}

}
