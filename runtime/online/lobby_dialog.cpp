#include "lobby_dialog.h"
#include "chat_reactions.h"
#include "lobby_client.h"
#include "player_profile.h"
#include "room_dialog.h"

#include <commctrl.h>

#include <algorithm>
#include <array>
#include <iterator>
#include <utility>
#include <vector>

namespace Online
{
    namespace
    {
        constexpr COLORREF kBackground = RGB(10, 10, 12);
        constexpr COLORREF kPanel = RGB(19, 19, 22);
        constexpr COLORREF kField = RGB(27, 27, 31);
        constexpr COLORREF kLine = RGB(58, 58, 65);
        constexpr COLORREF kText = RGB(244, 244, 242);
        constexpr COLORREF kMuted = RGB(174, 174, 180);
        constexpr COLORREF kRed = RGB(255, 76, 66);
        constexpr COLORREF kGreen = RGB(61, 255, 138);
        constexpr COLORREF kAmber = RGB(255, 194, 42);
        constexpr int kWidth = 796;
        constexpr int kHeight = 558;
        constexpr UINT_PTR kTimer = 1;
        constexpr wchar_t kWindowClass[] = L"PES13OnlineLobby";

        enum Control : int
        {
            Players = 201, PublicRooms, Invitations, Connect, OpenRoom, JoinRoom, Invite, Accept, Decline,
            Leave, Back, Status, Close, Nickname, Direct, ChatHistory, ChatEntry, ChatSend
        };

        struct Row
        {
            std::string id;
            std::wstring text;
            bool available = true;

            bool operator==(const Row&) const = default;
        };

        struct Listing
        {
            HWND window = nullptr;
            std::vector<Row> rows;
        };

        struct Dialog
        {
            HWND window = nullptr;
            HWND owner = nullptr;
            HWND status = nullptr;
            HWND nickname = nullptr;
            HWND chatHistory = nullptr;
            HWND chatEntry = nullptr;
            HFONT titleFont = nullptr;
            HFONT bodyFont = nullptr;
            HFONT smallFont = nullptr;
            HFONT headingFont = nullptr;
            HBRUSH fieldBrush = nullptr;
            HBRUSH panelBrush = nullptr;
            float scale = 1.0f;
            bool started = false;
            std::wstring validation;
            std::wstring chatError;
            std::wstring chatText;
            Lobby::State state;
            Listing players, rooms, invites;
        };

        Dialog g_dialog;

        int Px(int value)
        {
            return int(float(value) * g_dialog.scale + 0.5f);
        }

        RECT Rect(int x, int y, int w, int h)
        {
            return { Px(x), Px(y), Px(x + w), Px(y + h) };
        }

        void Fill(HDC dc, RECT rect, COLORREF color)
        {
            SetDCBrushColor(dc, color);
            FillRect(dc, &rect, static_cast<HBRUSH>(GetStockObject(DC_BRUSH)));
        }

        void Box(HDC dc, RECT rect, COLORREF color)
        {
            Fill(dc, rect, color);
            SetDCBrushColor(dc, kLine);
            FrameRect(dc, &rect, static_cast<HBRUSH>(GetStockObject(DC_BRUSH)));
        }

        void FocusBorder(HDC dc, RECT bounds, HWND control)
        {
            if (GetFocus() == control)
            {
                SetDCBrushColor(dc, kRed);
                FrameRect(dc, &bounds, static_cast<HBRUSH>(GetStockObject(DC_BRUSH)));
            }
        }

        void Label(HDC dc, std::wstring_view text, HFONT font, COLORREF color, RECT rect, UINT align = DT_LEFT)
        {
            const auto previous = SelectObject(dc, font);
            SetBkMode(dc, TRANSPARENT);
            SetTextColor(dc, color);
            DrawTextW(dc, text.data(), int(text.size()), &rect,
                align | DT_SINGLELINE | DT_VCENTER | DT_NOPREFIX | DT_END_ELLIPSIS);
            SelectObject(dc, previous);
        }

