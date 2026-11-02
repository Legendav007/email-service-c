#!/usr/bin/env python3
"""
MiniMail protocol test suite.

Starts a fresh server on a scratch data directory, then exercises the
application-layer protocol directly over TCP. Each test prints PASS/FAIL and
the script exits non-zero if any test fails.

Usage: python3 tests/test_protocol.py [--port 9191]
"""
import argparse
import os
import shutil
import socket
import subprocess
import sys
import time

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
TMP = os.path.join(ROOT, "tests", "tmp")
DATA = os.path.join(TMP, "data")

results = []


class Client:
    def __init__(self, port, host="127.0.0.1"):
        self.sock = socket.create_connection((host, port), timeout=5)
        self.buf = b""
        self.welcome = self.line()

    def line(self):
        while b"\n" not in self.buf:
            chunk = self.sock.recv(4096)
            if not chunk:
                raise ConnectionError("server closed connection")
            self.buf += chunk
        raw, self.buf = self.buf.split(b"\n", 1)
        return raw.decode("utf-8", "replace").rstrip("\r")

    def send(self, text):
        self.sock.sendall(text.encode() + b"\n")

    def cmd(self, text):
        self.send(text)
        return self.line()

    def list_cmd(self, text):
        """For INBOX/SENT: returns (header, [records])."""
        header = self.cmd(text)
        records = []
        while True:
            ln = self.line()
            if ln == "END":
                return header, records
            records.append(ln)

    def close(self):
        try:
            self.sock.close()
        except OSError:
            pass


def unescape(text):
    """Reverse the MiniMail field escaping (\\p -> |, \\n -> newline, ...)."""
    out = []
    i = 0
    while i < len(text):
        ch = text[i]
        if ch == "\\" and i + 1 < len(text):
            nxt = text[i + 1]
            out.append({"\\": "\\", "p": "|", "n": "\n", "r": "\r"}.get(nxt, nxt))
            i += 2
        else:
            out.append(ch)
            i += 1
    return "".join(out)


def check(name, condition, detail=""):
    results.append((name, bool(condition), detail))
    status = "PASS" if condition else "FAIL"
    print(f"[{status}] {name}" + (f"  ({detail})" if detail and not condition else ""))


def start_server(port):
    os.makedirs(TMP, exist_ok=True)
    if os.path.isdir(DATA):
        shutil.rmtree(DATA)
    proc = subprocess.Popen(
        [os.path.join(ROOT, "server"), "-p", str(port), "-d", DATA],
        stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True,
    )
    for _ in range(100):
        try:
            s = socket.create_connection(("127.0.0.1", port), timeout=0.2)
            s.close()
            return proc
        except OSError:
            if proc.poll() is not None:
                out = proc.stdout.read()
                print("server exited early:\n" + out)
                sys.exit(1)
            time.sleep(0.05)
    print("server did not start")
    proc.kill()
    sys.exit(1)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--port", type=int, default=9191)
    args = ap.parse_args()
    port = args.port

    proc = start_server(port)
    try:
        run_tests(port)
    finally:
        proc.terminate()
        try:
            proc.wait(timeout=5)
        except subprocess.TimeoutExpired:
            proc.kill()

    failed = [r for r in results if not r[1]]
    print(f"\n{len(results) - len(failed)}/{len(results)} checks passed")
    if failed:
        print("FAILED:")
        for name, _, detail in failed:
            print(f"  - {name} {detail}")
        return 1
    return 0


