#include "room_dialog.h"
#include "lobby_dialog.h"
#include "room.h"
#include "player_profile.h"

#include <algorithm>
#include <charconv>
#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <iterator>
#include <mutex>
#include <optional>
#include <stop_token>
#include <string>
#include <thread>

namespace Online
{
    namespace
    {
        constexpr uint16_t kExperimentalMatchPort = 28513;

        bool ExperimentalSync()
        {
            static const bool enabled = std::getenv("PES_EXPERIMENTAL_SYNC") != nullptr;
            return enabled;
        }

        enum class Phase { Idle, Hosting, Joining, Connected, Error };

        struct Status
        {
            Phase phase = Phase::Idle;
            std::wstring message;
            std::wstring code;
            bool host = false;
            std::optional<std::chrono::microseconds> ping;
            std::wstring localName;
            std::wstring remoteName;
        };

        struct Session
        {
            std::mutex mutex;
            Status status;
            std::shared_ptr<Room> room;
            std::stop_source stop;
            uint64_t revision = 0;
        };

        Session g_session;

        std::wstring Wide(std::string_view text)
        {
            return { text.begin(), text.end() };
        }

        void Update(uint64_t revision, Status status, std::shared_ptr<Room> room = {})
        {
            std::lock_guard lock(g_session.mutex);
            if (g_session.revision != revision)
                return;
            g_session.status = std::move(status);
            g_session.room = std::move(room);
        }

        void StopRoom()
        {
            std::lock_guard lock(g_session.mutex);
            ++g_session.revision;
            g_session.stop.request_stop();
            g_session.room.reset();
            g_session.status = { Phase::Idle, L"Sala cerrada.", {} };
        }

        struct Destination
        {
            std::string address;
            uint16_t port = 0;
            std::string token;
        };

        bool ParseCode(std::string_view code, Destination& destination)
        {
            const size_t colon = code.find(':');
            const size_t slash = code.find('/');
            if (colon == std::string_view::npos || slash == std::string_view::npos || colon == 0 || slash <= colon + 1)
                return false;

            unsigned port = 0;
            const char* start = code.data() + colon + 1;
            const char* end = code.data() + slash;
            const auto decimal = std::from_chars(start, end, port);
            const std::string_view token = code.substr(slash + 1);
            if (decimal.ec != std::errc{} || decimal.ptr != end || port == 0 || port > 65535 ||
                token.size() != 32 || token.find_first_not_of("0123456789abcdefABCDEF") != std::string_view::npos)
                return false;

            destination = { std::string(code.substr(0, colon)), uint16_t(port), std::string(token) };
            return true;
        }

        void StartRoom(bool hosting, Destination destination, std::wstring name)
        {
            uint64_t revision;
            std::stop_token stop;
            {
                std::lock_guard lock(g_session.mutex);
                if (g_session.status.phase != Phase::Idle && g_session.status.phase != Phase::Error)
                    return;
                revision = ++g_session.revision;
                g_session.stop = std::stop_source{};
                stop = g_session.stop.get_token();
                g_session.status = hosting
                    ? Status{ Phase::Hosting, L"Creando sala\u2026", {}, true }
                    : Status{ Phase::Joining, L"Conectando con el anfitri\u00f3n\u2026", {} };
                g_session.status.localName = name;
            }

            try
            {
                std::thread([hosting, destination = std::move(destination), name = std::move(name), revision, stop] {
                    const bool experimental = ExperimentalSync();
                    auto room = std::make_shared<Room>(experimental ? Room::Mode::Input : Room::Mode::Latency);
                    if (!room->SetLocalName(PlayerNameUtf8(name)))
                    {
                        Update(revision, { Phase::Error, L"El apodo no es v\u00e1lido.", {} });
                        return;
                    }
                    std::wstring code;
                    if (hosting)
                    {
                        if (!room->Host(destination.address, experimental ? kExperimentalMatchPort : destination.port))
                        {
                            Update(revision, { Phase::Error, Wide(room->Error()), {} });
                            return;
                        }
                        code = Wide(destination.address + ":" + std::to_string(room->Port()) + "/" + room->Token());
                        Status waiting{ Phase::Hosting, L"Esperando a tu rival\u2026", code, true };
                        waiting.localName = name;
                        Update(revision, std::move(waiting));
                    }

                    const bool connected = hosting
                        ? room->WaitForPeer(stop)
                        : room->Join(destination.address, destination.port, destination.token, stop);
                    if (!connected)
                    {
                        Update(revision, { Phase::Error, Wide(room->Error()), {} });
                        return;
                    }

                    Status joined{ Phase::Connected,
                        experimental ? L"Conectados. Vuelvan al juego y sigan desde la previa."
                                     : L"Conexi\u00f3n de prueba establecida. El partido online sigue en desarrollo.",
                        code, hosting };
                    joined.localName = name;
                    joined.remoteName = PlayerNameFromUtf8(room->PeerName());
                    Update(revision, std::move(joined), room);
                    if (experimental) return;

                    std::chrono::microseconds sample;
                    while (!stop.stop_requested() && room->MeasureLatency(sample, stop))
                    {
                        std::lock_guard lock(g_session.mutex);
                        if (g_session.revision != revision || g_session.room != room) return;
                        g_session.status.ping = sample;
                    }
                    if (!stop.stop_requested())
                        ReportRoomFailure(room, {});
                }).detach();
            }
            catch (const std::system_error&)
            {
                Update(revision, { Phase::Error, L"No se pudo iniciar la conexi\u00f3n.", {} });
            }
        }

