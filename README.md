# MiniMail — Multi-Client Email Service in C

A miniature email service built with TCP/IP sockets in C. One central server
handles many concurrent clients; users register, log in, send mail to one or
more recipients, read their inbox, and review sent mail. All data is persisted
to plain-text files on the server.

This project was written for the *Internet Technology Laboratory* individual
assignment (B.Tech. IT).

---

## 1. Contents

| Path | Purpose |
|------|---------|
| [server.c](server.c) | TCP server: accept loop, thread-per-client, command dispatch |
| [client.c](client.c) | Interactive menu-driven client |
| [protocol.c](protocol.c) / [common.h](common.h) | Framing, escaping, validation, shared limits |
| [storage.c](storage.c) / [storage.h](storage.h) | File-based persistence for users and mail |
| [Makefile](Makefile) | Build, test and clean targets |
| [tests/test_protocol.py](tests/test_protocol.py) | 65-check automated protocol/functional test suite |
| [tests/run_tests.sh](tests/run_tests.sh) | Build + run the suite |
| [docs/PROTOCOL.md](docs/PROTOCOL.md) | Full application-layer protocol specification |
| [docs/REPORT.md](docs/REPORT.md) | Project report (architecture, design, testing, security) |
| [docs/TEST_RESULTS.md](docs/TEST_RESULTS.md) | Test cases and recorded results |
| [docs/AI_USAGE_LOG.md](docs/AI_USAGE_LOG.md) | AI/LLM usage log |

---

## 2. Compilation

Requires `gcc` (or `clang`), `make`, and POSIX threads.

```bash
make            # builds ./server and ./client
```

Equivalent manual commands:

```bash
gcc -std=c11 -Wall -Wextra -Wpedantic -O2 -g -o server server.c storage.c protocol.c -lpthread
gcc -std=c11 -Wall -Wextra -Wpedantic -O2 -g -o client client.c protocol.c
```

The build is warning-free with `-Wall -Wextra -Wpedantic`.

---

## 3. Running the server

```bash
./server                       # listens on 0.0.0.0:9090, data in ./data
./server -p 9090               # explicit port
./server -p 9090 -d /var/mail  # explicit data directory
./server -b 127.0.0.1          # bind to loopback only
```

| Option | Meaning | Default |
|--------|---------|---------|
| `-p port` | TCP port to listen on (1–65535) | `9090` |
| `-d dir` | storage directory | `data` |
| `-b addr` | local IPv4 address to bind | `0.0.0.0` |
| `-h` | usage help | — |

The server prints a timestamped log line for every connect, register, login,
send, logout and disconnect. Stop it with `Ctrl+C` (SIGINT) or SIGTERM.

---

## 4. Running the client

```bash
./client                       # connects to 127.0.0.1:9090
./client -h 192.168.1.10 -p 9090
```

| Option | Meaning | Default |
|--------|---------|---------|
| `-h host` | server hostname or IP | `127.0.0.1` |
| `-p port` | server port | `9090` |

Menu:

```
 1. Register            6. View Sent mail
 2. Login               7. Delete a message
 3. Compose / Send      8. Mailbox statistics
 4. View Inbox          9. Logout
 5. Read a message      0. Quit
```

When composing, type the body line by line and finish with a single `.` on its
own line. Multiple recipients are comma-separated (`bob,carol`).

---

## 5. Port number

Default **9090** for both server and client. Change it with `-p` on either side;
the client must use the same port as the server.

---

## 6. Configuration

There is no config file — everything is a command-line flag:

* **Port** — `-p`
* **Bind address** — `-b` (server)
* **Data directory** — `-d` (server)
* **Server host** — `-h` (client)

Protocol limits (compile-time, in [common.h](common.h)):

| Constant | Value | Meaning |
|----------|-------|---------|
| `MAX_LINE` | 16384 | max bytes in one protocol line |
| `MAX_BODY` | 4096 | max message body |
| `MAX_SUBJECT` | 128 | max subject |
| `MAX_RECIPIENTS` | 10 | recipients per `SEND` |
| `MAX_CLIENTS` | 128 | concurrent client slots |

---

## 7. Data-file locations

Everything lives under the data directory (`./data` by default):

```
data/
├── users.dat                 username|email|password|created
├── next_id.dat               next global email id
└── mail/
    ├── alice/
    │   ├── inbox.dat         id|from|to|subject|date|read|body
    │   └── sent.dat
    └── bob/
        ├── inbox.dat
        └── sent.dat
```

