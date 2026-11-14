#pragma once

#include <cstddef>
#include <string>
#include <string_view>

namespace Online
{
    // PES's personal-data keyboard reserves the 16th unit for its terminator.
    inline constexpr size_t kMaxPlayerNameLength = 15;

    // These return an empty string for an invalid nickname.
    std::wstring NormalizePlayerName(std::wstring_view name);
    std::string PlayerNameUtf8(std::wstring_view name);
    std::wstring PlayerNameFromUtf8(std::string_view name);

    bool ValidPlayerName(std::wstring_view name);
    std::wstring LoadPlayerName();
    bool SavePlayerName(std::wstring_view name);
}