        constexpr int kCreate = 100;
        constexpr int kJoin = 101;
        constexpr int kCopy = 102;
        constexpr int kStop = 103;
        constexpr int kStatus = 104;
        constexpr int kPing = 105;
        constexpr int kClose = 106;
        constexpr int kPlayerName = 107;
        constexpr int kSaveName = 108;
        constexpr int kLocalNameLabel = 109;
        constexpr int kRemoteNameLabel = 110;
        constexpr UINT_PTR kRefreshTimer = 1;

        constexpr COLORREF kBackground = RGB(10, 10, 12);
        constexpr COLORREF kSurface = RGB(19, 19, 22);
        constexpr COLORREF kField = RGB(30, 30, 35);
        constexpr COLORREF kLine = RGB(47, 47, 53);
        constexpr COLORREF kText = RGB(244, 244, 242);
        constexpr COLORREF kMuted = RGB(174, 174, 180);
        constexpr COLORREF kRed = RGB(255, 76, 66);
        constexpr COLORREF kGreen = RGB(61, 255, 138);
        constexpr COLORREF kAmber = RGB(255, 194, 42);
        constexpr int kWidth = 748, kHeight = 576;

        struct Dialog
        {
            HWND window = nullptr;
            HWND hostAddress = nullptr;
            HWND joinCode = nullptr;
            HWND shareCode = nullptr;
            HWND status = nullptr;
            HWND create = nullptr;
            HWND join = nullptr;
            HWND copy = nullptr;
            HWND stop = nullptr;
            HWND ping = nullptr;
            HWND back = nullptr;
            HWND close = nullptr;
            HWND playerName = nullptr;
            HWND saveName = nullptr;
            HWND localNameLabel = nullptr;
            HWND remoteNameLabel = nullptr;
            float scale = 1.0f;
            HFONT titleFont = nullptr;
            HFONT sectionFont = nullptr;
            HFONT bodyFont = nullptr;
            HFONT smallFont = nullptr;
            HFONT codeFont = nullptr;
            HFONT invitationFont = nullptr;
            HFONT pingFont = nullptr;
            Phase phase = Phase::Idle;
            bool host = false;
            std::wstring localName;
            std::wstring remoteName;
            int signalBars = 0;
            COLORREF signalColor = kGreen;
            HBRUSH fieldBrush = nullptr;
            HBRUSH surfaceBrush = nullptr;
        };

        Dialog g_dialog;

        int Px(int logical)
        {
            return int(float(logical) * g_dialog.scale + 0.5f);
        }

        RECT Bounds(int x, int y, int w, int h)
        {
            return { Px(x), Px(y), Px(x + w), Px(y + h) };
        }

        void Fill(HDC dc, RECT rect, COLORREF color)
        {
            SetDCBrushColor(dc, color);
            FillRect(dc, &rect, static_cast<HBRUSH>(GetStockObject(DC_BRUSH)));
        }

        void Box(HDC dc, int x, int y, int w, int h, COLORREF color)
        {
            const auto r = Bounds(x, y, w, h);
            Fill(dc, r, color);
            SetDCBrushColor(dc, kLine);
            FrameRect(dc, &r, static_cast<HBRUSH>(GetStockObject(DC_BRUSH)));
        }

        void Label(HDC dc, const wchar_t* text, HFONT font, COLORREF color, RECT bounds, UINT flags = DT_LEFT)
        {
            const auto previous = SelectObject(dc, font);
            SetBkMode(dc, TRANSPARENT);
            SetTextColor(dc, color);
            DrawTextW(dc, text, -1, &bounds, flags | DT_SINGLELINE | DT_VCENTER | DT_NOPREFIX | DT_END_ELLIPSIS);
            SelectObject(dc, previous);
        }

        void Slash(HDC dc, int x, int y, int w, int h, COLORREF color)
        {
            POINT points[] = { {Px(x + w / 2), Px(y)}, {Px(x + w), Px(y)},
                {Px(x + w / 2), Px(y + h)}, {Px(x), Px(y + h)} };
            const auto oldPen = SelectObject(dc, GetStockObject(NULL_PEN));
            const auto oldBrush = SelectObject(dc, GetStockObject(DC_BRUSH));
            SetDCBrushColor(dc, color);
            Polygon(dc, points, 4);
            SelectObject(dc, oldBrush);
            SelectObject(dc, oldPen);
        }