        void Stripe(HDC dc, int x, int width, COLORREF color)
        {
            POINT points[] = {
                { Px(x + 54), Px(3) }, { Px(x + width + 54), Px(3) },
                { Px(x + width), Px(120) }, { Px(x), Px(120) }
            };
            const auto brush = SelectObject(dc, GetStockObject(DC_BRUSH));
            const auto pen = SelectObject(dc, GetStockObject(NULL_PEN));
            SetDCBrushColor(dc, color);
            Polygon(dc, points, int(std::size(points)));
            SelectObject(dc, pen);
            SelectObject(dc, brush);
        }

        void Panel(HDC dc, int x, const wchar_t* title, size_t count, const wchar_t* empty)
        {
            Box(dc, Rect(x, 142, 238, 300), kPanel);
            Label(dc, title, g_dialog.headingFont, kText, Rect(x + 16, 150, 195, 31));
            const auto countText = std::to_wstring(count);
            Label(dc, countText, g_dialog.smallFont, kMuted, Rect(x + 202, 150, 21, 31), DT_RIGHT);
            if (!count)
                Label(dc, empty, g_dialog.smallFont, kMuted, Rect(x + 16, 222, 206, 34), DT_CENTER);
        }

        void Paint(HDC dc)
        {
            Fill(dc, Rect(0, 0, kWidth, kHeight), kBackground);
            Box(dc, Rect(0, 0, kWidth, 121), RGB(15, 15, 18));
            Stripe(dc, 493, 95, RGB(40, 15, 17));
            Stripe(dc, 588, 39, RGB(84, 23, 26));
            Stripe(dc, 627, 16, kRed);
            Fill(dc, Rect(0, 0, kWidth, 3), kRed);
            Label(dc, L"PES 2013 / MULTIJUGADOR", g_dialog.smallFont, kRed, Rect(26, 12, 410, 23));
            Label(dc, L"LOBBY", g_dialog.titleFont, kText, Rect(24, 30, 520, 51));
            Label(dc, L"Tu apodo", g_dialog.bodyFont, kMuted, Rect(27, 82, 80, 28));
            Box(dc, Rect(108, 82, 219, 28), kField);
            FocusBorder(dc, Rect(108, 82, 219, 28), g_dialog.nickname);

            const auto& state = g_dialog.state;
            const wchar_t* connection = state.connected ? L"Conectado" : state.busy ? L"Conectando\u2026" : L"Desconectado";
            const auto connectionColor = state.connected ? kGreen : state.busy ? kAmber : kMuted;
            Label(dc, L"RED LOCAL", g_dialog.smallFont, kMuted, Rect(372, 79, 112, 27));
            Fill(dc, Rect(654, 89, 8, 8), connectionColor);
            Label(dc, connection, g_dialog.smallFont, connectionColor, Rect(671, 79, 103, 27));

            if (state.match)
            {
                Box(dc, Rect(24, 142, 748, 300), kPanel);
                Label(dc, L"Chat de sala", g_dialog.headingFont, kText, Rect(40, 151, 380, 31));
                Label(dc, L"F2 \u00b7 Escribir", g_dialog.smallFont, kMuted, Rect(557, 153, 199, 27), DT_RIGHT);
                Box(dc, Rect(40, 190, 716, 151), kField);
                FocusBorder(dc, Rect(40, 190, 716, 151), g_dialog.chatHistory);
                Box(dc, Rect(40, 355, 600, 38), kField);
                FocusBorder(dc, Rect(40, 355, 600, 38), g_dialog.chatEntry);
                Label(dc, L"Enter \u00b7 Enviar", g_dialog.smallFont, kMuted, Rect(40, 404, 116, 22));
                Label(dc, kChatCommandHint, g_dialog.smallFont, kMuted, Rect(156, 404, 600, 22));
            }
            else
            {
                Panel(dc, 24, L"Jugadores", g_dialog.players.rows.size(), L"No hay otros jugadores");
                Panel(dc, 279, L"Salas p\u00fablicas", g_dialog.rooms.rows.size(), L"No hay salas abiertas");
                Panel(dc, 534, L"Invitaciones", g_dialog.invites.rows.size(), L"No ten\u00e9s invitaciones");
            }

            Box(dc, Rect(24, 457, 748, 36), kPanel);
            Fill(dc, Rect(39, 470, 8, 8), connectionColor);
            Label(dc, L"F1 / Esc \u00b7 Volver", g_dialog.smallFont, kMuted, Rect(25, 510, 126, 32));
        }

