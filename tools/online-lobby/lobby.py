"""Local PES 2013 lobby for presence, rooms, invitations, and match chat."""

import argparse
from collections import deque
from dataclasses import dataclass, field
from http import HTTPStatus
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
import json
import re
import secrets
import threading
import time
import unicodedata


PLAYER_TIMEOUT = 15
INVITE_TIMEOUT = 30
MAX_PLAYERS = 32
MAX_INVITES_PER_PLAYER = 8
MAX_BODY_BYTES = 512
MAX_CHAT_BODY_BYTES = 2300
MAX_CHAT_CHARS = 180
MAX_CHAT_MESSAGES = 40
CHAT_BURST = 6
CHAT_WINDOW_SECONDS = 3
CHAT_REACTIONS = frozenset({"jaja", "uy", "payaso", "frio", "casi", "var"})
ID_PATTERN = re.compile(r"[a-f0-9]{16}", re.ASCII)
TOKEN_PATTERN = re.compile(r"[a-f0-9]{48}", re.ASCII)


class LobbyError(Exception):
    def __init__(self, status, message):
        super().__init__(message)
        self.status = status


@dataclass
class Player:
    id: str
    name: str
    token: str
    last_seen: float
    chat_times: deque = field(default_factory=deque)


@dataclass
class Invite:
    id: str
    sender: str
    recipient: str
    expires: float


@dataclass
class Match:
    id: str
    host: str
    guest: str
    kind: str
    messages: deque = field(default_factory=lambda: deque(maxlen=MAX_CHAT_MESSAGES))


def display_name(value):
    if not isinstance(value, str):
        raise LobbyError(400, "El apodo debe tener entre 2 y 15 caracteres.")
    value = value.strip(" ")
    if not 2 <= len(value) <= 15 or any(
        ord(ch) > 0xFFFF
        or not (unicodedata.category(ch)[0] in "LMN" or ch in " -_'.")
        for ch in value
    ):
        raise LobbyError(400, "El apodo debe tener entre 2 y 15 caracteres válidos.")
    if not any(unicodedata.category(ch)[0] in "LN" for ch in value):
        raise LobbyError(400, "El apodo debe incluir alguna letra o número.")
    return value


def fields(payload, *names):
    if type(payload) is not dict or set(payload) != set(names):
        raise LobbyError(400, "Los datos de la solicitud son inválidos.")


def identifier(value):
    if not isinstance(value, str) or not ID_PATTERN.fullmatch(value):
        raise LobbyError(400, "El identificador es inválido.")
    return value


def chat_text(value):
    error = f"El mensaje debe tener entre 1 y {MAX_CHAT_CHARS} caracteres en una sola línea."
    if not isinstance(value, str) or any(
        unicodedata.category(ch) in ("Cc", "Cs", "Zl", "Zp") for ch in value
    ):
        raise LobbyError(400, error)
    value = value.strip(" ")
    if not 1 <= len(value) <= MAX_CHAT_CHARS:
        raise LobbyError(400, error)
    return value


