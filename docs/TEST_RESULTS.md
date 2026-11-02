# MiniMail — Test Cases and Results

All results below were produced by running the automated suite on this machine:

```bash
$ make test
== Building ==
== Protocol / functional tests (port 9191) ==
...
65/65 checks passed
All automated tests passed.
```

The suite ([tests/test_protocol.py](../tests/test_protocol.py)) starts a fresh
server on port 9191 with a scratch data directory, drives the protocol over real
TCP sockets, and asserts on the exact response lines.

---

## 1. Test environment

| Item | Value |
|------|-------|
| OS | Linux (x86-64) |
| Compiler | gcc, `-std=c11 -Wall -Wextra -Wpedantic -O2 -g` |
| Server | `./server -p 9191 -d tests/tmp/data` |
| Client under test | raw TCP sockets (protocol level) + interactive `./client` (manual) |
| Date of run | 2026-11-02 |

---

## 2. Registration (TC-01 … TC-09)

| ID | Test case | Input | Expected | Actual | Result |
|----|-----------|-------|----------|--------|--------|
| TC-01 | Welcome banner on connect | connect | `OK WELCOME\|MiniMail/1.0\|...` | as expected | PASS |
| TC-02 | Register valid user | `REGISTER\|alice\|alice@iitlab.local\|secret123` | `OK REGISTER\|alice\|alice@iitlab.local` | as expected | PASS |
| TC-03 | Register second user | `REGISTER\|bob\|bob@iitlab.local\|secret123` | `OK REGISTER` | as expected | PASS |
| TC-04 | Register third user | `REGISTER\|carol\|carol@iitlab.local\|secret123` | `OK REGISTER` | as expected | PASS |
| TC-05 | Duplicate username | `REGISTER\|alice\|other@iitlab.local\|secret123` | `ERR USER_EXISTS` | as expected | PASS |
| TC-06 | Duplicate e-mail | `REGISTER\|dave\|alice@iitlab.local\|secret123` | `ERR EMAIL_EXISTS` | as expected | PASS |
| TC-07 | Username too short | `REGISTER\|ab\|ab@iitlab.local\|secret123` | `ERR INVALID_USERNAME` | as expected | PASS |
| TC-08 | Malformed e-mail | `REGISTER\|eve\|not-an-email\|secret123` | `ERR INVALID_EMAIL` | as expected | PASS |
| TC-09 | Weak password | `REGISTER\|frank\|frank@iitlab.local\|123` | `ERR WEAK_PASSWORD` | as expected | PASS |
| TC-10 | Missing fields | `REGISTER\|gina` | `ERR BAD_REQUEST` | as expected | PASS |

---

## 3. Authentication (TC-11 … TC-16)

| ID | Test case | Input | Expected | Actual | Result |
|----|-----------|-------|----------|--------|--------|
| TC-11 | Wrong password | `LOGIN\|alice\|wrongpass` | `ERR BAD_PASSWORD` | as expected | PASS |
| TC-12 | Non-existent user | `LOGIN\|nobody\|secret123` | `ERR NO_SUCH_USER` | as expected | PASS |
| TC-13 | Valid login | `LOGIN\|alice\|secret123` | `OK LOGIN\|alice\|alice@iitlab.local\|0` | as expected | PASS |
| TC-14 | Login twice on one connection | `LOGIN\|alice\|secret123` again | `ERR ALREADY_LOGGED_IN` | as expected | PASS |
| TC-15 | Same account on second connection | `LOGIN\|alice\|secret123` from client D | `ERR ALREADY_ACTIVE` | as expected | PASS |
| TC-16 | Login after logout | `LOGOUT` then `LOGIN\|alice\|secret123` | `OK LOGIN` | as expected | PASS |

---

## 4. Authorisation guards (TC-17 … TC-18)

| ID | Test case | Input | Expected | Result |
|----|-----------|-------|----------|--------|
| TC-17 | Inbox without login | `INBOX` | `ERR NOT_LOGGED_IN` | PASS |
| TC-18 | Send without login | `SEND\|alice\|hi\|body` | `ERR NOT_LOGGED_IN` | PASS |

---

## 5. Sending mail (TC-19 … TC-26)