        void PaintButton(const DRAWITEMSTRUCT& item)
        {
            wchar_t text[96]{};
            GetWindowTextW(item.hwndItem, text, int(std::size(text)));
            const bool disabled = (item.itemState & ODS_DISABLED) != 0;
            const bool focused = (item.itemState & ODS_FOCUS) != 0;
            const bool pressed = (item.itemState & ODS_SELECTED) != 0;
            const bool primary = !disabled && (item.CtlID == OpenRoom || item.CtlID == JoinRoom ||
                item.CtlID == Invite || item.CtlID == Accept || item.CtlID == ChatSend ||
                (item.CtlID == Connect && !g_dialog.state.connected));
            const COLORREF background = disabled ? kPanel : pressed ? kRed : primary ? kText : kField;
            const COLORREF foreground = disabled ? kMuted : pressed || primary ? kBackground : kText;
            Fill(item.hDC, item.rcItem, background);
            SetDCBrushColor(item.hDC, focused ? kRed : kLine);
            FrameRect(item.hDC, &item.rcItem, static_cast<HBRUSH>(GetStockObject(DC_BRUSH)));
            Label(item.hDC, text, g_dialog.bodyFont, foreground, item.rcItem, DT_CENTER);
            if (focused)
            {
                RECT focus = item.rcItem;
                InflateRect(&focus, -Px(5), -Px(5));
                DrawFocusRect(item.hDC, &focus);
            }
        }

        void SetLabel(HWND window, std::wstring_view value)
        {
            wchar_t existing[256]{};
            GetWindowTextW(window, existing, int(std::size(existing)));
            if (value != existing)
                SetWindowTextW(window, std::wstring(value).c_str());
        }

        const Row* Selection(const Listing& list)
        {
            const auto selected = SendMessageW(list.window, LB_GETCURSEL, 0, 0);
            return selected != LB_ERR && size_t(selected) < list.rows.size() ? &list.rows[size_t(selected)] : nullptr;
        }

        bool ReplaceRows(Listing& list, std::vector<Row> rows, bool visible)
        {
            const bool changed = list.rows != rows;
            if (changed)
            {
                const Row* selection = Selection(list);
                const auto selected = selection ? selection->id : std::string{};
                SendMessageW(list.window, WM_SETREDRAW, FALSE, 0);
                SendMessageW(list.window, LB_RESETCONTENT, 0, 0);
                size_t selectIndex = 0;
                for (size_t i = 0; i < rows.size(); ++i)
                {
                    SendMessageW(list.window, LB_ADDSTRING, 0, LPARAM(rows[i].text.c_str()));
                    if (rows[i].id == selected) selectIndex = i;
                }
                list.rows = std::move(rows);
                if (!list.rows.empty())
                    SendMessageW(list.window, LB_SETCURSEL, selectIndex, 0);
                SendMessageW(list.window, WM_SETREDRAW, TRUE, 0);
                InvalidateRect(list.window, nullptr, TRUE);
            }
            const bool show = visible && !list.rows.empty();
            if (bool(GetWindowLongPtrW(list.window, GWL_STYLE) & WS_VISIBLE) != show)
                ShowWindow(list.window, show ? SW_SHOWNA : SW_HIDE);
            return changed;
        }

        std::wstring PlayerStatus(std::string_view status)
        {
            if (status == "available" || status == "idle" || status == "online") return L"Disponible";
            if (status == "hosting" || status == "waiting" || status == "in_room") return L"En sala";
            return L"Ocupado";
        }

        void UpdateButtons()
        {
            const auto& state = g_dialog.state;
            const bool connected = state.connected && !state.busy;
            const bool available = connected && !state.match;
            const auto* player = Selection(g_dialog.players);
            const auto* room = Selection(g_dialog.rooms);
            const auto* invite = Selection(g_dialog.invites);
            const auto enable = [](Control id, bool enabled) {
                const HWND control = GetDlgItem(g_dialog.window, id);
                if (bool(IsWindowEnabled(control)) != enabled) EnableWindow(control, enabled);
            };
            SetLabel(GetDlgItem(g_dialog.window, Connect), state.connected ? L"Desconectarme" : L"Conectarme");
            enable(Connect, !state.busy);
            enable(OpenRoom, available && state.ownRoomId.empty());
            enable(JoinRoom, available && room && room->id != state.ownRoomId);
            enable(Invite, available && player && player->available);
            enable(Accept, available && invite);
            enable(Decline, connected && invite);
            enable(Leave, connected && (!state.ownRoomId.empty() || state.match));
            enable(ChatSend, state.match && GetWindowTextLengthW(g_dialog.chatEntry) != 0);
            SendMessageW(g_dialog.nickname, EM_SETREADONLY, state.connected || state.busy, 0);
        }