def run_tests(port):
    # ---------------- registration ----------------
    a = Client(port)
    check("welcome banner", a.welcome.startswith("OK WELCOME"))

    r = a.cmd("REGISTER|alice|alice@iitlab.local|secret123")
    check("register alice", r.startswith("OK REGISTER"), r)

    r = a.cmd("REGISTER|bob|bob@iitlab.local|secret123")
    check("register bob", r.startswith("OK REGISTER"), r)

    r = a.cmd("REGISTER|carol|carol@iitlab.local|secret123")
    check("register carol", r.startswith("OK REGISTER"), r)

    r = a.cmd("REGISTER|alice|other@iitlab.local|secret123")
    check("duplicate username rejected", r.startswith("ERR USER_EXISTS"), r)

    r = a.cmd("REGISTER|dave|alice@iitlab.local|secret123")
    check("duplicate email rejected", r.startswith("ERR EMAIL_EXISTS"), r)

    r = a.cmd("REGISTER|ab|ab@iitlab.local|secret123")
    check("short username rejected", r.startswith("ERR INVALID_USERNAME"), r)

    r = a.cmd("REGISTER|eve|not-an-email|secret123")
    check("bad email rejected", r.startswith("ERR INVALID_EMAIL"), r)

    r = a.cmd("REGISTER|frank|frank@iitlab.local|123")
    check("weak password rejected", r.startswith("ERR WEAK_PASSWORD"), r)

    r = a.cmd("REGISTER|gina")
    check("malformed REGISTER rejected", r.startswith("ERR BAD_REQUEST"), r)

    # ---------------- login ----------------
    r = a.cmd("LOGIN|alice|wrongpass")
    check("wrong password rejected", r.startswith("ERR BAD_PASSWORD"), r)

    r = a.cmd("LOGIN|nobody|secret123")
    check("unknown user rejected", r.startswith("ERR NO_SUCH_USER"), r)

    r = a.cmd("LOGIN|alice|secret123")
    check("login alice", r.startswith("OK LOGIN|alice|alice@iitlab.local|0"), r)

    r = a.cmd("LOGIN|alice|secret123")
    check("second login on same connection rejected",
          r.startswith("ERR ALREADY_LOGGED_IN"), r)

    # ---------------- auth guards ----------------
    b = Client(port)
    r = b.cmd("INBOX")
    check("INBOX requires login", r.startswith("ERR NOT_LOGGED_IN"), r)
    r = b.cmd("SEND|alice|hi|body")
    check("SEND requires login", r.startswith("ERR NOT_LOGGED_IN"), r)

    # ---------------- send ----------------
    r = a.cmd("SEND|bob|Internet Technology Lab|Please complete the socket programming experiment.")
    check("send alice->bob", r.startswith("OK SEND|1|bob"), r)

    r = a.cmd("SEND|bob,carol|Group mail|Hello both of you")
    check("send to multiple recipients", r.startswith("OK SEND|2|bob,carol"), r)

    r = a.cmd("SEND|ghost|Hello|Are you there?")
    check("unknown recipient rejected", r.startswith("ERR UNKNOWN_RECIPIENT"), r)

    r = a.cmd("SEND|bob||No subject")
    check("empty subject rejected", r.startswith("ERR EMPTY_SUBJECT"), r)

    r = a.cmd("SEND|bob|Subject|")
    check("empty body rejected", r.startswith("ERR EMPTY_BODY"), r)

    r = a.cmd("SEND||Subject|Body")
    check("empty recipient rejected", r.startswith("ERR NO_RECIPIENTS"), r)

    r = a.cmd("SEND|" + "x" * 200 + "|Subject|Body")
    check("over-long recipient rejected", r.startswith("ERR UNKNOWN_RECIPIENT"), r)

    r = a.cmd("SEND|bob|" + "S" * 200 + "|Body")
    check("over-long subject rejected", r.startswith("ERR SUBJECT_TOO_LONG"), r)

    # special characters must survive escaping
    r = a.cmd("SEND|bob|Pipes \\p and \\\\ backslash|Line one\\nLine two with \\p pipe")
    check("send with escaped specials", r.startswith("OK SEND|3|bob"), r)

    # ---------------- inbox ----------------
    b.cmd("LOGIN|bob|secret123")
    header, recs = b.list_cmd("INBOX")
    check("inbox header", header == "OK LIST|3", header)
    check("inbox has 3 messages", len(recs) == 3, str(len(recs)))
    check("inbox record shape", all(len(x.split("|")) == 7 for x in recs), str(recs))
    check("inbox unread flags", all(x.split("|")[6] == "0" for x in recs), str(recs))

    r = b.cmd("READ|1")
    check("read message 1", r.startswith("OK READ|1|alice|bob|Internet Technology Lab|"), r)
    check("read returns body", r.endswith("Please complete the socket programming experiment."), r)

    r = b.cmd("READ|3")
    decoded = unescape(r)
    check("escaped body round-trips",
          "Line one\nLine two with | pipe" in decoded
          and "Pipes | and \\ backslash" in decoded, decoded)

    header, recs = b.list_cmd("INBOX")
    read_flags = {x.split("|")[1]: x.split("|")[6] for x in recs}
    check("message 1 marked read", read_flags.get("1") == "1", str(read_flags))
    check("message 2 still unread", read_flags.get("2") == "0", str(read_flags))

    r = b.cmd("READ|999")
    check("read unknown id rejected", r.startswith("ERR NO_SUCH_MAIL"), r)

    r = b.cmd("READ|abc")
    check("read invalid id rejected", r.startswith("ERR BAD_REQUEST"), r)

    # ---------------- sent ----------------
    header, recs = a.list_cmd("SENT")
    check("sent header", header == "OK LIST|3", header)
    check("sent has 3 messages", len(recs) == 3, str(len(recs)))
    check("sent shows recipients",
          recs[1].split("|")[3] == "bob,carol", str(recs[1]))

    # ---------------- stats ----------------
    r = b.cmd("STATS")
    check("stats for bob", r == "OK STATS|3|1|0", r)

    r = a.cmd("STATS")
    check("stats for alice", r == "OK STATS|0|0|3", r)

    # ---------------- delete ----------------
    r = b.cmd("DELETE|2|inbox")
    check("delete inbox message", r.startswith("OK DELETE|2"), r)
    header, recs = b.list_cmd("INBOX")
    check("inbox shrinks after delete", header == "OK LIST|2", header)

    r = b.cmd("DELETE|2|inbox")
    check("delete missing message rejected", r.startswith("ERR NO_SUCH_MAIL"), r)

    r = b.cmd("DELETE|1|trash")
    check("delete bad box rejected", r.startswith("ERR BAD_REQUEST"), r)

    # ---------------- protocol robustness ----------------
    r = b.cmd("BOGUS|a|b")
    check("unknown command rejected", r.startswith("ERR UNKNOWN_COMMAND"), r)

    r = b.cmd("")
    check("empty request rejected", r.startswith("ERR BAD_REQUEST"), r)

    r = b.cmd("PING")
    check("ping works", r.startswith("OK PONG"), r)

    r = b.cmd("HELP")
    check("help lists commands", "SEND" in r and "INBOX" in r, r)

    r = b.cmd("SEND|a|b|c|d|e|f|g|h|i")
    check("too many fields rejected", r.startswith("ERR BAD_REQUEST"), r)

    b.send("X" * 20000)
    r = b.line()
    check("oversized line rejected", r.startswith("ERR LINE_TOO_LONG"), r)
    r = b.cmd("PING")
    check("connection usable after oversized line", r.startswith("OK PONG"), r)

    # ---------------- concurrency ----------------
    clients = []
    for i in range(3):
        c = Client(port)
        r = c.cmd(f"REGISTER|user{i}|user{i}@iitlab.local|secret123")
        assert r.startswith("OK REGISTER"), r
        r = c.cmd(f"LOGIN|user{i}|secret123")
        assert r.startswith("OK LOGIN"), r
        clients.append(c)

    for i, c in enumerate(clients):
        r = c.cmd(f"SEND|user{(i + 1) % 3}|Concurrent {i}|Message from client {i}")
        check(f"concurrent client {i} can send", r.startswith("OK SEND"), r)

    for i, c in enumerate(clients):
        header, recs = c.list_cmd("INBOX")
        check(f"concurrent client {i} received mail", header == "OK LIST|1", header)

    for c in clients:
        c.close()

    # ---------------- session rules ----------------
    d = Client(port)
    r = d.cmd("LOGIN|alice|secret123")
    check("alice already active elsewhere", r.startswith("ERR ALREADY_ACTIVE"), r)

    r = a.cmd("LOGOUT")
    check("logout alice", r.startswith("OK LOGOUT|alice"), r)

    r = d.cmd("LOGIN|alice|secret123")
    check("login allowed after logout", r.startswith("OK LOGIN|alice"), r)

    r = d.cmd("QUIT")
    check("quit acknowledged", r.startswith("OK BYE"), r)
    d.close()

    # ---------------- persistence ----------------
    r = b.cmd("LOGOUT")
    check("logout bob", r.startswith("OK LOGOUT|bob"), r)
    b.close()

    e = Client(port)
    r = e.cmd("LOGIN|bob|secret123")
    check("data survives reconnect", r.startswith("OK LOGIN|bob"), r)
    header, recs = e.list_cmd("INBOX")
    check("inbox persisted", header == "OK LIST|2", header)
    e.close()

    a.close()


if __name__ == "__main__":
    sys.exit(main())
