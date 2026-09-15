#include "webhook.h"
#include "../utils/json.h"
#include "../utils/logger.h"

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdio>
#include <deque>
#include <mutex>
#include <thread>

namespace Discord {

namespace {

bool UrlIsSafe(const std::string& url) {
    if (url.rfind("https://", 0) != 0) return false;
    for (char c : url) {
        if (c == '\'' || c == '"' || c == '`' || c == '$' || c == ';' || c == '|' ||
            c == '&' || c == '<' || c == '>' || c == '\\' || c == '\n' || c == '\r') {
            return false;
        }
    }
    return true;
}

// I need to add embed support, just temporary chat until I get around to it.

struct Sender {
    explicit Sender(const char* senderName) : name(senderName) {}

    std::string name;
    std::string url;
    std::string username = "Dragonwilds";
    std::atomic<bool> enabled{false};
    std::atomic<bool> running{false};
    std::thread* worker = nullptr;

    std::mutex mutex;
    std::condition_variable signal;
    std::deque<std::string> queue;

    void Send(const std::string& content) {
        std::string payload = "{\"username\":\"" + Json::Escape(username) +
                              "\",\"content\":\"" + Json::Escape(content) +
                              "\",\"allowed_mentions\":{\"parse\":[]}}";

        std::string quoted = "'";
        for (char c : payload) {
            if (c == '\'') {
                quoted += "'\\''";
            } else {
                quoted += c;
            }
        }
        quoted += "'";

        std::string command = "curl -s -o /dev/null -m 10 -X POST -H 'Content-Type: application/json' -d " +
                              quoted + " '" + url + "'";

        FILE* pipe = popen(command.c_str(), "r");
        if (!pipe) {
            LogMessage("Discord: could not run curl");
            return;
        }
        pclose(pipe);
    }

    void Run() {
        while (running) {
            std::string batch;
            {
                std::unique_lock<std::mutex> lock(mutex);
                signal.wait_for(lock, std::chrono::seconds(2),
                                [this] { return !queue.empty() || !running; });
                if (!running && queue.empty()) return;

                while (!queue.empty()) {
                    const std::string& next = queue.front();
                    if (!batch.empty() && batch.size() + next.size() + 1 > 1900) break;
                    if (!batch.empty()) batch += "\n";
                    batch += next;
                    queue.pop_front();
                }
            }

            if (!batch.empty()) Send(batch);

            for (int i = 0; i < 10 && running; i++) {
                std::this_thread::sleep_for(std::chrono::milliseconds(100));
            }
        }
    }

    void Start(const std::string& webhookUrl, const std::string& webhookUsername) {
        if (enabled) return;
        if (webhookUrl.empty()) return;

        if (!UrlIsSafe(webhookUrl)) {
            LogMessage("Discord: " + name + " webhook rejected, it must be an https URL with no shell characters");
            return;
        }

        url = webhookUrl;
        if (!webhookUsername.empty()) username = webhookUsername;

        enabled = true;
        running = true;
        worker = new std::thread([this] { Run(); });

        LogMessage("Discord: " + name + " webhook enabled");
    }

    void Stop() {
        if (!running) return;
        running = false;
        signal.notify_all();

        if (worker && worker->joinable()) {
            worker->join();
            delete worker;
            worker = nullptr;
        }
        enabled = false;
    }

    void Post(const std::string& line) {
        if (!enabled || line.empty()) return;

        std::lock_guard<std::mutex> lock(mutex);
        if (queue.size() >= 200) queue.pop_front();
        queue.push_back(line);
        signal.notify_all();
    }
};

Sender g_Chat{"chat"};
Sender g_Deaths{"death"};

Sender& Get(Channel channel) {
    return channel == Channel::Deaths ? g_Deaths : g_Chat;
}

}

bool IsEnabled(Channel channel) { return Get(channel).enabled; }

void Start(Channel channel, const std::string& webhookUrl, const std::string& username) {
    Get(channel).Start(webhookUrl, username);
}

void Stop() {
    g_Chat.Stop();
    g_Deaths.Stop();
}

void Post(Channel channel, const std::string& line) {
    Get(channel).Post(line);
}

}