        void UpdateChat(const Lobby::State& state, bool newRoom)
        {
            if (newRoom)
            {
                g_dialog.chatError.clear();
                SetWindowTextW(g_dialog.chatEntry, L"");
            }
            std::wstring transcript;
            if (state.match)
            {
                for (const auto& message : state.messages)
                {
                    if (!transcript.empty()) transcript += L"\r\n";
                    transcript += message.name;
                    transcript += L": ";
                    const auto* reaction = FindChatReaction(message.reaction);
                    transcript += reaction ? reaction->caption : std::wstring_view(message.text);
                }
            }
            if (!newRoom && g_dialog.chatText == transcript) return;

            const auto history = g_dialog.chatHistory;
            const auto firstLine = SendMessageW(history, EM_GETFIRSTVISIBLELINE, 0, 0);
            SCROLLINFO scroll{};
            scroll.cbSize = sizeof(scroll);
            scroll.fMask = SIF_RANGE | SIF_PAGE | SIF_POS;
            const bool follow = newRoom || !GetScrollInfo(history, SB_VERT, &scroll) ||
                scroll.nPos >= scroll.nMax - std::max(int(scroll.nPage) - 1, 0);
            g_dialog.chatText = std::move(transcript);
            const wchar_t* display = g_dialog.chatText.empty() ? L"Todav\u00eda no hay mensajes." : g_dialog.chatText.c_str();
            SetWindowTextW(history, display);
            if (follow)
            {
                const auto end = GetWindowTextLengthW(history);
                SendMessageW(history, EM_SETSEL, end, end);
                SendMessageW(history, EM_SCROLLCARET, 0, 0);
            }
            else
                SendMessageW(history, EM_LINESCROLL, 0, firstLine);
        }

        void Refresh()
        {
            auto state = Lobby::GetClient().GetState();
            bool changed = state.connected != g_dialog.state.connected || state.busy != g_dialog.state.busy;
            const bool paired = state.match.has_value();
            const bool wasPaired = g_dialog.state.match.has_value();
            const bool newRoom = paired != wasPaired || (paired && state.match->id != g_dialog.state.match->id);
            const HWND focused = GetFocus();
            std::vector<Row> players, rooms, invites;
            for (const auto& player : state.players)
            {
                auto label = PlayerStatus(player.status);
                const bool available = label == L"Disponible";
                players.push_back({ player.id, player.name + L" \u00b7 " + label, available });
            }
            for (const auto& room : state.rooms)
                rooms.push_back({ room.id, room.hostName });
            if (!state.ownRoomId.empty())
                rooms.insert(rooms.begin(), { state.ownRoomId, L"Tu sala \u00b7 Esperando rival" });
            for (const auto& invite : state.invitations)
                invites.push_back({ invite.id, invite.fromName });
            changed |= ReplaceRows(g_dialog.players, std::move(players), !paired);
            changed |= ReplaceRows(g_dialog.rooms, std::move(rooms), !paired);
            changed |= ReplaceRows(g_dialog.invites, std::move(invites), !paired);
            if (paired != wasPaired)
            {
                constexpr Control lobbyControls[] = { Invite, JoinRoom, OpenRoom, Accept, Decline };
                constexpr Control chatControls[] = { ChatHistory, ChatEntry, ChatSend };
                for (auto id : lobbyControls)
                    ShowWindow(GetDlgItem(g_dialog.window, id), paired ? SW_HIDE : SW_SHOWNA);
                for (auto id : chatControls)
                    ShowWindow(GetDlgItem(g_dialog.window, id), paired ? SW_SHOWNA : SW_HIDE);
                if (IsWindowVisible(g_dialog.window) && IsChild(g_dialog.window, focused) && !IsWindowVisible(focused))
                    SetFocus(paired ? g_dialog.chatEntry : GetDlgItem(g_dialog.window, Back));
            }
            UpdateChat(state, newRoom);
            std::wstring status = g_dialog.validation.empty() ? state.message : g_dialog.validation;
            if (paired && !g_dialog.chatError.empty())
                status = g_dialog.chatError;
            else if (paired && !state.chatError.empty())
                status = state.chatError;
            else if (paired)
                status = L"Rival confirmado: " + state.match->peerName + L" \u00b7 El partido online sigue en desarrollo.";
            else if (status.empty())
                status = state.connected ? L"Eleg\u00ed una sala p\u00fablica o invit\u00e1 a un jugador."
                    : state.busy ? L"Conectando al lobby\u2026" : L"Eleg\u00ed un apodo para conectarte.";
            SetLabel(g_dialog.status, status);
            g_dialog.state = std::move(state);
            UpdateButtons();
            if (changed || newRoom) InvalidateRect(g_dialog.window, nullptr, FALSE);
        }