Records are one per line, `|`-separated, with `\p`, `\n`, `\r` and `\\`
escapes so subjects and bodies may contain any text. Passwords are stored as
plain text in `users.dat` — this is a teaching project, not a production system
(see the security notes below).

The directory tree is created automatically on first start. Delete the data
directory to reset the service.

---

## 8. Example session

Two terminals, one server:

```bash
$ ./server -p 9090
[2026-11-02 10:00:00] MiniMail server MiniMail/1.0 listening on 0.0.0.0:9090 (data: data)
```

Client A registers and sends:

```
Choice: 1
Username: alice
Email: alice@iitlab.local
Password: secret123
[+] Registered 'alice' (alice@iitlab.local). You can now log in.

Choice: 2
Username: alice
Password: secret123
[+] Logged in as alice <alice@iitlab.local>

Choice: 3
To (comma separated usernames): bob
Subject: Internet Technology Lab
Message body (end with a single '.' on its own line):
> Please complete the socket programming experiment.
> .
[+] Message sent (id 1) to bob.
```

Client B reads it:

```
Choice: 4
-------- INBOX --------
ID    FROM         SUBJECT                      DATE                 STATUS
1     alice        Internet Technology Lab      2026-11-02 10:00:12  UNREAD

Choice: 5
Message ID: 1
============================================================
ID      : 1
From    : alice
To      : bob
Subject : Internet Technology Lab
Date    : 2026-11-02 10:00:12
------------------------------------------------------------
Please complete the socket programming experiment.
============================================================
```

Raw protocol over `netcat` (useful for demos and debugging):

```bash
$ nc 127.0.0.1 9090
OK WELCOME|MiniMail/1.0|Send HELP for the command list
REGISTER|carol|carol@iitlab.local|secret123
OK REGISTER|carol|carol@iitlab.local
LOGIN|carol|secret123
OK LOGIN|carol|carol@iitlab.local|0
SEND|alice|Hello|Body text here
OK SEND|1|alice
QUIT
OK BYE|Goodbye
```

---

## 9. Testing

```bash
make test          # or: ./tests/run_tests.sh
```

The suite starts its own server on port 9191 with a scratch data directory
(`tests/tmp/`), then runs 65 checks covering registration, authentication,
sending, inbox/sent listing, read/unread transitions, deletion, escaping,
malformed input, oversized lines, three simultaneous clients, session rules and
persistence. Expected output ends with:

```
65/65 checks passed
All automated tests passed.
```

See [docs/TEST_RESULTS.md](docs/TEST_RESULTS.md) for the full test-case table.

---

## 10. Known limitations

* **Plaintext transport.** Traffic is unencrypted; passwords cross the wire in
  the clear. TLS is listed as a future enhancement.
* **Single global mutex.** All storage access is serialised, which is fine for a
  teaching-scale workload but would not scale to thousands of clients.
* **Thread-per-client.** Each connection costs a thread; there is no thread pool
  and no idle timeout.
* **One active session per user.** A second login for the same account is
  rejected with `ERR ALREADY_ACTIVE`.
* **No attachments, folders, CC/BCC, forwarding or search.** Only inbox and sent
  boxes exist.
* **Linear file scans.** Listing and lookup read whole files; no indexes.
* **No message size beyond 4 KB body / 16 KB line**, and at most 10 recipients
  per message.
* **No delivery to offline users' clients** — mail is stored server-side and
  appears on the recipient's next `INBOX` request; there is no push.
* **IPv4 only** for the bind address (`-b`); the client can resolve IPv6.
* **No password reset or account deletion.**
* **Plaintext passwords.** `users.dat` stores passwords as typed, with no
  hashing or salting. Deliberate simplification for a teaching project.

---

## 11. Security notes

* Passwords are stored in plain text in `users.dat`. This keeps the code simple
  and the data easy to inspect during a viva, but it is **not** acceptable for a
  real system. Anyone who can read `users.dat` learns every password.
* All user input is length-checked and validated (username charset, e-mail
  shape, password length) before use.
* Field escaping prevents protocol injection: a subject containing `|` or a
  newline cannot forge extra fields or extra requests.
* `SIGPIPE` is ignored so a vanished client cannot kill the server.
* Storage writes are flushed and checked; mailbox updates use write-temp +
  `rename()` so a crash cannot leave a half-written box.
* Not implemented: transport encryption, rate limiting, account lockout after
  repeated failures, audit trail of failed logins.