        bool InRoom(Phase phase)
        {
            return phase == Phase::Hosting || phase == Phase::Joining || phase == Phase::Connected;
        }

        void DrawPlayer(HDC dc, int x, const wchar_t* role, bool active)
        {
            Box(dc, x, 173, 337, 94, kSurface);
            Fill(dc, Bounds(x + 16, 191, 44, 58), kField);
            Slash(dc, x + 25, 202, 26, 35, active ? kRed : kLine);
            Label(dc, role, g_dialog.smallFont, kMuted, Bounds(x + 75, 193, 235, 19));
        }

        void DrawPanel(HDC dc)
        {
            const bool experimental = ExperimentalSync();
            Fill(dc, Bounds(0, 0, kWidth, kHeight), kBackground);
            Box(dc, 0, 0, kWidth, 150, RGB(15, 15, 18));
            // OpenSmash's angular red and white cuts, kept clear of the controls.
            Slash(dc, 512, 1, 116, 149, RGB(41, 15, 17));
            Slash(dc, 571, 1, 116, 149, RGB(68, 20, 22));
            Slash(dc, 635, 1, 46, 149, kRed);
            Slash(dc, 662, 1, 18, 149, kText);
            Fill(dc, Bounds(0, 0, kWidth, 3), kRed);
            Label(dc, L"PES 2013  /  ONLINE", g_dialog.smallFont, kRed, Bounds(28, 17, 262, 25));
            Label(dc, experimental ? L"PRUEBA DE AMISTOSO" : L"SALA PRIVADA",
                g_dialog.titleFont, kText, Bounds(26, 43, 492, 63));
            const bool connected = g_dialog.phase == Phase::Connected;
            const bool room = InRoom(g_dialog.phase);
            const wchar_t* subtitle = experimental
                ? L"En los dos juegos, lleguen a la previa con los mismos equipos."
                : connected ? L"Conexi\u00f3n directa entre jugadores" : L"Cre\u00e1 una sala o ingres\u00e1 un c\u00f3digo de invitaci\u00f3n.";
            Label(dc, subtitle, g_dialog.bodyFont, experimental ? kAmber : kMuted, Bounds(28, 109, 504, 22));

            if (!room)
            {
                Box(dc, 28, 173, 337, 182, kSurface);
                Box(dc, 383, 173, 337, 182, kSurface);
                Label(dc, L"Crear sala", g_dialog.sectionFont, kText, Bounds(44, 183, 300, 29));
                Label(dc, L"Direcci\u00f3n IPv4 de tu PC", g_dialog.smallFont, kMuted, Bounds(44, 218, 300, 17));
                Label(dc, L"Unirte a una sala", g_dialog.sectionFont, kText, Bounds(399, 183, 300, 29));
                Label(dc, L"C\u00f3digo de invitaci\u00f3n", g_dialog.smallFont, kMuted, Bounds(399, 218, 300, 17));
                Box(dc, 28, 373, 692, 76, kSurface);
                Label(dc, L"Tu apodo", g_dialog.sectionFont, kText, Bounds(44, 388, 160, 29));
                Label(dc, L"De 2 a 15 caracteres", g_dialog.smallFont, kMuted, Bounds(44, 417, 165, 18));
            }
            else
            {
                DrawPlayer(dc, 28, g_dialog.host ? L"VOS / ANFITRI\u00d3N" : L"VOS / INVITADO", true);
                DrawPlayer(dc, 383, g_dialog.host ? L"INVITADO" : L"ANFITRI\u00d3N", connected);
                Box(dc, 28, 283, 692, 101, kSurface);
                if (g_dialog.host)
                    Label(dc, L"C\u00d3DIGO PARA INVITAR", g_dialog.smallFont, kMuted, Bounds(45, 293, 405, 24));
                else
                {
                    Label(dc, L"SESI\u00d3N POR INVITACI\u00d3N", g_dialog.smallFont, kMuted, Bounds(45, 295, 405, 24));
                    Label(dc, connected ? L"Sala de tu amigo" : L"Conectando con la sala\u2026",
                        g_dialog.sectionFont, kText, Bounds(45, 320, 405, 34));
                }

                if (experimental)
                {
                    if (g_dialog.host)
                        Label(dc, L"Por Internet, us\u00e1 la IPv4 p\u00fablica en el c\u00f3digo. Conserv\u00e1 el puerto y la clave.",
                            g_dialog.bodyFont, kMuted, Bounds(44, 391, 656, 24));
                    Label(dc, L"Prueba de mandos \u00b7 Sin medici\u00f3n de ping", g_dialog.smallFont, kMuted, Bounds(44, 421, 656, 22));
                }
                else
                {
                    Box(dc, 540, 296, 162, 75, kField);
                    Label(dc, L"PING", g_dialog.smallFont, kMuted, Bounds(552, 305, 103, 18));
                    for (int i = 0; i < 4; ++i)
                    {
                        const int height = 9 + i * 5;
                        Fill(dc, Bounds(652 + i * 9, 349 - height, 5, height),
                            i < g_dialog.signalBars ? g_dialog.signalColor : kLine);
                    }
                }
            }

            Box(dc, 28, 467, 692, 36, kSurface);
            const auto dot = g_dialog.phase == Phase::Error ? kRed
                : connected ? kGreen : room ? kAmber : kMuted;
            Fill(dc, Bounds(43, 480, 9, 9), dot);
            Fill(dc, Bounds(28, 517, 692, 1), kLine);
            Label(dc, L"F1 / Esc  \u00b7  Ocultar panel", g_dialog.smallFont, kMuted, Bounds(28, 528, 282, 31));

            const HWND focus = GetFocus();
            RECT editFocus{};
            if (focus == g_dialog.hostAddress) editFocus = Bounds(44, 245, 305, 31);
            else if (focus == g_dialog.joinCode) editFocus = Bounds(399, 245, 305, 31);
            else if (focus == g_dialog.shareCode) editFocus = Bounds(44, 331, 374, 31);
            else if (focus == g_dialog.playerName) editFocus = Bounds(214, 396, 340, 31);
            if (!IsRectEmpty(&editFocus))
            {
                SetDCBrushColor(dc, kRed);
                FrameRect(dc, &editFocus, static_cast<HBRUSH>(GetStockObject(DC_BRUSH)));
            }
        }