        void Hide()
        {
            ShowWindow(g_dialog.window, SW_HIDE);
            if (g_dialog.owner && IsWindowVisible(g_dialog.owner))
            {
                SetForegroundWindow(g_dialog.owner);
                SetFocus(g_dialog.owner);
            }
        }

        void ConnectWithNickname()
        {
            wchar_t input[kMaxPlayerNameLength + 1]{};
            GetWindowTextW(g_dialog.nickname, input, int(std::size(input)));
            const auto name = NormalizePlayerName(input);
            if (name.empty())
                g_dialog.validation = L"Escrib\u00ed un apodo v\u00e1lido de 2 a 15 caracteres.";
            else if (!SavePlayerName(name))
                g_dialog.validation = L"No se pudo guardar el apodo.";
            else
            {
                g_dialog.validation.clear();
                SetLabel(g_dialog.nickname, name);
                Lobby::GetClient().Request(Lobby::Action::Connect, {}, name);
                return;
            }
            SetFocus(g_dialog.nickname);
        }

        void SendChat()
        {
            if (!g_dialog.state.match) return;
            wchar_t input[kChatMaxTextUnits + 1]{};
            GetWindowTextW(g_dialog.chatEntry, input, int(std::size(input)));
            if (*input && Lobby::SubmitChat(input, g_dialog.chatError))
                SetWindowTextW(g_dialog.chatEntry, L"");
            SetFocus(g_dialog.chatEntry);
        }

        void SelectAction(Control id)
        {
            using Lobby::Action;
            auto& client = Lobby::GetClient();
            switch (id)
            {
            case Connect:
                if (g_dialog.state.connected)
                    client.Request(Action::Disconnect);
                else
                    ConnectWithNickname();
                break;
            case OpenRoom:
                client.Request(Action::OpenRoom);
                break;
            case JoinRoom:
                if (const auto* selected = Selection(g_dialog.rooms))
                    client.Request(Action::JoinRoom, selected->id);
                break;
            case Invite:
                if (const auto* selected = Selection(g_dialog.players))
                    client.Request(Action::Invite, selected->id);
                break;
            case Accept:
            case Decline:
                if (const auto* selected = Selection(g_dialog.invites))
                    client.Request(id == Accept ? Action::Accept : Action::Decline, selected->id);
                break;
            case Leave:
                client.Request(Action::Leave);
                break;
            case Back:
            case Close:
                Hide();
                break;
            case Direct:
                Hide();
                ToggleRoomDialog(g_dialog.owner);
                break;
            case ChatSend:
                SendChat();
                break;
            default:
                break;
            }
        }

