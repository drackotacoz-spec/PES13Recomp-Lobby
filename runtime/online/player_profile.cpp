#include "player_profile.h"

#ifndef WIN32_LEAN_AND_MEAN
#   define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#   define NOMINMAX
#endif
#include <windows.h>

#include <array>
#include <filesystem>

namespace
{
    constexpr wchar_t kProfileFolder[] = L"userdata";
    constexpr wchar_t kProfilePath[] = L"userdata\\online-player.txt";
    constexpr size_t kMaxNameBytes = Online::kMaxPlayerNameLength * 3;

    bool IsOuterSpace(wchar_t ch)
    {
        WORD type{};
        return GetStringTypeW(CT_CTYPE1, &ch, 1, &type) &&
            (type & C1_SPACE) != 0 && (type & C1_CNTRL) == 0;
    }

    bool HiddenModifier(wchar_t ch)
    {
        return ch == 0x034F || (ch >= 0x180B && ch <= 0x180F) ||
            (ch >= 0xFE00 && ch <= 0xFE0F);
    }
}

namespace Online
{
    std::wstring NormalizePlayerName(std::wstring_view name)
    {
        while (!name.empty() && IsOuterSpace(name.front()))
            name.remove_prefix(1);
        while (!name.empty() && IsOuterSpace(name.back()))
            name.remove_suffix(1);
        if (name.size() < 2 || name.size() > kMaxPlayerNameLength)
            return {};

        std::array<WORD, kMaxPlayerNameLength> basic{}, marks{};
        const int length = static_cast<int>(name.size());
        if (!GetStringTypeW(CT_CTYPE1, name.data(), length, basic.data()) ||
            !GetStringTypeW(CT_CTYPE3, name.data(), length, marks.data()))
            return {};

        bool markAllowed = false;
        bool hasLetterOrDigit = false;
        size_t visible = 0;
        for (size_t i = 0; i < name.size(); ++i)
        {
            const wchar_t ch = name[i];
            if ((basic[i] & C1_CNTRL) != 0 ||
                (marks[i] & (C3_HIGHSURROGATE | C3_LOWSURROGATE)) != 0 ||
                HiddenModifier(ch))
                return {};
            if ((basic[i] & (C1_ALPHA | C1_DIGIT)) != 0)
            {
                markAllowed = true;
                hasLetterOrDigit = true;
                ++visible;
                continue;
            }
            if ((marks[i] & C3_NONSPACING) != 0)
            {
                if (!markAllowed) return {};
                continue;
            }
            markAllowed = false;
            if (ch == L' ') continue;
            if (ch != L'-' && ch != L'_' && ch != L'\'' && ch != L'.')
                return {};
            ++visible;
        }
        return visible >= 2 && hasLetterOrDigit ? std::wstring(name) : std::wstring{};
    }

    bool ValidPlayerName(std::wstring_view name)
    {
        return !NormalizePlayerName(name).empty();
    }

    std::string PlayerNameUtf8(std::wstring_view name)
    {
        const auto normalized = NormalizePlayerName(name);
        if (normalized.empty()) return {};
        std::array<char, kMaxNameBytes> bytes{};
        const int length = WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, normalized.data(),
            static_cast<int>(normalized.size()), bytes.data(), static_cast<int>(bytes.size()), nullptr, nullptr);
        return length > 0 ? std::string(bytes.data(), length) : std::string{};
    }

    std::wstring PlayerNameFromUtf8(std::string_view name)
    {
        if (name.empty() || name.size() > kMaxNameBytes) return {};
        std::array<wchar_t, kMaxPlayerNameLength> wide{};
        const int length = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, name.data(),
            static_cast<int>(name.size()), wide.data(), static_cast<int>(wide.size()));
        return length > 0 ? NormalizePlayerName(std::wstring_view(wide.data(), length)) : std::wstring{};
    }

    std::wstring LoadPlayerName()
    {
        const HANDLE file = CreateFileW(kProfilePath, GENERIC_READ,
            FILE_SHARE_READ | FILE_SHARE_DELETE, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (file == INVALID_HANDLE_VALUE) return {};

        std::array<char, kMaxNameBytes + 1> bytes{};
        DWORD length = 0;
        const bool read = ReadFile(file, bytes.data(), static_cast<DWORD>(bytes.size()), &length, nullptr);
        CloseHandle(file);
        return read ? PlayerNameFromUtf8(std::string_view(bytes.data(), length)) : std::wstring{};
    }

    bool SavePlayerName(std::wstring_view name)
    {
        const auto bytes = PlayerNameUtf8(name);
        if (bytes.empty()) return false;

        std::error_code error;
        std::filesystem::create_directories(kProfileFolder, error);
        if (error) return false;

        wchar_t temporary[MAX_PATH]{};
        if (!GetTempFileNameW(kProfileFolder, L"pes", 0, temporary)) return false;
        const HANDLE file = CreateFileW(temporary, GENERIC_WRITE, 0, nullptr, TRUNCATE_EXISTING,
            FILE_ATTRIBUTE_NORMAL, nullptr);
        bool flushed = false;
        if (file != INVALID_HANDLE_VALUE)
        {
            DWORD written = 0;
            flushed = WriteFile(file, bytes.data(), static_cast<DWORD>(bytes.size()), &written, nullptr) &&
                written == bytes.size() && FlushFileBuffers(file);
            CloseHandle(file);
        }

        if (flushed && MoveFileExW(temporary, kProfilePath, MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH))
            return true;
        DeleteFileW(temporary);
        return false;
    }
}