        void DrawButton(const DRAWITEMSTRUCT& item)
        {
            wchar_t text[96]{};
            GetWindowTextW(item.hwndItem, text, int(std::size(text)));
            const bool enabled = !(item.itemState & ODS_DISABLED);
            const bool pressed = item.itemState & ODS_SELECTED;
            const bool focused = item.itemState & ODS_FOCUS;
            const bool primary = item.CtlID == kCreate || item.CtlID == kJoin || item.CtlID == kCopy || item.CtlID == kSaveName;
            const bool stop = item.CtlID == kStop;
            const COLORREF fill = enabled && pressed ? kRed : enabled && primary ? kText : kField;
            const COLORREF color = !enabled ? kMuted : pressed || primary ? kBackground : stop ? kRed : kText;
            Fill(item.hDC, item.rcItem, fill);
            SetDCBrushColor(item.hDC, focused ? kRed : kLine);
            FrameRect(item.hDC, &item.rcItem, static_cast<HBRUSH>(GetStockObject(DC_BRUSH)));
            const auto font = item.CtlID == kCopy ? g_dialog.smallFont : g_dialog.bodyFont;
            Label(item.hDC, text, font, color, item.rcItem, DT_CENTER);
            if (focused)
            {
                RECT focus = item.rcItem;
                InflateRect(&focus, -Px(4), -Px(4));
                DrawFocusRect(item.hDC, &focus);
            }
        }

        std::string ReadAscii(HWND edit)
        {
            wchar_t buffer[256]{};
            const int length = GetWindowTextW(edit, buffer, int(sizeof(buffer) / sizeof(buffer[0])));
            std::string text;
            for (int i = 0; i < length && buffer[i] < 128; ++i)
                text += char(buffer[i]);
            if (text.size() != size_t(length))
                return {};
            const size_t first = text.find_first_not_of(" \t\r\n");
            if (first == std::string::npos)
                return {};
            return text.substr(first, text.find_last_not_of(" \t\r\n") - first + 1);
        }

