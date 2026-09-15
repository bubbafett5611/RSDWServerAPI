#pragma once
#include <string>

namespace Discord {

enum class Channel { Chat, Deaths };

void Start(Channel channel, const std::string& webhookUrl, const std::string& username);
void Stop();
bool IsEnabled(Channel channel);

void Post(Channel channel, const std::string& line);

}
