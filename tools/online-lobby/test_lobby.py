from concurrent.futures import ThreadPoolExecutor
from http.client import HTTPConnection
from http.server import ThreadingHTTPServer
import json
import threading
import unittest

from lobby import CHAT_BURST, CHAT_WINDOW_SECONDS, Handler, INVITE_TIMEOUT, Lobby, LobbyError
from lobby import MAX_CHAT_CHARS, MAX_CHAT_MESSAGES, PLAYER_TIMEOUT


class LobbyTests(unittest.TestCase):
    def setUp(self):
        self.now = 0
        self.lobby = Lobby(clock=lambda: self.now)
        self.first = self.register("Julián")
        self.second = self.register("José")
        self.third = self.register("Lautaro")

    def register(self, name):
        return self.lobby.request("/session", {"name": name})

    def ask(self, client, route, **payload):
        return self.lobby.request(route, payload, client["token"])

    def test_public_join_and_leave_from_either_side(self):
        for leaving in [self.first, self.second]:
            own_room = self.ask(self.first, "/room")["rooms"][0]
            visible_room = self.ask(self.second, "/state")["rooms"][0]
            self.assertEqual(own_room, visible_room)
            guest_match = self.ask(self.second, "/join", room_id=visible_room["id"])["match"]
            host_view = self.ask(self.first, "/state")
            self.assertFalse(host_view["rooms"])
            self.assertEqual(host_view["match"]["id"], guest_match["id"])
            self.assertEqual(host_view["match"]["role"], "host")
            self.assertEqual(guest_match["role"], "guest")
            self.assertEqual(guest_match["kind"], "public")
            self.assertEqual(guest_match["peer_name"], "Julián")
            self.ask(leaving, "/leave")
            self.assertIsNone(self.ask(self.first, "/state")["match"])
            self.assertIsNone(self.ask(self.second, "/state")["match"])
            self.assertEqual(self.ask(self.second, "/state")["you"]["status"], "available")

    def test_private_invites_require_the_recipients_acceptance(self):
        invitation_target = self.second["player"]["id"]
        self.ask(self.first, "/invite", player_id=invitation_target)
        self.ask(self.first, "/invite", player_id=invitation_target)
        received = self.ask(self.second, "/state")["invitations"]
        self.assertEqual(len(received), 1)
        self.assertFalse(self.ask(self.third, "/state")["invitations"])
        self.assertFalse(self.ask(self.third, "/state")["rooms"])
        self.assertIsNone(self.ask(self.first, "/state")["match"])
        invite_id = received[0]["id"]
        with self.assertRaises(LobbyError) as raised:
            self.ask(self.third, "/respond", invite_id=invite_id, accept=True)
        self.assertEqual(raised.exception.status, 404)
        self.assertIsNone(self.ask(self.second, "/respond", invite_id=invite_id, accept=False)["match"])
        self.ask(self.first, "/invite", player_id=invitation_target)
        invite_id = self.ask(self.second, "/state")["invitations"][0]["id"]
        guest_match = self.ask(self.second, "/respond", invite_id=invite_id, accept=True)["match"]
        self.assertEqual(guest_match["kind"], "private")
        self.assertFalse(self.ask(self.third, "/state")["rooms"])
        self.assertEqual(self.ask(self.first, "/state")["match"]["id"], guest_match["id"])
        self.ask(self.second, "/leave")
        self.assertIsNone(self.ask(self.first, "/state")["match"])

    def test_only_one_guest_claims_a_public_room(self):
        room_id = self.ask(self.first, "/room")["rooms"][0]["id"]

        def join(client):
            try:
                return self.ask(client, "/join", room_id=room_id)["match"]
            except LobbyError as error:
                return error.status

        with ThreadPoolExecutor(2) as pool:
            outcomes = list(pool.map(join, (self.second, self.third)))
        self.assertEqual(outcomes.count(404), 1)
        self.assertEqual(sum(isinstance(outcome, dict) for outcome in outcomes), 1)

    def test_stale_and_disconnected_players_and_invites_are_removed(self):
        self.ask(self.first, "/invite", player_id=self.second["player"]["id"])
        self.ask(self.third, "/room")
        self.now = PLAYER_TIMEOUT - 1
        self.ask(self.second, "/state")
        self.now = PLAYER_TIMEOUT
        state = self.ask(self.second, "/state")
        self.assertFalse(state["players"])
        self.assertFalse(state["rooms"])
        self.assertFalse(state["invitations"])
        with self.assertRaises(LobbyError) as raised:
            self.ask(self.first, "/state")
        self.assertEqual(raised.exception.status, 401)

    def test_invitation_expires_with_both_players_online(self):
        self.ask(self.first, "/invite", player_id=self.second["player"]["id"])
        invite_id = self.ask(self.second, "/state")["invitations"][0]["id"]
        for self.now in range(5, INVITE_TIMEOUT, 5):
            self.ask(self.first, "/state")
            self.ask(self.second, "/state")
        self.now = INVITE_TIMEOUT
        with self.assertRaises(LobbyError) as raised:
            self.ask(self.second, "/respond", invite_id=invite_id, accept=True)
        self.assertEqual(raised.exception.status, 404)
        self.assertIsNone(self.ask(self.first, "/state")["match"])
        self.assertEqual(self.ask(self.second, "/disconnect"), {"ok": True})
        with self.assertRaises(LobbyError) as raised:
            self.ask(self.second, "/state")
        self.assertEqual(raised.exception.status, 401)

    def test_idle_check_forgets_stale_sessions(self):
        self.assertTrue(self.lobby.has_players())
        self.now = PLAYER_TIMEOUT
        self.assertFalse(self.lobby.has_players())
        self.register("Nuevo")
        self.assertTrue(self.lobby.has_players())

    def pair(self, host, guest):
        host_id = host["player"]["id"]
        rooms = self.ask(host, "/room")["rooms"]
        room_id = next(room["id"] for room in rooms if room["host_id"] == host_id)
        return self.ask(guest, "/join", room_id=room_id)["match"]["id"]

    def test_chat_and_reactions_belong_to_the_paired_match(self):
        match_id = self.pair(self.first, self.second)
        fourth = self.register("Elena")
        other_match_id = self.pair(self.third, fourth)
        sent = self.ask(self.first, "/chat", match_id=match_id, text="  ¡Buen partido!  ")["messages"]
        both = self.ask(self.second, "/chat", match_id=match_id, reaction="jaja")["messages"]

        self.assertEqual(sent, [dict(id=both[0]["id"], name="Julián", text="¡Buen partido!",
                                    reaction="", sender_id=self.first["player"]["id"])])
        self.assertEqual(both[1], dict(id=both[1]["id"], name="José", text="", reaction="jaja",
                                      sender_id=self.second["player"]["id"]))
        self.assertGreater(both[0]["id"], 0)
        self.assertGreater(both[1]["id"], both[0]["id"])
        self.assertEqual(self.ask(self.first, "/state")["messages"], both)
        self.assertEqual(self.ask(self.third, "/state")["messages"], [])
        self.assertEqual(self.ask(fourth, "/state")["messages"], [])

        for other, wrong_id in ((self.third, match_id), (self.first, other_match_id)):
            with self.assertRaises(LobbyError) as raised:
                self.ask(other, "/chat", match_id=wrong_id, text="No debe enviarse")
            self.assertEqual(raised.exception.status, 409)
        self.assertEqual(self.ask(self.first, "/state")["messages"], both)
        self.assertEqual(self.ask(self.third, "/state")["messages"], [])

        self.assertEqual(self.ask(self.first, "/leave")["messages"], [])
        self.assertEqual(self.ask(self.second, "/state")["messages"], [])
        with self.assertRaises(LobbyError) as raised:
            self.ask(self.first, "/chat", match_id=match_id, text="Fuera de la sala")
        self.assertEqual(raised.exception.status, 409)
        new_match_id = self.pair(self.first, self.second)
        self.assertNotEqual(new_match_id, match_id)
        with self.assertRaises(LobbyError) as raised:
            self.ask(self.first, "/chat", match_id=match_id, text="Solicitud demorada")
        self.assertEqual(raised.exception.status, 409)
        self.assertEqual(self.ask(self.second, "/state")["messages"], [])

    def test_chat_rejects_invalid_payloads(self):
        match_id = self.pair(self.first, self.second)
        for payload in [
            {"text": ""}, {"text": "   "}, {"text": 123}, {"text": "uno\ndos"},
            {"text": "uno\u2028dos"}, {"text": "uno\u0000dos"}, {"text": "\ud800"},
            {"text": "á" * (MAX_CHAT_CHARS + 1)}, {"reaction": []}, {"reaction": "unknown"},
            {"text": "hola", "reaction": "jaja"}, {},
        ]:
            with self.subTest(payload=payload):
                with self.assertRaises(LobbyError) as raised:
                    self.ask(self.first, "/chat", match_id=match_id, **payload)
                self.assertEqual(raised.exception.status, 400)
        self.assertEqual(self.ask(self.second, "/state")["messages"], [])
        for key in ("jaja", "uy", "payaso", "frio", "casi", "var"):
            self.ask(self.first, "/chat", match_id=match_id, reaction=key)

    def test_chat_throttles_each_player_and_bounds_history(self):
        match_id = self.pair(self.first, self.second)
        for index in range(CHAT_BURST):
            self.ask(self.first, "/chat", match_id=match_id, text=str(index))
        with self.assertRaises(LobbyError) as raised:
            self.ask(self.first, "/chat", match_id=match_id, reaction="var")
        self.assertEqual(raised.exception.status, 429)
        self.ask(self.second, "/chat", match_id=match_id, reaction="var")

        for index in range(MAX_CHAT_MESSAGES + 2):
            self.now += CHAT_WINDOW_SECONDS
            self.ask(self.first, "/chat", match_id=match_id, text=str(index))
            history = self.ask(self.second, "/state")["messages"]
        self.assertEqual(len(history), MAX_CHAT_MESSAGES)
        self.assertEqual([message["text"] for message in history],
                         [str(index) for index in range(2, MAX_CHAT_MESSAGES + 2)])