        void RefreshDialog()
        {
            Status state;
            {
                std::lock_guard lock(g_session.mutex);
                state = g_session.status;
            }

            const bool active = InRoom(state.phase);
            const bool layoutChanged = g_dialog.phase != state.phase || g_dialog.host != state.host ||
                g_dialog.localName != state.localName || g_dialog.remoteName != state.remoteName;
            g_dialog.phase = state.phase;
            g_dialog.host = state.host;
            g_dialog.localName = state.localName;
            g_dialog.remoteName = state.remoteName;
            const auto oldBars = g_dialog.signalBars;
            const auto oldColor = g_dialog.signalColor;
            g_dialog.signalBars = 0;
            std::wstring ping = L"\u2014";
            if (state.ping)
            {
                const auto rounded = (state.ping->count() + 500) / 1000;
                ping = state.ping->count() < 1000 ? L"<1 ms" : std::to_wstring(rounded) + L" ms";
                g_dialog.signalBars = rounded <= 80 ? 4 : rounded <= 150 ? 3 : rounded <= 250 ? 2 : 1;
                g_dialog.signalColor = g_dialog.signalBars >= 3 ? kGreen : g_dialog.signalBars == 2 ? kAmber : kRed;
            }
            const auto updateText = [](HWND control, const std::wstring& text) {
                wchar_t current[400]{};
                GetWindowTextW(control, current, int(sizeof(current) / sizeof(current[0])));
                if (text != current)
                    SetWindowTextW(control, text.c_str());
            };
            updateText(g_dialog.status, state.message.empty() ? L"Sin conexi\u00f3n" : state.message);
            updateText(g_dialog.shareCode, state.code);
            if (!ExperimentalSync())
                updateText(g_dialog.ping, state.phase == Phase::Connected && !state.ping ? L"Midiendo\u2026" : ping);
            updateText(g_dialog.localNameLabel, state.localName.empty() ? L"Vos" : state.localName);
            updateText(g_dialog.remoteNameLabel, state.phase != Phase::Connected ? L"Esperando al rival" :
                state.remoteName.empty() ? L"Rival" : state.remoteName);
            if (layoutChanged)
            {
                ShowWindow(g_dialog.create, active ? SW_HIDE : SW_SHOW);
                ShowWindow(g_dialog.join, active ? SW_HIDE : SW_SHOW);
                ShowWindow(g_dialog.hostAddress, active ? SW_HIDE : SW_SHOW);
                ShowWindow(g_dialog.joinCode, active ? SW_HIDE : SW_SHOW);
                ShowWindow(g_dialog.shareCode, active && state.host ? SW_SHOW : SW_HIDE);
                ShowWindow(g_dialog.copy, active && state.host ? SW_SHOW : SW_HIDE);
                ShowWindow(g_dialog.ping, active && !ExperimentalSync() ? SW_SHOW : SW_HIDE);
                ShowWindow(g_dialog.stop, active ? SW_SHOW : SW_HIDE);
                ShowWindow(g_dialog.playerName, active ? SW_HIDE : SW_SHOW);
                ShowWindow(g_dialog.saveName, active ? SW_HIDE : SW_SHOW);
                ShowWindow(g_dialog.localNameLabel, active ? SW_SHOW : SW_HIDE);
                ShowWindow(g_dialog.remoteNameLabel, active ? SW_SHOW : SW_HIDE);
                const HWND focus = GetFocus();
                if (focus && IsWindowVisible(g_dialog.window) && IsChild(g_dialog.window, focus) && !IsWindowVisible(focus))
                    SetFocus(active ? g_dialog.back : g_dialog.joinCode);
                InvalidateRect(g_dialog.window, nullptr, FALSE);
            }
            else if (oldBars != g_dialog.signalBars || oldColor != g_dialog.signalColor)
            {
                const auto signal = Bounds(644, 320, 48, 34);
                InvalidateRect(g_dialog.window, &signal, FALSE);
            }

            if (bool(IsWindowEnabled(g_dialog.copy)) != !state.code.empty())
                EnableWindow(g_dialog.copy, !state.code.empty());
        }

        void ShowInputError(const wchar_t* message)
        {
            std::lock_guard lock(g_session.mutex);
            if (g_session.status.phase == Phase::Idle || g_session.status.phase == Phase::Error)
                g_session.status = { Phase::Error, message, {} };
        }

        std::wstring SaveDialogName(bool announce)
        {
            wchar_t value[kMaxPlayerNameLength + 1]{};
            GetWindowTextW(g_dialog.playerName, value, int(std::size(value)));
            const auto name = NormalizePlayerName(value);
            if (name.empty())
            {
                ShowInputError(L"Escrib\u00ed un apodo v\u00e1lido de 2 a 15 caracteres.");
                SetFocus(g_dialog.playerName);
                return {};
            }
            if (!SavePlayerName(name))
            {
                ShowInputError(L"No se pudo guardar el apodo.");
                return {};
            }
            SetWindowTextW(g_dialog.playerName, name.c_str());
            if (announce)
            {
                std::lock_guard lock(g_session.mutex);
                if (!InRoom(g_session.status.phase))
                    g_session.status = { Phase::Idle, L"Apodo guardado.", {} };
            }
            return name;
        }

        void CopyCode(HWND window)
        {
            std::wstring code;
            {
                std::lock_guard lock(g_session.mutex);
                code = g_session.status.code;
            }
            if (code.empty())
                return;

            const size_t size = (code.size() + 1) * sizeof(wchar_t);
            HGLOBAL data = GlobalAlloc(GMEM_MOVEABLE, size);
            if (!data)
                return;
            if (void* memory = GlobalLock(data))
            {
                std::memcpy(memory, code.c_str(), size);
                GlobalUnlock(data);
                if (OpenClipboard(window))
                {
                    if (EmptyClipboard() && SetClipboardData(CF_UNICODETEXT, data))
                    {
                        data = nullptr;
                        std::lock_guard lock(g_session.mutex);
                        if (g_session.status.code == code && g_session.status.phase == Phase::Hosting)
                            g_session.status.message = L"C\u00f3digo copiado. Esperando a tu amigo\u2026";
                    }
                    CloseClipboard();
                }
            }
            if (data)
                GlobalFree(data);
        }

        void HideDialog(HWND window)
        {
            ShowWindow(window, SW_HIDE);
            if (HWND owner = GetWindow(window, GW_OWNER))
                SetForegroundWindow(owner);
        }

