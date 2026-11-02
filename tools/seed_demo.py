#!/usr/bin/env python3
"""
seed_demo.py - populate a fresh data directory with demo accounts and mail.

Useful for a quick demonstration or for the viva: it creates three users and a
few messages so the inbox/sent views are not empty.

Usage:
    python3 tools/seed_demo.py [--port 9090] [--host 127.0.0.1]

The server must already be running. Existing accounts are skipped.
"""
import argparse
import socket
import sys

USERS = [
    ("alice", "alice@iitlab.local", "secret123"),
    ("bob", "bob@iitlab.local", "secret123"),
    ("carol", "carol@iitlab.local", "secret123"),
]

MAIL = [
    ("alice", "bob", "Internet Technology Lab",
     "Please complete the socket programming experiment.\nSubmit the report by Friday."),
    ("alice", "bob,carol", "Group project",
     "Let us meet in the lab at 10 AM.\nBring your laptops."),
    ("bob", "alice", "Re: Internet Technology Lab",
     "Done. I tested it with three clients at once."),
    ("carol", "alice", "Question about the protocol",
     "How do you handle a '|' inside the subject line?"),
]


class Conn:
    def __init__(self, host, port):
        self.sock = socket.create_connection((host, port), timeout=5)
        self.buf = b""
        self.line()

    def line(self):
        while b"\n" not in self.buf:
            chunk = self.sock.recv(4096)
            if not chunk:
                raise ConnectionError("server closed the connection")
            self.buf += chunk
        raw, self.buf = self.buf.split(b"\n", 1)
        return raw.decode().rstrip("\r")

    def cmd(self, text):
        self.sock.sendall(text.encode() + b"\n")
        return self.line()

    def close(self):
        try:
            self.cmd("QUIT")
        except OSError:
            pass
        self.sock.close()


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--host", default="127.0.0.1")
    ap.add_argument("--port", type=int, default=9090)
    args = ap.parse_args()

    try:
        c = Conn(args.host, args.port)
    except OSError as exc:
        print(f"Cannot connect to {args.host}:{args.port}: {exc}")
        return 1

    for user, email, password in USERS:
        r = c.cmd(f"REGISTER|{user}|{email}|{password}")
        if r.startswith("OK"):
            print(f"created {user}")
        elif "USER_EXISTS" in r:
            print(f"skipped {user} (already exists)")
        else:
            print(f"failed  {user}: {r}")

    for sender, to, subject, body in MAIL:
        r = c.cmd(f"LOGIN|{sender}|secret123")
        if not r.startswith("OK"):
            print(f"cannot log in as {sender}: {r}")
            continue
        escaped = body.replace("\\", "\\\\").replace("|", "\\p").replace("\n", "\\n")
        r = c.cmd(f"SEND|{to}|{subject}|{escaped}")
        print(f"{sender} -> {to}: {r}")
        c.cmd("LOGOUT")

    c.close()
    print("\nDemo data ready. Log in as alice / bob / carol with password 'secret123'.")
    return 0


if __name__ == "__main__":
    sys.exit(main())