class HttpTests(unittest.TestCase):
    def test_loopback_json_and_bearer_session(self):
        with ThreadingHTTPServer(("127.0.0.1", 0), Handler) as server:
            server.lobby = Lobby()
            worker = threading.Thread(target=server.serve_forever)
            worker.start()
            client = HTTPConnection("127.0.0.1", server.server_port, timeout=3)
            try:
                client.request("GET", "/health")
                response = client.getresponse()
                self.assertEqual(response.status, 200)
                self.assertEqual(json.load(response), {"service": "pes13-lobby", "version": 2})
                client.request("POST", "/session", json.dumps({"name": "Niñó"}))
                response = client.getresponse()
                self.assertEqual(response.status, 200)
                enrolled = json.load(response)
                client.request("GET", "/state", headers={"Authorization": f"Bearer {enrolled['token']}"})
                response = client.getresponse()
                self.assertEqual(response.status, 200)
                self.assertEqual(json.load(response)["you"]["name"], "Niñó")
                client.request("GET", "/state")
                response = client.getresponse()
                self.assertEqual(response.status, 401)
                self.assertIn("error", json.load(response))

                headers = {"Authorization": f"Bearer {enrolled['token']}"}
                client.request("POST", "/room", "{}", headers)
                room_id = json.load(client.getresponse())["rooms"][0]["id"]
                client.request("POST", "/session", json.dumps({"name": "Visitante"}))
                guest = json.load(client.getresponse())
                guest_headers = {"Authorization": f"Bearer {guest['token']}"}
                client.request("POST", "/join", json.dumps({"room_id": room_id}), guest_headers)
                match_id = json.load(client.getresponse())["match"]["id"]
                text = "\U0001f642" * MAX_CHAT_CHARS
                client.request("POST", "/chat", json.dumps({"match_id": match_id, "text": text}))
                response = client.getresponse()
                self.assertEqual(response.status, 401)
                self.assertIn("error", json.load(response))
                client.request("POST", "/chat", json.dumps({"match_id": match_id, "text": text}), headers)
                response = client.getresponse()
                self.assertEqual(response.status, 200)
                self.assertEqual(json.load(response)["messages"][0]["text"], text)
                client.request("GET", "/state", headers=guest_headers)
                response = client.getresponse()
                self.assertEqual(response.status, 200)
                self.assertEqual(json.load(response)["messages"][0]["text"], text)
            finally:
                client.close()
                server.shutdown()
                worker.join()


if __name__ == "__main__":
    unittest.main()