        LRESULT CALLBACK RoomDialogProc(HWND window, UINT message, WPARAM wParam, LPARAM lParam)
        {
            switch (message)
            {
            case WM_PAINT:
            {
                PAINTSTRUCT paint{};
                HDC dc = BeginPaint(window, &paint);
                DrawPanel(dc);
                EndPaint(window, &paint);
                return 0;
            }
            case WM_ERASEBKGND:
                return TRUE;
            case WM_PRINTCLIENT:
                DrawPanel(reinterpret_cast<HDC>(wParam));
                return 0;
            case WM_NCHITTEST:
                if (DefWindowProcW(window, message, wParam, lParam) == HTCLIENT)
                {
                    POINT p{ short(LOWORD(lParam)), short(HIWORD(lParam)) };
                    ScreenToClient(window, &p);
                    return p.y <= Px(150) ? HTCAPTION : HTCLIENT;
                }
                return HTCLIENT;
            case WM_DRAWITEM:
                if (const auto* item = reinterpret_cast<DRAWITEMSTRUCT*>(lParam); item && item->CtlType == ODT_BUTTON)
                {
                    DrawButton(*item);
                    return TRUE;
                }
                break;
            case WM_CTLCOLOREDIT:
            case WM_CTLCOLORSTATIC:
            {
                const auto dc = reinterpret_cast<HDC>(wParam);
                const auto child = reinterpret_cast<HWND>(lParam);
                const bool edit = child == g_dialog.hostAddress || child == g_dialog.joinCode ||
                    child == g_dialog.shareCode || child == g_dialog.playerName;
                const bool ping = child == g_dialog.ping;
                const bool waiting = child == g_dialog.remoteNameLabel && g_dialog.phase != Phase::Connected;
                SetTextColor(dc, ping && g_dialog.signalBars ? g_dialog.signalColor : waiting ? kMuted : kText);
                SetBkColor(dc, edit || ping ? kField : kSurface);
                return reinterpret_cast<LRESULT>(edit || ping ? g_dialog.fieldBrush : g_dialog.surfaceBrush);
            }
            case WM_CLOSE:
                HideDialog(window);
                return 0;
            case WM_TIMER:
                if (wParam == kRefreshTimer)
                {
                    if (IsWindowVisible(window))
                        RefreshDialog();
                }
                return 0;
            case WM_COMMAND:
                if (HIWORD(wParam) == EN_SETFOCUS || HIWORD(wParam) == EN_KILLFOCUS)
                    InvalidateRect(window, nullptr, FALSE);
                switch (LOWORD(wParam))
                {
                case kCreate:
                {
                    auto address = ReadAscii(g_dialog.hostAddress);
                    if (address.empty())
                        ShowInputError(L"Escrib\u00ed la direcci\u00f3n IPv4 de tu PC.");
                    else if (auto name = SaveDialogName(false); !name.empty())
                        StartRoom(true, { std::move(address), 0, {} }, std::move(name));
                    break;
                }
                case kJoin:
                {
                    Destination destination;
                    if (!ParseCode(ReadAscii(g_dialog.joinCode), destination))
                        ShowInputError(L"Peg\u00e1 el c\u00f3digo completo que te pas\u00f3 tu amigo (IP:puerto/clave).");
                    else if (auto name = SaveDialogName(false); !name.empty())
                        StartRoom(false, std::move(destination), std::move(name));
                    break;
                }
                case kCopy:
                    CopyCode(window);
                    break;
                case kStop:
                    StopRoom();
                    break;
                case kSaveName:
                    SaveDialogName(true);
                    break;
                case IDCANCEL:
                case kClose:
                    HideDialog(window);
                    return 0;
                default:
                    return DefWindowProcW(window, message, wParam, lParam);
                }
                RefreshDialog();
                return 0;
            case WM_DESTROY:
                KillTimer(window, kRefreshTimer);
                StopRoom();
                for (auto font : { g_dialog.titleFont, g_dialog.sectionFont, g_dialog.bodyFont,
                        g_dialog.smallFont, g_dialog.codeFont, g_dialog.invitationFont, g_dialog.pingFont })
                    DeleteObject(font);
                DeleteObject(g_dialog.fieldBrush);
                DeleteObject(g_dialog.surfaceBrush);
                g_dialog = {};
                return 0;
            }
            return DefWindowProcW(window, message, wParam, lParam);
        }