class Lobby:
    def __init__(self, clock=time.monotonic):
        self.clock = clock
        self.lock = threading.Lock()
        self.players = {}
        self.tokens = {}
        self.rooms = {}
        self.invites = {}
        self.matches = {}
        self.next_message_id = 1

    def _forget_invites(self, player_id):
        for key, invite in list(self.invites.items()):
            if player_id in (invite.sender, invite.recipient):
                del self.invites[key]

    def _leave(self, player_id):
        self.rooms.pop(player_id, None)
        self._forget_invites(player_id)
        match = self.matches.pop(player_id, None)
        if match:
            self.matches.pop(self._peer(match, player_id), None)

    def _disconnect(self, player_id):
        self._leave(player_id)
        self.tokens.pop(self.players.pop(player_id).token)

    def _expire(self, now):
        for key, invite in list(self.invites.items()):
            if now >= invite.expires:
                del self.invites[key]
        for key, player in list(self.players.items()):
            if now - player.last_seen >= PLAYER_TIMEOUT:
                self._disconnect(key)

    def has_players(self):
        with self.lock:
            self._expire(self.clock())
            return bool(self.players)

    def _peer(self, match, player_id):
        return match.guest if match.host == player_id else match.host

    def _player_view(self, player):
        if player.id in self.matches:
            status = "matched"
        elif player.id in self.rooms:
            status = "hosting"
        else:
            status = "available"
        return dict(id=player.id, name=player.name, status=status)

    def _snapshot(self, player_id):
        active = self.matches.get(player_id)
        match = None
        if active:
            peer = self._peer(active, player_id)
            match = dict(id=active.id, peer_id=peer, peer_name=self.players[peer].name,
                         kind=active.kind, role="host" if active.host == player_id else "guest")
        return dict(
            you=self._player_view(self.players[player_id]),
            players=[self._player_view(player) for player in self.players.values()
                     if player.id != player_id],
            rooms=[dict(id=room_id, host_id=host, host_name=self.players[host].name)
                   for host, room_id in self.rooms.items()],
            invitations=[dict(id=invite.id, from_id=invite.sender,
                              from_name=self.players[invite.sender].name)
                         for invite in self.invites.values() if invite.recipient == player_id],
            match=match,
            messages=list(active.messages) if active else [],
        )

    def _available_pair(self, first, second):
        if first == second:
            raise LobbyError(409, "Elegí a otro jugador.")
        if second not in self.players:
            raise LobbyError(404, "Ese jugador se desconectó.")
        if first in self.matches or second in self.matches:
            raise LobbyError(409, "Uno de los jugadores ya tiene pareja.")

    def _pair(self, host, guest, kind):
        self._available_pair(host, guest)
        result = Match(secrets.token_hex(8), host, guest, kind)
        for player_id in (host, guest):
            self.rooms.pop(player_id, None)
            self._forget_invites(player_id)
            self.matches[player_id] = result

    def _chat(self, player, payload, now):
        if isinstance(payload, dict) and "text" in payload:
            fields(payload, "match_id", "text")
            text, reaction = chat_text(payload["text"]), ""
        else:
            fields(payload, "match_id", "reaction")
            text, reaction = "", payload["reaction"]
            if not isinstance(reaction, str) or reaction not in CHAT_REACTIONS:
                raise LobbyError(400, "La reacción no existe.")
        match_id = identifier(payload["match_id"])
        match = self.matches.get(player.id)
        if not match or match.id != match_id:
            raise LobbyError(409, "La sala de este mensaje ya no está activa.")

        times = player.chat_times
        while times and now - times[0] >= CHAT_WINDOW_SECONDS:
            times.popleft()
        if len(times) >= CHAT_BURST:
            raise LobbyError(429, "Esperá un momento antes de enviar otro mensaje.")
        times.append(now)
        match.messages.append(dict(id=self.next_message_id, name=player.name,
                                   text=text, reaction=reaction, sender_id=player.id))
        self.next_message_id += 1

    def request(self, path, payload, token=None):
        with self.lock:
            now = self.clock()
            self._expire(now)
            if path == "/session":
                fields(payload, "name")
                name = display_name(payload["name"])
                if len(self.players) >= MAX_PLAYERS:
                    raise LobbyError(429, "El lobby local está lleno.")
                token = secrets.token_hex(24)
                player = Player(secrets.token_hex(8), name, token, now)
                self.players[player.id] = player
                self.tokens[token] = player.id
                return dict(token=token, player=self._player_view(player))

            if token not in self.tokens:
                raise LobbyError(401, "Volvé a conectar al lobby.")
            player_id = self.tokens[token]
            self.players[player_id].last_seen = now
            if path == "/state":
                fields(payload)
            elif path == "/room":
                fields(payload)
                if player_id in self.matches:
                    raise LobbyError(409, "Salí de la sala actual antes de abrir otra.")
                self.rooms.setdefault(player_id, secrets.token_hex(8))
            elif path == "/join":
                fields(payload, "room_id")
                room_id = identifier(payload["room_id"])
                host = next((host for host, room in self.rooms.items() if room == room_id), None)
                if not host:
                    raise LobbyError(404, "Esa sala pública ya no está disponible.")
                self._pair(host, player_id, "public")
            elif path == "/invite":
                fields(payload, "player_id")
                target = identifier(payload["player_id"])
                self._available_pair(player_id, target)
                duplicate = any(invite.sender == player_id and invite.recipient == target
                                for invite in self.invites.values())
                if not duplicate:
                    if sum(invite.sender == player_id for invite in self.invites.values()) >= MAX_INVITES_PER_PLAYER:
                        raise LobbyError(429, "Ya enviaste demasiadas invitaciones. Esperá antes de invitar de nuevo.")
                    invite = Invite(secrets.token_hex(8), player_id, target, now + INVITE_TIMEOUT)
                    self.invites[invite.id] = invite
            elif path == "/respond":
                fields(payload, "invite_id", "accept")
                invite_id = identifier(payload["invite_id"])
                if type(payload["accept"]) is not bool:
                    raise LobbyError(400, "Elegí aceptar o rechazar la invitación.")
                invite = self.invites.get(invite_id)
                if not invite or invite.recipient != player_id:
                    raise LobbyError(404, "La invitación ya no está disponible.")
                del self.invites[invite_id]
                if payload["accept"]:
                    self._pair(invite.sender, player_id, "private")
            elif path == "/chat":
                self._chat(self.players[player_id], payload, now)
            elif path == "/leave":
                fields(payload)
                self._leave(player_id)
            elif path == "/disconnect":
                fields(payload)
                self._disconnect(player_id)
                return dict(ok=True)
            else:
                raise LobbyError(404, "La operación no existe.")
            return self._snapshot(player_id)


