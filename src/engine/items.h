#pragma once
#include <cstdint>
#include <string>
#include <vector>

namespace DomItems {

struct ItemInfo {
    std::string name;
    int32_t maxStackSize = 0;
    uintptr_t object = 0;
};

std::vector<ItemInfo> ListLoaded();
uintptr_t FindItem(const std::string& name, std::string& outResolvedName);

bool GiveItem(const std::string& player, const std::string& item, int32_t count,
              std::string& outMessage);

bool GiveItemToController(uintptr_t controller, uintptr_t itemData, int32_t count);

}