        HWND CreateDialogWindow(HWND owner)
        {
            constexpr wchar_t className[] = L"PES13PrivateRoom";
            const auto instance = GetModuleHandleW(nullptr);
            WNDCLASSW windowClass{};
            windowClass.lpfnWndProc = RoomDialogProc;
            windowClass.hInstance = instance;
            windowClass.hCursor = LoadCursor(nullptr, IDC_ARROW);
            windowClass.lpszClassName = className;
            if (!RegisterClassW(&windowClass) && GetLastError() != ERROR_CLASS_ALREADY_EXISTS)
                return nullptr;

            constexpr DWORD style = WS_POPUP | WS_CLIPCHILDREN;
            constexpr DWORD exStyle = WS_EX_TOOLWINDOW | WS_EX_CONTROLPARENT;
            RECT area{};
            if (!GetWindowRect(owner, &area))
                SystemParametersInfoW(SPI_GETWORKAREA, 0, &area, 0);
            MONITORINFO monitor{ sizeof(monitor) };
            if (!GetMonitorInfoW(MonitorFromWindow(owner, MONITOR_DEFAULTTONEAREST), &monitor))
                monitor.rcWork = area;
            using GetDpi = UINT(WINAPI*)(HWND);
            const auto dpi = reinterpret_cast<GetDpi>(GetProcAddress(GetModuleHandleW(L"user32.dll"), "GetDpiForWindow"));
            g_dialog.scale = std::min({ dpi ? float(dpi(owner)) / 96.0f : 1.0f,
                float(monitor.rcWork.right - monitor.rcWork.left - 24) / kWidth,
                float(monitor.rcWork.bottom - monitor.rcWork.top - 24) / kHeight });
            if (g_dialog.scale <= 0.0f) g_dialog.scale = 1.0f;
            const int w = Px(kWidth), h = Px(kHeight);
            const int x = std::clamp(int(area.left + (area.right - area.left - w) / 2),
                int(monitor.rcWork.left), int(monitor.rcWork.right - w));
            const int y = std::clamp(int(area.top + (area.bottom - area.top - h) / 2),
                int(monitor.rcWork.top), int(monitor.rcWork.bottom - h));
            const auto title = ExperimentalSync() ? L"PES 2013 \u00b7 Prueba de amistoso" : L"PES 2013 \u00b7 Sala privada";
            HWND window = CreateWindowExW(exStyle, className, title, style,
                x, y, w, h, owner, nullptr, instance, nullptr);
            if (!window)
                return nullptr;

            g_dialog.window = window;
            const auto font = [](const wchar_t* face, int height, int weight) {
                return CreateFontW(-Px(height), 0, 0, 0, weight, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
                    OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH, face);
            };
            g_dialog.titleFont = font(L"Bahnschrift Condensed", ExperimentalSync() ? 49 : 57, FW_BOLD);
            g_dialog.sectionFont = font(L"Segoe UI", 23, FW_SEMIBOLD);
            g_dialog.bodyFont = font(L"Segoe UI", 15, FW_SEMIBOLD);
            g_dialog.smallFont = font(L"Segoe UI", 13, FW_SEMIBOLD);
            g_dialog.codeFont = font(L"Consolas", 14, FW_NORMAL);
            g_dialog.invitationFont = font(L"Consolas", 11, FW_NORMAL);
            g_dialog.pingFont = font(L"Consolas", 25, FW_BOLD);
            g_dialog.fieldBrush = CreateSolidBrush(kField);
            g_dialog.surfaceBrush = CreateSolidBrush(kSurface);

            const auto control = [&](const wchar_t* type, const wchar_t* text, HFONT face, DWORD flags,
                int cx, int cy, int cw, int ch, int id = -1) {
                HWND item = CreateWindowExW(0, type, text, WS_CHILD | flags,
                    Px(cx), Px(cy), Px(cw), Px(ch), window, HMENU(INT_PTR(id)), instance, nullptr);
                SendMessageW(item, WM_SETFONT, WPARAM(face), FALSE);
                return item;
            };
            const auto edit = [&](const wchar_t* text, int x, int y, int width, int limit, DWORD flags) {
                HWND item = control(L"EDIT", text, g_dialog.codeFont, WS_TABSTOP | ES_AUTOHSCROLL | flags,
                    x, y, width, 29);
                SendMessageW(item, EM_LIMITTEXT, limit, 0);
                SendMessageW(item, EM_SETMARGINS, EC_LEFTMARGIN | EC_RIGHTMARGIN, MAKELPARAM(Px(8), Px(8)));
                return item;
            };
            const auto button = [&](const wchar_t* text, int x, int y, int width, int height, int id) {
                return control(L"BUTTON", text, g_dialog.bodyFont, WS_TABSTOP | BS_OWNERDRAW,
                    x, y, width, height, id);
            };

            g_dialog.hostAddress = edit(L"127.0.0.1", 45, 246, 303, 15, 0);
            g_dialog.create = button(L"Crear sala", 44, 298, 305, 39, kCreate);
            g_dialog.joinCode = edit(L"", 400, 246, 303, 128, 0);
            g_dialog.join = button(L"Unirme", 399, 298, 305, 39, kJoin);
            g_dialog.shareCode = edit(L"", 45, 332, 372, 128, ES_READONLY);
            SendMessageW(g_dialog.shareCode, WM_SETFONT, WPARAM(g_dialog.invitationFont), FALSE);
            SendMessageW(g_dialog.shareCode, EM_SETMARGINS, EC_LEFTMARGIN | EC_RIGHTMARGIN, MAKELPARAM(Px(4), Px(4)));
            g_dialog.copy = button(L"Copiar c\u00f3digo", 426, 323, 104, 42, kCopy);
            g_dialog.playerName = control(L"EDIT", LoadPlayerName().c_str(), g_dialog.codeFont,
                WS_TABSTOP | ES_AUTOHSCROLL, 215, 397, 338, 29, kPlayerName);
            SendMessageW(g_dialog.playerName, EM_LIMITTEXT, kMaxPlayerNameLength, 0);
            SendMessageW(g_dialog.playerName, EM_SETMARGINS, EC_LEFTMARGIN | EC_RIGHTMARGIN, MAKELPARAM(Px(8), Px(8)));
            g_dialog.saveName = button(L"Guardar apodo", 568, 393, 135, 38, kSaveName);
            g_dialog.localNameLabel = control(L"STATIC", L"", g_dialog.sectionFont, SS_LEFT | SS_CENTERIMAGE | SS_NOPREFIX,
                103, 213, 235, 30, kLocalNameLabel);
            g_dialog.remoteNameLabel = control(L"STATIC", L"", g_dialog.sectionFont, SS_LEFT | SS_CENTERIMAGE | SS_NOPREFIX,
                458, 213, 235, 30, kRemoteNameLabel);
            g_dialog.status = control(L"STATIC", L"", g_dialog.bodyFont, WS_VISIBLE | SS_LEFT | SS_CENTERIMAGE | SS_NOPREFIX,
                63, 471, 640, 27, kStatus);
            g_dialog.ping = control(L"STATIC", L"", g_dialog.pingFont, SS_LEFT | SS_CENTERIMAGE | SS_NOPREFIX,
                551, 323, 100, 37, kPing);
            g_dialog.stop = button(L"Salir de la sala", 399, 528, 145, 34, kStop);
            g_dialog.back = button(L"Volver al juego", 558, 528, 162, 34, IDCANCEL);
            g_dialog.close = button(L"\u00d7", 685, 20, 34, 34, kClose);
            ShowWindow(g_dialog.back, SW_SHOW);
            ShowWindow(g_dialog.close, SW_SHOW);
            // Force the first layout refresh even when no room is active.
            g_dialog.host = true;
            SetTimer(window, kRefreshTimer, 125, nullptr);
            return window;
        }
    }