class Handler(BaseHTTPRequestHandler):
    def setup(self):
        self.request.settimeout(3)
        super().setup()

    def _send(self, status, payload):
        data = json.dumps(payload, ensure_ascii=True, separators=(",", ":")).encode("ascii")
        self.send_response(HTTPStatus(status))
        self.send_header("Content-Type", "application/json; charset=utf-8")
        self.send_header("Content-Length", str(len(data)))
        self.send_header("Cache-Control", "no-store")
        self.end_headers()
        self.wfile.write(data)

    def _token(self):
        authorization = self.headers.get("Authorization", "")
        if authorization.startswith("Bearer ") and TOKEN_PATTERN.fullmatch(authorization[7:]):
            return authorization[7:]
        return None

    def _read_body(self):
        raw_length = self.headers.get("Content-Length", "")
        if not raw_length.isascii() or not raw_length.isdecimal() or len(raw_length) > 4:
            raise LobbyError(400, "La solicitud debe contener un objeto JSON.")
        length = int(raw_length)
        max_length = MAX_CHAT_BODY_BYTES if self.path == "/chat" else MAX_BODY_BYTES
        if not 0 < length <= max_length:
            raise LobbyError(400, "La solicitud es demasiado grande o está vacía.")
        data = self.rfile.read(length)
        if len(data) != length:
            raise LobbyError(400, "La solicitud quedó incompleta.")
        try:
            return json.loads(data.decode("utf-8"))
        except (UnicodeError, ValueError, RecursionError) as error:
            raise LobbyError(400, "No se pudo leer la solicitud.") from error

    def _handle(self, post):
        try:
            expected_post = self.path not in ("/state", "/health")
            if post != expected_post:
                raise LobbyError(405, "El método de la solicitud no corresponde.")
            payload = self._read_body() if post else {}
            status = 200
            if self.path == "/health":
                result = {"service": "pes13-lobby", "version": 2}
            else:
                result = self.server.lobby.request(self.path, payload, self._token())
        except LobbyError as error:
            status = error.status
            result = {"error": str(error)}
        except OSError:
            return
        try:
            self._send(status, result)
        except OSError:
            return

    def do_GET(self):
        self._handle(post=False)

    def do_POST(self):
        self._handle(post=True)

    def log_message(self, *_args):
        pass


def serve(host, port, idle_seconds=0):
    with ThreadingHTTPServer((host, port), Handler) as server:
        server.lobby = Lobby()
        print(f"Lobby local escuchando en http://{host}:{server.server_port}", flush=True)
        if not idle_seconds:
            server.serve_forever()
            return
        server.timeout = 0.5
        last_active = time.monotonic()
        while time.monotonic() - last_active < idle_seconds:
            server.handle_request()
            if server.lobby.has_players():
                last_active = time.monotonic()


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--host", default="127.0.0.1", choices=["127.0.0.1"])
    parser.add_argument("--port", type=int, default=8766)
    parser.add_argument("--idle-seconds", type=int, default=0)
    args = parser.parse_args()
    if args.idle_seconds < 0:
        parser.error("--idle-seconds debe ser mayor o igual a cero")
    try:
        serve(args.host, args.port, args.idle_seconds)
    except KeyboardInterrupt:
        pass
