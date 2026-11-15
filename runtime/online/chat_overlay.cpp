#include "chat_overlay.h"
#include "room_dialog.h"
#include "chat_reactions.h"
#include "lobby_client.h"

#include <d2d1.h>
#include <dwrite.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <memory>
#include <string>

namespace Online
{
    namespace
    {
        using Clock = std::chrono::steady_clock;
        using namespace std::chrono_literals;
        struct ReleaseInterface
        {
            void operator()(IUnknown* value) const { value->Release(); }
        };

        template<typename Interface>
        using ComPointer = std::unique_ptr<Interface, ReleaseInterface>;

        constexpr wchar_t kComposerClass[] = L"PES13ChatComposer";
        constexpr wchar_t kReactionClass[] = L"PES13ReactionOverlay";
        constexpr wchar_t kToastClass[] = L"PES13ChatToast";
        constexpr UINT kComposerText = 301;
        constexpr UINT kComposerSend = 302;
        constexpr UINT kComposerStatus = 303;
        constexpr COLORREF kBackground = RGB(20, 20, 26);
        constexpr COLORREF kField = RGB(35, 35, 44);
        constexpr COLORREF kText = RGB(247, 246, 250);
        constexpr COLORREF kMuted = RGB(193, 192, 209);
        constexpr COLORREF kOrange = RGB(255, 177, 65);

        struct Notice
        {
            HWND window = nullptr;
            Clock::time_point expires{};
            std::wstring author;
            std::wstring text;
            const ChatReaction* reaction = nullptr;
        };

        struct ChatOverlay
        {
            HWND composer = nullptr;
            HWND edit = nullptr;
            HWND status = nullptr;
            HFONT body = nullptr;
            HFONT label = nullptr;
            HFONT title = nullptr;
            HBRUSH field = nullptr;
            HBRUSH background = nullptr;
            HWND game = nullptr;
            Notice toast;
            Notice sticker;
            std::string matchId;
            uint64_t seen = 0;
            std::wstring lastError;
        } g_chat;

        void PaintRect(HDC dc, const RECT& rect, COLORREF color)
        {
            const auto brush = CreateSolidBrush(color);
            FillRect(dc, &rect, brush);
            DeleteObject(brush);
        }

        void PaintText(HDC dc, RECT rect, const std::wstring& text, HFONT font, COLORREF color, UINT flags)
        {
            const auto prior = SelectObject(dc, font);
            const auto previous = SetTextColor(dc, color);
            const auto mode = SetBkMode(dc, TRANSPARENT);
            DrawTextW(dc, text.c_str(), -1, &rect, flags | DT_NOPREFIX);
            SetBkMode(dc, mode);
            SetTextColor(dc, previous);
            SelectObject(dc, prior);
        }