        LRESULT CALLBACK LobbyWindowProc(HWND window, UINT message, WPARAM wParam, LPARAM lParam)
        {
            switch (message)
            {
            case WM_PAINT:
            {
                PAINTSTRUCT paint{};
                Paint(BeginPaint(window, &paint));
                EndPaint(window, &paint);
                return 0;
            }
            case WM_PRINTCLIENT:
                Paint(reinterpret_cast<HDC>(wParam));
                return 0;
            case WM_ERASEBKGND:
                return TRUE;
            case WM_DRAWITEM:
                if (const auto* item = reinterpret_cast<DRAWITEMSTRUCT*>(lParam); item && item->CtlType == ODT_BUTTON)
                {
                    PaintButton(*item);
                    return TRUE;
                }
                break;
            case WM_CTLCOLORLISTBOX:
            case WM_CTLCOLOREDIT:
            case WM_CTLCOLORSTATIC:
            {
                const auto dc = reinterpret_cast<HDC>(wParam);
                const auto control = reinterpret_cast<HWND>(lParam);
                const bool field = message == WM_CTLCOLORLISTBOX || control == g_dialog.nickname ||
                    control == g_dialog.chatHistory || control == g_dialog.chatEntry;
                SetTextColor(dc, kText);
                SetBkColor(dc, field ? kField : kPanel);
                return reinterpret_cast<LRESULT>(field ? g_dialog.fieldBrush : g_dialog.panelBrush);
            }
            case WM_NCHITTEST:
            {
                const LRESULT hit = DefWindowProcW(window, message, wParam, lParam);
                if (hit != HTCLIENT) return hit;
                POINT cursor{ short(LOWORD(lParam)), short(HIWORD(lParam)) };
                ScreenToClient(window, &cursor);
                if (cursor.y < Px(78) && cursor.x < Px(718)) return HTCAPTION;
                return HTCLIENT;
            }
            case WM_TIMER:
                if (wParam == kTimer && IsWindowVisible(window))
                    Refresh();
                return 0;
            case WM_COMMAND:
            {
                const auto id = Control(LOWORD(wParam));
                if (id == Nickname)
                {
                    if (HIWORD(wParam) == EN_CHANGE && !g_dialog.validation.empty())
                    {
                        g_dialog.validation.clear();
                        Refresh();
                    }
                    if (HIWORD(wParam) == EN_SETFOCUS || HIWORD(wParam) == EN_KILLFOCUS)
                    {
                        const auto bounds = Rect(108, 82, 219, 28);
                        InvalidateRect(window, &bounds, FALSE);
                    }
                }
                else if (id == ChatEntry || id == ChatHistory)
                {
                    if (HIWORD(wParam) == EN_CHANGE && id == ChatEntry)
                    {
                        UpdateButtons();
                        if (!g_dialog.chatError.empty())
                        {
                            g_dialog.chatError.clear();
                            Refresh();
                        }
                    }
                    if (HIWORD(wParam) == EN_SETFOCUS || HIWORD(wParam) == EN_KILLFOCUS)
                    {
                        const auto bounds = id == ChatEntry ? Rect(40, 355, 600, 38) : Rect(40, 190, 716, 151);
                        InvalidateRect(window, &bounds, FALSE);
                    }
                }
                else if (HIWORD(wParam) == LBN_SELCHANGE)
                    UpdateButtons();
                else if (HIWORD(wParam) == BN_CLICKED && (id == Direct || id == ChatSend || (id >= Connect && id <= Close)) &&
                    IsWindowEnabled(GetDlgItem(window, id)))
                {
                    SelectAction(id);
                    Refresh();
                }
                return 0;
            }
            case WM_CLOSE:
                Hide();
                return 0;
            case WM_DESTROY:
                KillTimer(window, kTimer);
                for (auto font : { g_dialog.titleFont, g_dialog.bodyFont, g_dialog.smallFont, g_dialog.headingFont })
                    DeleteObject(font);
                DeleteObject(g_dialog.fieldBrush);
                DeleteObject(g_dialog.panelBrush);
                g_dialog = {};
                return 0;
            }
            return DefWindowProcW(window, message, wParam, lParam);
        }

