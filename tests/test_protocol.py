#!/usr/bin/env python3
"""
MiniMail test suite - covers the assignment's required functionality.

Starts a fresh server on a scratch data directory, then exercises the protocol
over real TCP. Prints PASS/FAIL per check and exits non-zero on any failure.

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
    out, i = [], 0
    while i < len(text):
        if text[i] == "\\" and i + 1 < len(text):
            out.append({"\\": "\\", "p": "|", "n": "\n", "r": "\r"}.get(text[i + 1], text[i + 1]))
            i += 2
        else:
            out.append(text[i])
            i += 1
    return "".join(out)


def check(name, condition, detail=""):
    results.append((name, bool(condition), detail))
    print(f"[{'PASS' if condition else 'FAIL'}] {name}"
          + (f"  ({detail})" if detail and not condition else ""))


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
            socket.create_connection(("127.0.0.1", port), timeout=0.2).close()
            return proc
        except OSError:
            if proc.poll() is not None:
                print("server exited early:\n" + proc.stdout.read())
                sys.exit(1)
            time.sleep(0.05)
    proc.kill()
    print("server did not start")
    sys.exit(1)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--port", type=int, default=9191)
    port = ap.parse_args().port

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
    for name, _, detail in failed:
        print(f"  - {name} {detail}")
    return 1 if failed else 0


def run_tests(port):
    a = Client(port)
    check("welcome banner", a.welcome.startswith("OK WELCOME"))

    # ---- 4.1 registration ----
    check("register alice",
          a.cmd("REGISTER|alice|alice@iitlab.local|secret123").startswith("OK REGISTER"))
    check("register bob",
          a.cmd("REGISTER|bob|bob@iitlab.local|secret123").startswith("OK REGISTER"))
    check("register carol",
          a.cmd("REGISTER|carol|carol@iitlab.local|secret123").startswith("OK REGISTER"))
    check("duplicate username rejected",
          a.cmd("REGISTER|alice|x@iitlab.local|secret123").startswith("ERR USER_EXISTS"))
    check("duplicate email rejected",
          a.cmd("REGISTER|dave|alice@iitlab.local|secret123").startswith("ERR EMAIL_EXISTS"))
    check("bad email rejected",
          a.cmd("REGISTER|eve|not-an-email|secret123").startswith("ERR INVALID_EMAIL"))
    check("short password rejected",
          a.cmd("REGISTER|frank|frank@iitlab.local|123").startswith("ERR WEAK_PASSWORD"))

    # ---- 4.2 login ----
    check("wrong password rejected",
          a.cmd("LOGIN|alice|wrongpass").startswith("ERR BAD_PASSWORD"))
    check("unknown user rejected",
          a.cmd("LOGIN|nobody|secret123").startswith("ERR NO_SUCH_USER"))
    check("login alice",
          a.cmd("LOGIN|alice|secret123").startswith("OK LOGIN|alice|alice@iitlab.local|0"))

    b = Client(port)
    check("inbox requires login", b.cmd("INBOX").startswith("ERR NOT_LOGGED_IN"))
    check("send requires login", b.cmd("SEND|alice|hi|body").startswith("ERR NOT_LOGGED_IN"))

    # ---- 4.3 sending ----
    check("send alice->bob",
          a.cmd("SEND|bob|Internet Technology Lab|Please complete the experiment.")
          .startswith("OK SEND|1|bob"))
    check("send to multiple recipients",
          a.cmd("SEND|bob,carol|Group mail|Hello both").startswith("OK SEND|2|bob,carol"))
    check("unknown recipient rejected",
          a.cmd("SEND|ghost|Hello|Are you there?").startswith("ERR UNKNOWN_RECIPIENT"))
    check("empty subject rejected",
          a.cmd("SEND|bob||No subject").startswith("ERR EMPTY_SUBJECT"))
    check("empty body rejected",
          a.cmd("SEND|bob|Subject|").startswith("ERR EMPTY_BODY"))
    check("special characters survive",
          a.cmd("SEND|bob|Pipes \\p and \\\\ backslash|Line one\\nLine two with \\p pipe")
          .startswith("OK SEND|3|bob"))

    # ---- 4.4 receiving ----
    b.cmd("LOGIN|bob|secret123")
    header, recs = b.list_cmd("INBOX")
    check("inbox header", header == "OK LIST|3", header)
    check("inbox record shape", all(len(x.split("|")) == 7 for x in recs), str(recs))
    check("new mail unread", all(x.split("|")[6] == "0" for x in recs), str(recs))

    r = b.cmd("READ|1")
    check("read message 1",
          r.startswith("OK READ|1|alice|bob|Internet Technology Lab|"), r)
    check("read returns body",
          r.endswith("Please complete the experiment."), r)

    r = b.cmd("READ|3")
    decoded = unescape(r)
    check("escaped body round-trips",
          "Line one\nLine two with | pipe" in decoded
          and "Pipes | and \\ backslash" in decoded, decoded)

    header, recs = b.list_cmd("INBOX")
    flags = {x.split("|")[1]: x.split("|")[6] for x in recs}
    check("message 1 marked read", flags.get("1") == "1", str(flags))
    check("message 2 still unread", flags.get("2") == "0", str(flags))
    check("read unknown id rejected", b.cmd("READ|999").startswith("ERR NO_SUCH_MAIL"))

    # ---- 4.5 sent mail ----
    header, recs = a.list_cmd("SENT")
    check("sent header", header == "OK LIST|3", header)
    check("sent shows recipients", recs[1].split("|")[3] == "bob,carol", str(recs[1]))

    # ---- robustness ----
    check("unknown command rejected", b.cmd("BOGUS|a").startswith("ERR UNKNOWN_COMMAND"))
    check("empty request rejected", b.cmd("").startswith("ERR BAD_REQUEST"))
    b.send("X" * 20000)
    check("oversized line rejected", b.line().startswith("ERR LINE_TOO_LONG"))
    check("connection usable after oversized line",
          b.cmd("INBOX").startswith("OK LIST"))

    # ---- 4.6 multiple clients ----
    clients = []
    for i in range(3):
        c = Client(port)
        assert c.cmd(f"REGISTER|user{i}|user{i}@iitlab.local|secret123").startswith("OK")
        assert c.cmd(f"LOGIN|user{i}|secret123").startswith("OK")
        clients.append(c)

    for i, c in enumerate(clients):
        check(f"client {i} can send",
              c.cmd(f"SEND|user{(i + 1) % 3}|Concurrent {i}|Message {i}").startswith("OK SEND"))
    for i, c in enumerate(clients):
        header, _ = c.list_cmd("INBOX")
        check(f"client {i} received mail", header == "OK LIST|1", header)
    for c in clients:
        c.close()

    # ---- logout and persistence ----
    check("logout alice", a.cmd("LOGOUT").startswith("OK LOGOUT|alice"))
    check("quit acknowledged", a.cmd("QUIT").startswith("OK BYE"))
    a.close()

    e = Client(port)
    check("data survives reconnect", e.cmd("LOGIN|bob|secret123").startswith("OK LOGIN|bob"))
    header, _ = e.list_cmd("INBOX")
    check("inbox persisted", header == "OK LIST|3", header)
    e.close()


if __name__ == "__main__":
    sys.exit(main())