        bool PaintEmoji(HDC dc, const RECT& rect, const ChatReaction& reaction)
        {
            static ComPointer<ID2D1Factory> d2d;
            static ComPointer<IDWriteFactory> dwrite;
            static ComPointer<IDWriteTextFormat> glyph;
            if (!d2d)
            {
                ID2D1Factory* factory = nullptr;
                if (FAILED(D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED, &factory))) return false;
                d2d.reset(factory);
            }
            if (!dwrite)
            {
                IUnknown* factory = nullptr;
                if (FAILED(DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED, __uuidof(IDWriteFactory), &factory)))
                    return false;
                dwrite.reset(static_cast<IDWriteFactory*>(factory));
            }
            if (!glyph)
            {
                IDWriteTextFormat* font = nullptr;
                if (FAILED(dwrite->CreateTextFormat(L"Segoe UI Emoji", nullptr, DWRITE_FONT_WEIGHT_NORMAL,
                    DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL, 60, L"es-AR", &font)))
                    return false;
                glyph.reset(font);
                glyph->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_CENTER);
                glyph->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
            }
            const auto format = D2D1::RenderTargetProperties(D2D1_RENDER_TARGET_TYPE_DEFAULT,
                D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_IGNORE), 96, 96);
            ID2D1DCRenderTarget* surface = nullptr;
            if (FAILED(d2d->CreateDCRenderTarget(&format, &surface))) return false;
            const ComPointer<ID2D1DCRenderTarget> target(surface);
            if (FAILED(target->BindDC(dc, &rect))) return false;
            ID2D1SolidColorBrush* brush = nullptr;
            if (FAILED(target->CreateSolidColorBrush(D2D1::ColorF(0xFFF4D0), &brush))) return false;
            const ComPointer<ID2D1SolidColorBrush> color(brush);
            target->BeginDraw();
            target->Clear(D2D1::ColorF(0x382638));
            target->DrawText(reaction.face, static_cast<UINT32>(std::wstring_view(reaction.face).size()), glyph.get(),
                D2D1::RectF(0, 0, float(rect.right - rect.left), float(rect.bottom - rect.top)), color.get(),
                D2D1_DRAW_TEXT_OPTIONS_ENABLE_COLOR_FONT);
            return SUCCEEDED(target->EndDraw());
        }

        void PaintNotice(HWND window, HDC dc, const Notice& notice)
        {
            RECT area{};
            GetClientRect(window, &area);
            PaintRect(dc, area, kBackground);
            const bool sticker = notice.reaction != nullptr;
            const auto accent = sticker ? kOrange : RGB(96, 225, 184);
            PaintRect(dc, { 0, 0, 5, area.bottom }, accent);
            if (sticker)
            {
                const RECT icon{ 13, 10, 99, area.bottom - 10 };
                if (!PaintEmoji(dc, icon, *notice.reaction))
                {
                    PaintRect(dc, icon, RGB(56, 38, 56));
                    PaintText(dc, icon, notice.reaction->face, g_chat.title, kOrange,
                        DT_CENTER | DT_SINGLELINE | DT_VCENTER);
                }
                PaintText(dc, { 112, 12, area.right - 13, 32 }, notice.author, g_chat.label, accent,
                    DT_SINGLELINE | DT_END_ELLIPSIS | DT_VCENTER);
                PaintText(dc, { 110, 31, area.right - 13, area.bottom - 10 }, notice.text, g_chat.title, kText,
                    DT_SINGLELINE | DT_END_ELLIPSIS | DT_VCENTER);
                return;
            }
            if (notice.author.empty())
            {
                PaintText(dc, { 15, 9, area.right - 12, area.bottom - 8 }, notice.text, g_chat.body, kText,
                    DT_WORDBREAK | DT_END_ELLIPSIS | DT_EDITCONTROL);
                return;
            }
            PaintText(dc, { 15, 7, area.right - 12, 26 }, notice.author, g_chat.label, accent,
                DT_SINGLELINE | DT_END_ELLIPSIS | DT_VCENTER);
            PaintText(dc, { 15, 28, area.right - 12, area.bottom - 7 }, notice.text, g_chat.body, kText,
                DT_WORDBREAK | DT_END_ELLIPSIS | DT_EDITCONTROL);
        }

        LRESULT CALLBACK NoticeProc(HWND window, UINT message, WPARAM wp, LPARAM lp)
        {
            if (message == WM_NCHITTEST) return HTTRANSPARENT;
            if (message == WM_MOUSEACTIVATE) return MA_NOACTIVATE;
            if (message == WM_ERASEBKGND) return 1;
            if (message == WM_NCCREATE)
            {
                const auto creation = reinterpret_cast<const CREATESTRUCTW*>(lp);
                SetWindowLongPtrW(window, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(creation->lpCreateParams));
            }
            const auto notice = reinterpret_cast<const Notice*>(GetWindowLongPtrW(window, GWLP_USERDATA));
            if (notice && message == WM_PRINTCLIENT)
            {
                PaintNotice(window, reinterpret_cast<HDC>(wp), *notice);
                return 0;
            }
            if (notice && message == WM_PAINT)
            {
                PAINTSTRUCT paint{};
                const auto dc = BeginPaint(window, &paint);
                PaintNotice(window, dc, *notice);
                EndPaint(window, &paint);
                return 0;
            }
            return DefWindowProcW(window, message, wp, lp);
        }

        RECT GameArea()
        {
            RECT area{};
            GetClientRect(g_chat.game, &area);
            POINT origin{ area.left, area.top };
            ClientToScreen(g_chat.game, &origin);
            OffsetRect(&area, origin.x, origin.y);
            return area;
        }

        void PositionNotice(Notice& notice)
        {
            if (!notice.window) return;
            const auto area = GameArea();
            if (IsIconic(g_chat.game) || Clock::now() >= notice.expires || area.right - area.left < 240)
            {
                if (IsWindowVisible(notice.window)) ShowWindow(notice.window, SW_HIDE);
                return;
            }
            const bool sticker = notice.reaction != nullptr;
            const int width = std::min(sticker ? 330 : 390, int(area.right - area.left) - 24);
            const int height = sticker ? 104 : 84;
            const int left = sticker ? area.right - width - 18 : area.left + 18;
            const int top = sticker ? area.top + 26 : std::max(area.top + 8, area.bottom - height - 24);
            RECT current{};
            GetWindowRect(notice.window, &current);
            if (!IsWindowVisible(notice.window) || current.left != left || current.top != top ||
                current.right - current.left != width || current.bottom - current.top != height)
                SetWindowPos(notice.window, nullptr, left, top, width, height, SWP_NOACTIVATE | SWP_NOZORDER | SWP_SHOWWINDOW);
        }

        bool RegisterOverlayClass(const wchar_t* name, WNDPROC procedure)
        {
            WNDCLASSW definition{};
            definition.lpfnWndProc = procedure;
            definition.hInstance = GetModuleHandleW(nullptr);
            definition.hCursor = LoadCursor(nullptr, IDC_ARROW);
            definition.lpszClassName = name;
            return RegisterClassW(&definition) != 0;
        }

        void InitAppearance()
        {
            if (g_chat.body) return;
            g_chat.background = CreateSolidBrush(kBackground);
            g_chat.field = CreateSolidBrush(kField);
            g_chat.body = CreateFontW(-17, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
                0, 0, CLEARTYPE_QUALITY, 0, L"Segoe UI");
            g_chat.label = CreateFontW(-16, 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
                0, 0, CLEARTYPE_QUALITY, 0, L"Segoe UI");
            g_chat.title = CreateFontW(-31, 0, 0, 0, FW_HEAVY, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
                0, 0, CLEARTYPE_QUALITY, 0, L"Segoe UI");
        }

        void ShowNotice(Notice& notice, std::wstring author, std::wstring message, const ChatReaction* reaction = nullptr)
        {
            if (!notice.window)
            {
                const auto windowClass = reaction ? kReactionClass : kToastClass;
                if (!RegisterOverlayClass(windowClass, NoticeProc)) return;
                InitAppearance();
                notice.window = CreateWindowExW(WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE, windowClass, L"", WS_POPUP,
                    0, 0, 1, 1, g_chat.game, nullptr, GetModuleHandleW(nullptr), &notice);
                if (!notice.window) return;
            }
            notice.author = std::move(author);
            notice.text = std::move(message);
            notice.reaction = reaction;
            notice.expires = Clock::now() + (reaction ? 3500ms : 4200ms);
            const auto caption = notice.author.empty() ? notice.text : notice.author + L" \u00b7 " + notice.text;
            SetWindowTextW(notice.window, caption.c_str());
            PositionNotice(notice);
            InvalidateRect(notice.window, nullptr, FALSE);
        }

        void HideComposer(bool restoreFocus)
        {
            if (!g_chat.composer || !IsWindowVisible(g_chat.composer)) return;
            ShowWindow(g_chat.composer, SW_HIDE);
            if (restoreFocus)
            {
                SetForegroundWindow(g_chat.game);
                SetFocus(g_chat.game);
            }
        }

        void SendComposer()
        {
            std::array<wchar_t, kChatMaxTextUnits + 1> buffer{};
            const int length = GetWindowTextW(g_chat.edit, buffer.data(), static_cast<int>(buffer.size()));
            std::wstring error;
            if (Lobby::SubmitChat(std::wstring_view(buffer.data(), length), error))
            {
                SetWindowTextW(g_chat.edit, L"");
                SetWindowTextW(g_chat.status, L"Enter para enviar   \u00b7   Esc para volver");
                HideComposer(true);
                return;
            }
            SetWindowTextW(g_chat.status, error.empty() ? L"Escrib\u00ed un mensaje o una reacci\u00f3n." : error.c_str());
            SetFocus(g_chat.edit);
        }

        LRESULT CALLBACK ComposerProc(HWND window, UINT message, WPARAM wp, LPARAM lp)
        {
            switch (message)
            {
            case WM_ERASEBKGND: return 1;
            case WM_CTLCOLOREDIT:
                SetTextColor(reinterpret_cast<HDC>(wp), kText);
                SetBkColor(reinterpret_cast<HDC>(wp), kField);
                return reinterpret_cast<LRESULT>(g_chat.field);
            case WM_CTLCOLORSTATIC:
                SetTextColor(reinterpret_cast<HDC>(wp), kMuted);
                SetBkColor(reinterpret_cast<HDC>(wp), kBackground);
                return reinterpret_cast<LRESULT>(g_chat.background);
            case WM_CLOSE:
                HideComposer(true);
                return 0;
            case WM_COMMAND:
                if (LOWORD(wp) == kComposerSend && HIWORD(wp) == BN_CLICKED)
                {
                    SendComposer();
                    return 0;
                }
                break;
            case WM_PAINT:
            {
                RECT area{};
                GetClientRect(window, &area);
                PAINTSTRUCT paint{};
                const auto dc = BeginPaint(window, &paint);
                PaintRect(dc, area, kBackground);
                PaintRect(dc, { 0, 0, area.right, 3 }, kOrange);
                PaintText(dc, { 14, 10, area.right - 12, 36 }, L"CHAT DE SALA", g_chat.label, kText,
                    DT_SINGLELINE | DT_VCENTER);
                PaintText(dc, { 14, 80, area.right - 12, 104 }, kChatCommandHint, g_chat.body, kOrange,
                    DT_SINGLELINE | DT_VCENTER | DT_END_ELLIPSIS);
                EndPaint(window, &paint);
                return 0;
            }
            }
            return DefWindowProcW(window, message, wp, lp);
        }

        HWND ComposerControl(const wchar_t* kind, const wchar_t* label, DWORD style, UINT id)
        {
            const auto child = CreateWindowExW(0, kind, label, WS_CHILD | WS_VISIBLE | style, 0, 0, 0, 0,
                g_chat.composer, reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)), GetModuleHandleW(nullptr), nullptr);
            SendMessageW(child, WM_SETFONT, reinterpret_cast<WPARAM>(g_chat.body), FALSE);
            return child;
        }

        bool CreateComposer()
        {
            if (!RegisterOverlayClass(kComposerClass, ComposerProc)) return false;
            InitAppearance();
            g_chat.composer = CreateWindowExW(WS_EX_TOOLWINDOW | WS_EX_CONTROLPARENT, kComposerClass, L"Chat de sala", WS_POPUP,
                0, 0, 1, 1, g_chat.game, nullptr, GetModuleHandleW(nullptr), nullptr);
            if (!g_chat.composer) return false;
            g_chat.edit = ComposerControl(L"EDIT", L"", WS_TABSTOP | ES_AUTOHSCROLL | WS_BORDER, kComposerText);
            ComposerControl(L"BUTTON", L"Enviar", WS_TABSTOP | BS_PUSHBUTTON, kComposerSend);
            g_chat.status = ComposerControl(L"STATIC", L"Enter para enviar   \u00b7   Esc para volver", SS_LEFT, kComposerStatus);
            SendMessageW(g_chat.edit, EM_SETLIMITTEXT, kChatMaxTextUnits, 0);
            SendMessageW(g_chat.edit, EM_SETMARGINS, EC_LEFTMARGIN | EC_RIGHTMARGIN, MAKELPARAM(6, 6));
            return true;
        }
    }

    void ToggleChatComposer(HWND gameWindow)
    {
        g_chat.game = gameWindow;
        if (g_chat.composer && IsWindowVisible(g_chat.composer))
        {
            HideComposer(true);
            return;
        }
        const auto state = Lobby::GetClient().GetState();
        if (!state.match)
        {
            ShowNotice(g_chat.toast, {}, L"Entr\u00e1 a una sala con otro jugador para usar el chat. F1 abre el lobby.");
            return;
        }
        if (!g_chat.composer && !CreateComposer()) return;
        const auto area = GameArea();
        const int width = std::clamp(int(area.right - area.left) - 36, 210, 520);
        constexpr int height = 139;
        const int left = area.left + 18;
        const int top = std::max(area.top, area.bottom - height - 18);
        MoveWindow(g_chat.edit, 14, 41, width - 110, 29, FALSE);
        MoveWindow(GetDlgItem(g_chat.composer, kComposerSend), width - 86, 40, 72, 30, FALSE);
        MoveWindow(g_chat.status, 14, 106, width - 28, 24, FALSE);
        SetWindowPos(g_chat.composer, HWND_TOP, left, top, width, height, SWP_SHOWWINDOW);
        SetForegroundWindow(g_chat.composer);
        SetFocus(g_chat.edit);
    }

    void TickChatOverlay(HWND gameWindow)
    {
        g_chat.game = gameWindow;
        const auto state = Lobby::GetClient().GetState();
        const auto matchId = state.match ? state.match->id : std::string{};
        if (g_chat.matchId != matchId)
        {
            if (!g_chat.matchId.empty())
            {
                g_chat.sticker.expires = g_chat.toast.expires = {};
                if (g_chat.edit) SetWindowTextW(g_chat.edit, L"");
                if (g_chat.status) SetWindowTextW(g_chat.status, L"Enter para enviar   \u00b7   Esc para volver");
                HideComposer(GetForegroundWindow() == g_chat.composer);
            }
            g_chat.matchId = matchId;
            g_chat.seen = 0;
            g_chat.lastError.clear();
        }
        for (const auto& message : state.messages)
        {
            if (message.id <= g_chat.seen) continue;
            g_chat.seen = message.id;
            if (const auto reaction = FindChatReaction(message.reaction))
                ShowNotice(g_chat.sticker, message.name, reaction->caption, reaction);
            else if (message.senderId != state.selfId && !message.text.empty())
                ShowNotice(g_chat.toast, message.name, message.text);
        }
        if (g_chat.lastError != state.chatError)
        {
            g_chat.lastError = state.chatError;
            if (!state.chatError.empty()) ShowNotice(g_chat.toast, {}, state.chatError);
        }
        if (auto failure = TakeRoomFailureNotice(); !failure.empty())
            ShowNotice(g_chat.toast, {}, std::move(failure));
        PositionNotice(g_chat.sticker);
        PositionNotice(g_chat.toast);
    }

    bool HandleChatComposerMessage(MSG& message)
    {
        const auto window = g_chat.composer;
        if (!window || !IsWindowVisible(window) ||
            (message.hwnd != window && !IsChild(window, message.hwnd))) return false;
        if (message.message == WM_KEYDOWN)
        {
            switch (message.wParam)
            {
            case VK_RETURN:
                if (message.hwnd != GetDlgItem(window, kComposerSend))
                {
                    if (!(message.lParam & (1L << 30))) SendComposer();
                    return true;
                }
                break;
            case VK_F2:
            case VK_ESCAPE:
                if (!(message.lParam & (1L << 30))) HideComposer(true);
                return true;
            }
        }
        return IsDialogMessageW(window, &message) != FALSE;
    }
}