    void ToggleRoomDialog(HWND gameWindow)
    {
        if (g_dialog.window && IsWindowVisible(g_dialog.window))
        {
            HideDialog(g_dialog.window);
            return;
        }
        if (!g_dialog.window && !CreateDialogWindow(gameWindow))
            return;
        RefreshDialog();
        ShowWindow(g_dialog.window, SW_SHOWNORMAL);
        SetForegroundWindow(g_dialog.window);
        SetFocus(InRoom(g_dialog.phase) ? g_dialog.back :
            GetWindowTextLengthW(g_dialog.playerName) == 0 ? g_dialog.playerName : g_dialog.joinCode);
    }

    bool HandleRoomDialogMessage(MSG& message)
    {
        if (HandleLobbyDialogMessage(message))
            return true;
        HWND window = g_dialog.window;
        if (!window || !IsWindowVisible(window))
            return false;
        if (message.message == WM_KEYDOWN && (message.hwnd == window || IsChild(window, message.hwnd)))
        {
            if (message.wParam == VK_F1 || message.wParam == VK_ESCAPE)
            {
                if (!(message.lParam & (1L << 30)))
                    HideDialog(window);
                return true;
            }
            if (message.wParam == VK_RETURN && message.hwnd == g_dialog.joinCode)
            {
                if (IsWindowEnabled(g_dialog.join))
                    SendMessageW(g_dialog.join, BM_CLICK, 0, 0);
                return true;
            }
            if (message.wParam == VK_RETURN && message.hwnd == g_dialog.playerName)
            {
                SendMessageW(g_dialog.saveName, BM_CLICK, 0, 0);
                return true;
            }
        }
        return IsDialogMessageW(window, &message) != FALSE;
    }

    std::shared_ptr<Room> ConnectedRoom()
    {
        std::lock_guard lock(g_session.mutex);
        return g_session.room;
    }

    std::wstring RoomPlayerName(bool local)
    {
        std::lock_guard lock(g_session.mutex);
        if (g_session.status.phase != Phase::Connected)
            return {};
        return local ? g_session.status.localName : g_session.status.remoteName;
    }

    void ReportRoomFailure(const std::shared_ptr<Room>& room, std::string_view reason)
    {
        std::lock_guard lock(g_session.mutex);
        if (g_session.room != room || !room)
            return;
        g_session.room.reset();
        g_session.status = { Phase::Error, reason.empty() ? L"Se cerr\u00f3 la conexi\u00f3n con tu amigo." : Wide(reason), {} };
    }
}
