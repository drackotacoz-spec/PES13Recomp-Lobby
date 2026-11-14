#pragma once

#include <string_view>

namespace Online
{
    inline constexpr size_t kChatMaxTextUnits = 180;

    struct ChatReaction
    {
        std::string_view key;
        std::wstring_view command;
        const wchar_t* caption;
        const wchar_t* face;
    };

    inline constexpr ChatReaction kChatReactions[] = {
        { "jaja", L"/jaja", L"JAJA", L"\U0001F602" },
        { "uy", L"/uy", L"\u00a1Uy!", L"\U0001F631" },
        { "payaso", L"/payaso", L"Payaso", L"\U0001F921" },
        { "frio", L"/frio", L"Qu\u00e9 fr\u00edo", L"\U0001F976" },
        { "casi", L"/casi", L"Casi, eh", L"\U0001F60F" },
        { "var", L"/var", L"\u00a1VAR!", L"\U0001F4FA" },
    };

    inline constexpr wchar_t kChatCommandHint[] = L"/jaja   /uy   /payaso   /frio   /casi   /var";

    inline const ChatReaction* FindChatReaction(std::string_view key)
    {
        for (const auto& reaction : kChatReactions)
            if (reaction.key == key) return &reaction;
        return nullptr;
    }
}