| ID | Test case | Input | Expected | Result |
|----|-----------|-------|----------|--------|
| TC-19 | Single recipient | `SEND\|bob\|Internet Technology Lab\|Please complete...` | `OK SEND\|1\|bob` | PASS |
| TC-20 | Multiple recipients | `SEND\|bob,carol\|Group mail\|Hello both of you` | `OK SEND\|2\|bob,carol` | PASS |
| TC-21 | Unknown recipient | `SEND\|ghost\|Hello\|Are you there?` | `ERR UNKNOWN_RECIPIENT` | PASS |
| TC-22 | Empty subject | `SEND\|bob\|\|No subject` | `ERR EMPTY_SUBJECT` | PASS |
| TC-23 | Empty body | `SEND\|bob\|Subject\|` | `ERR EMPTY_BODY` | PASS |
| TC-24 | Empty recipient list | `SEND\|\|Subject\|Body` | `ERR NO_RECIPIENTS` | PASS |
| TC-24b | Over-long recipient name | `SEND\|` + 200 `x` + `\|Subject\|Body` | `ERR UNKNOWN_RECIPIENT` | PASS |
| TC-24c | Over-long subject | `SEND\|bob\|` + 200 `S` + `\|Body` | `ERR SUBJECT_TOO_LONG` | PASS |
| TC-25 | Special characters | subject `Pipes \p and \\ backslash`, body with `\n` and `\p` | `OK SEND\|3\|bob` | PASS |
| TC-26 | Too many fields | `SEND\|a\|b\|c\|d\|e\|f\|g\|h\|i` | `ERR BAD_REQUEST` | PASS |

---

## 6. Inbox and reading (TC-27 … TC-36)

| ID | Test case | Expected | Result |
|----|-----------|----------|--------|
| TC-27 | Inbox header | `OK LIST\|3` | PASS |
| TC-28 | Inbox record count | 3 records | PASS |
| TC-29 | Record shape | 7 `\|`-separated fields per record | PASS |
| TC-30 | New mail is unread | all `read` flags `0` | PASS |
| TC-31 | Read message 1 | `OK READ\|1\|alice\|bob\|Internet Technology Lab\|...` | PASS |
| TC-32 | Body returned intact | body text matches what was sent | PASS |
| TC-33 | Escaped body round-trips | `Line one\nLine two with \| pipe` and `Pipes \| and \\ backslash` decoded correctly | PASS |
| TC-34 | Read marks message read | flag for id 1 becomes `1` | PASS |
| TC-35 | Other messages stay unread | flag for id 2 remains `0` | PASS |
| TC-36 | Read unknown id | `ERR NO_SUCH_MAIL` | PASS |
| TC-37 | Read non-numeric id | `ERR BAD_REQUEST` | PASS |

---

## 7. Sent mail and statistics (TC-38 … TC-42)

| ID | Test case | Expected | Result |
|----|-----------|----------|--------|
| TC-38 | Sent header | `OK LIST\|3` | PASS |
| TC-39 | Sent record count | 3 records | PASS |
| TC-40 | Sent shows all recipients | `to` field is `bob,carol` for the group mail | PASS |
| TC-41 | Stats for recipient | `OK STATS\|3\|1\|0` | PASS |
| TC-42 | Stats for sender | `OK STATS\|0\|0\|3` | PASS |

---

## 8. Deletion (TC-43 … TC-45)

| ID | Test case | Expected | Result |
|----|-----------|----------|--------|
| TC-43 | Delete inbox message | `OK DELETE\|2`, inbox shrinks to 2 | PASS |
| TC-44 | Delete missing message | `ERR NO_SUCH_MAIL` | PASS |
| TC-45 | Delete with invalid box | `ERR BAD_REQUEST` | PASS |

---

## 9. Protocol robustness (TC-46 … TC-52)

| ID | Test case | Expected | Result |
|----|-----------|----------|--------|
| TC-46 | Unknown command | `ERR UNKNOWN_COMMAND` | PASS |
| TC-47 | Empty request line | `ERR BAD_REQUEST` | PASS |
| TC-48 | `PING` | `OK PONG\|MiniMail/1.0` | PASS |
| TC-49 | `HELP` | command list containing `SEND` and `INBOX` | PASS |
| TC-50 | 20 000-byte line | `ERR LINE_TOO_LONG` | PASS |
| TC-51 | Connection usable after oversized line | `PING` still answers `OK PONG` | PASS |
| TC-52 | Malformed escape `\q` | `ERR BAD_REQUEST\|Malformed escape sequence` | PASS |

---

## 10. Concurrency (TC-53 … TC-58)