        HWND CreateLobby(HWND owner)
        {
            const auto instance = GetModuleHandleW(nullptr);
            WNDCLASSW windowClass{};
            windowClass.lpfnWndProc = LobbyWindowProc;
            windowClass.hInstance = instance;
            windowClass.hCursor = LoadCursorW(nullptr, MAKEINTRESOURCEW(32512));
            windowClass.lpszClassName = kWindowClass;
            if (!RegisterClassW(&windowClass) && GetLastError() != ERROR_CLASS_ALREADY_EXISTS)
                return nullptr;

            RECT ownerRect{};
            GetWindowRect(owner, &ownerRect);
            MONITORINFO monitor{};
            monitor.cbSize = sizeof(monitor);
            if (!GetMonitorInfoW(MonitorFromWindow(owner, MONITOR_DEFAULTTONEAREST), &monitor))
                SystemParametersInfoW(SPI_GETWORKAREA, 0, &monitor.rcWork, 0);
            const auto& work = monitor.rcWork;
            using DpiProc = UINT(WINAPI*)(HWND);
            const auto dpi = reinterpret_cast<DpiProc>(GetProcAddress(GetModuleHandleW(L"user32.dll"), "GetDpiForWindow"));
            g_dialog.scale = std::min({ dpi ? float(dpi(owner)) / 96.0f : 1.0f,
                float(work.right - work.left - 20) / kWidth, float(work.bottom - work.top - 20) / kHeight });
            if (g_dialog.scale <= 0) g_dialog.scale = 1.0f;
            const int w = Px(kWidth), h = Px(kHeight);
            const int x = std::clamp(int((ownerRect.left + ownerRect.right - w) / 2), int(work.left), std::max(int(work.left), int(work.right - w)));
            const int y = std::clamp(int((ownerRect.top + ownerRect.bottom - h) / 2), int(work.top), std::max(int(work.top), int(work.bottom - h)));
            HWND window = CreateWindowExW(WS_EX_TOOLWINDOW | WS_EX_CONTROLPARENT, kWindowClass, L"PES 2013 \u00b7 Lobby",
                WS_POPUP | WS_CLIPCHILDREN, x, y, w, h, owner, nullptr, instance, nullptr);
            if (!window) return nullptr;

            const auto font = [](const wchar_t* face, int height, int weight) {
                return CreateFontW(-Px(height), 0, 0, 0, weight, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
                    OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH, face);
            };
            g_dialog.window = window;
            g_dialog.owner = owner;
            g_dialog.titleFont = font(L"Bahnschrift Condensed", 45, FW_BOLD);
            g_dialog.bodyFont = font(L"Segoe UI", 15, FW_SEMIBOLD);
            g_dialog.smallFont = font(L"Segoe UI", 13, FW_SEMIBOLD);
            g_dialog.headingFont = font(L"Segoe UI", 21, FW_SEMIBOLD);
            g_dialog.fieldBrush = CreateSolidBrush(kField);
            g_dialog.panelBrush = CreateSolidBrush(kPanel);
            const auto control = [&](const wchar_t* cls, const wchar_t* text, DWORD flags, RECT r, Control id) {
                const auto hwnd = CreateWindowExW(0, cls, text, WS_CHILD | flags,
                    r.left, r.top, r.right - r.left, r.bottom - r.top, window, HMENU(INT_PTR(id)), instance, nullptr);
                SendMessageW(hwnd, WM_SETFONT, WPARAM(g_dialog.bodyFont), FALSE);
                return hwnd;
            };
            const auto list = [&](int x, const wchar_t* label, Control id) {
                return control(L"LISTBOX", label, WS_TABSTOP | WS_VSCROLL | LBS_NOTIFY | LBS_NOINTEGRALHEIGHT, Rect(x, 188, 206, 139), id);
            };
            const auto button = [&](const wchar_t* name, int x, int y, int w, int h, Control id) {
                return control(L"BUTTON", name, WS_VISIBLE | WS_TABSTOP | BS_OWNERDRAW, Rect(x, y, w, h), id);
            };
            g_dialog.nickname = control(L"EDIT", L"", WS_VISIBLE | WS_TABSTOP | ES_AUTOHSCROLL,
                Rect(110, 84, 215, 24), Nickname);
            SendMessageW(g_dialog.nickname, EM_LIMITTEXT, kMaxPlayerNameLength, 0);
            SendMessageW(g_dialog.nickname, EM_SETMARGINS, EC_LEFTMARGIN | EC_RIGHTMARGIN,
                MAKELPARAM(Px(7), Px(7)));
            SendMessageW(g_dialog.nickname, EM_SETCUEBANNER, FALSE, LPARAM(L"2 a 15 caracteres"));
            g_dialog.players.window = list(40, L"Jugadores", Players);
            button(L"Invitar", 40, 339, 206, 36, Invite);
            g_dialog.rooms.window = list(295, L"Salas p\u00fablicas", PublicRooms);
            button(L"Unirme", 295, 339, 206, 36, JoinRoom);
            button(L"Crear sala p\u00fablica", 295, 385, 206, 36, OpenRoom);
            g_dialog.invites.window = list(550, L"Invitaciones", Invitations);
            button(L"Aceptar", 550, 339, 206, 36, Accept);
            button(L"Rechazar", 550, 385, 206, 36, Decline);
            g_dialog.chatHistory = control(L"EDIT", L"", WS_TABSTOP | WS_VSCROLL | ES_MULTILINE | ES_AUTOVSCROLL | ES_READONLY,
                Rect(47, 196, 700, 138), ChatHistory);
            g_dialog.chatEntry = control(L"EDIT", L"", WS_TABSTOP | ES_AUTOHSCROLL, Rect(42, 362, 596, 24), ChatEntry);
            SendMessageW(g_dialog.chatEntry, EM_LIMITTEXT, kChatMaxTextUnits, 0);
            SendMessageW(g_dialog.chatEntry, EM_SETMARGINS, EC_LEFTMARGIN | EC_RIGHTMARGIN, MAKELPARAM(Px(7), Px(7)));
            SendMessageW(g_dialog.chatEntry, EM_SETCUEBANNER, FALSE, LPARAM(L"Escrib\u00ed un mensaje o un comando"));
            control(L"BUTTON", L"Enviar", WS_TABSTOP | BS_OWNERDRAW, Rect(652, 355, 104, 38), ChatSend);
            g_dialog.status = control(L"STATIC", L"", WS_VISIBLE | SS_LEFT | SS_CENTERIMAGE | SS_NOPREFIX,
                Rect(58, 460, 702, 30), Status);
            button(L"Conexi\u00f3n directa", 155, 506, 150, 36, Direct);
            button(L"Salir de la sala", 317, 506, 144, 36, Leave);
            button(L"Conectarme", 473, 506, 144, 36, Connect);
            button(L"Volver", 629, 506, 143, 36, Back);
            button(L"Cerrar", 718, 19, 54, 33, Close);
            SetTimer(window, kTimer, 150, nullptr);
            return window;
        }
    }