Three clients registered and logged in simultaneously, each sending to the next.

| ID | Test case | Expected | Result |
|----|-----------|----------|--------|
| TC-53 | Client 0 sends while 1 and 2 are connected | `OK SEND` | PASS |
| TC-54 | Client 1 sends | `OK SEND` | PASS |
| TC-55 | Client 2 sends | `OK SEND` | PASS |
| TC-56 | Client 0 receives | `OK LIST\|1` | PASS |
| TC-57 | Client 1 receives | `OK LIST\|1` | PASS |
| TC-58 | Client 2 receives | `OK LIST\|1` | PASS |

---

## 11. Session and persistence (TC-59 … TC-63)

| ID | Test case | Expected | Result |
|----|-----------|----------|--------|
| TC-59 | `QUIT` acknowledged | `OK BYE\|Goodbye` | PASS |
| TC-60 | `LOGOUT` releases the session | `OK LOGOUT\|bob` | PASS |
| TC-61 | Reconnect after logout | `OK LOGIN\|bob` | PASS |
| TC-62 | Inbox survives reconnect | `OK LIST\|2` | PASS |
| TC-63 | Accounts survive reconnect | login succeeds with the original password | PASS |

---

## 12. Manual / interactive tests

Performed with the real `./client` binary against `./server -p 9301`.

| ID | Test case | Steps | Observed | Result |
|----|-----------|-------|----------|--------|
| MT-01 | Register via menu | choice 1, fill fields | `[+] Registered 'alice' (alice@iitlab.local).` | PASS |
| MT-02 | Login via menu | choice 2 | `[+] Logged in as alice <alice@iitlab.local>` | PASS |
| MT-03 | Compose multi-line body | choice 3, body lines, `.` | `[+] Message sent (id 1) to bob.` | PASS |
| MT-04 | Sent view | choice 6 | table with `TO`, `SUBJECT`, `DATE` | PASS |
| MT-05 | Inbox view | choice 4 as bob | table with `FROM`, `SUBJECT`, `DATE`, `UNREAD` | PASS |
| MT-06 | Read message | choice 5, id 1 | full header + body, `\|` preserved | PASS |
| MT-07 | Stats | choice 8 | `Inbox: 1 message(s), 0 unread \| Sent: 0 message(s)` | PASS |
| MT-08 | Logout | choice 9 | `[+] Logged out.` | PASS |
| MT-09 | Quit | choice 0 | `[+] Disconnected.` | PASS |

---

## 13. Error-handling tests (manual)

| ID | Scenario | Command | Observed | Result |
|----|----------|---------|----------|--------|
| EH-01 | Server unavailable | `./client -p 9999` | `[!] Could not connect to 127.0.0.1:9999: Connection refused` | PASS |
| EH-02 | Invalid port | `./server -p 70000` | `Invalid port: 70000`, exit 1 | PASS |
| EH-03 | Invalid bind address | `./server -b 999.1.1.1` | `Invalid bind address: 999.1.1.1` | PASS |
| EH-04 | Port already in use | second `./server -p 9301` | `bind: Address already in use` | PASS |
| EH-05 | Unwritable data dir | `./server -d /proc/nope` | `Cannot initialise storage in '/proc/nope': No such file or directory` | PASS |
| EH-06 | Abrupt client disconnect while logged in | close socket after `LOGIN` | server logs `DISCONNECT ... (peer closed)`, session released, next login succeeds | PASS |
| EH-07 | Binary garbage request | `\x00\x01\x02garbage\n` | `ERR BAD_REQUEST\|Empty request`, connection stays open | PASS |
| EH-08 | Server survives all of the above | `PING` afterwards | `OK PONG` | PASS |

---

## 14. Summary

| Category | Checks | Passed |
|----------|--------|--------|
| Registration | 10 | 10 |
| Authentication | 6 | 6 |
| Authorisation guards | 2 | 2 |
| Sending | 8 | 8 |
| Inbox / reading | 11 | 11 |
| Sent / statistics | 5 | 5 |
| Deletion | 3 | 3 |
| Protocol robustness | 7 | 7 |
| Concurrency | 6 | 6 |
| Session / persistence | 5 | 5 |
| **Automated total** | **65** | **65** |
| Manual interactive | 9 | 9 |
| Manual error handling | 8 | 8 |

No failures. The server never crashed or exited during any test, including
malformed, oversized and abruptly terminated requests.