    void ToggleLobbyDialog(HWND owner, std::wstring name)
    {
        if (g_dialog.window && IsWindowVisible(g_dialog.window))
        {
            Hide();
            return;
        }
        if (!g_dialog.window && !CreateLobby(owner))
            return;
        if (!g_dialog.started)
        {
            g_dialog.started = true;
            const auto nickname = name.empty() ? LoadPlayerName() : NormalizePlayerName(name);
            SetLabel(g_dialog.nickname, nickname);
            if (!nickname.empty())
                Lobby::GetClient().Request(Lobby::Action::Connect, {}, nickname);
        }
        Refresh();
        ShowWindow(g_dialog.window, SW_SHOWNORMAL);
        SetForegroundWindow(g_dialog.window);
        const HWND back = GetDlgItem(g_dialog.window, Back);
        SetFocus(GetWindowTextLengthW(g_dialog.nickname) == 0 ? g_dialog.nickname : back);
    }

    bool HandleLobbyDialogMessage(MSG& message)
    {
        const HWND window = g_dialog.window;
        if (!window || !IsWindowVisible(window) || (message.hwnd != window && !IsChild(window, message.hwnd)))
            return false;
        if (message.message == WM_KEYDOWN && (message.wParam == VK_F1 || message.wParam == VK_ESCAPE))
        {
            if (!(message.lParam & (1L << 30))) Hide();
            return true;
        }
        if (message.message == WM_KEYDOWN && message.wParam == VK_F2 && g_dialog.state.match)
        {
            SetFocus(g_dialog.chatEntry);
            return true;
        }
        if (message.message == WM_KEYDOWN && message.wParam == VK_RETURN && message.hwnd == g_dialog.chatEntry)
        {
            if (!(message.lParam & (1L << 30)))
            {
                SendChat();
                Refresh();
            }
            return true;
        }
        if (message.message == WM_KEYDOWN && message.wParam == VK_RETURN && message.hwnd == g_dialog.nickname &&
            !g_dialog.state.connected && !g_dialog.state.busy)
        {
            ConnectWithNickname();
            Refresh();
            return true;
        }
        return IsDialogMessageW(window, &message) != FALSE;
    }
}
