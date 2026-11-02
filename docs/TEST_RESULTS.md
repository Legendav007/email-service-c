# MiniMail — Test Cases and Results

Produced by running the automated suite on this machine:

```bash
$ make test
== Building ==
== Protocol / functional tests (port 9191) ==
...
44/44 checks passed
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

## 2. Registration (TC-01 … TC-07)

| ID | Test case | Input | Expected | Result |
|----|-----------|-------|----------|--------|
| TC-01 | Welcome banner on connect | connect | `OK WELCOME\|MiniMail/1.0\|Connected` | PASS |
| TC-02 | Register valid user | `REGISTER\|alice\|alice@iitlab.local\|secret123` | `OK REGISTER\|alice\|alice@iitlab.local` | PASS |
| TC-03 | Register second user | `REGISTER\|bob\|bob@iitlab.local\|secret123` | `OK REGISTER` | PASS |
| TC-04 | Register third user | `REGISTER\|carol\|carol@iitlab.local\|secret123` | `OK REGISTER` | PASS |
| TC-05 | Duplicate username | `REGISTER\|alice\|x@iitlab.local\|secret123` | `ERR USER_EXISTS` | PASS |
| TC-06 | Duplicate e-mail | `REGISTER\|dave\|alice@iitlab.local\|secret123` | `ERR EMAIL_EXISTS` | PASS |
| TC-07 | Malformed e-mail | `REGISTER\|eve\|not-an-email\|secret123` | `ERR INVALID_EMAIL` | PASS |
| TC-08 | Short password | `REGISTER\|frank\|frank@iitlab.local\|123` | `ERR WEAK_PASSWORD` | PASS |

---

## 3. Authentication (TC-09 … TC-12)

| ID | Test case | Input | Expected | Result |
|----|-----------|-------|----------|--------|
| TC-09 | Wrong password | `LOGIN\|alice\|wrongpass` | `ERR BAD_PASSWORD` | PASS |
| TC-10 | Non-existent user | `LOGIN\|nobody\|secret123` | `ERR NO_SUCH_USER` | PASS |
| TC-11 | Valid login | `LOGIN\|alice\|secret123` | `OK LOGIN\|alice\|alice@iitlab.local\|0` | PASS |
| TC-12 | Login twice on one connection | `LOGIN\|alice\|secret123` again | `ERR ALREADY_LOGGED_IN` | PASS |

---

## 4. Authorisation guards (TC-13 … TC-14)

| ID | Test case | Input | Expected | Result |
|----|-----------|-------|----------|--------|
| TC-13 | Inbox without login | `INBOX` | `ERR NOT_LOGGED_IN` | PASS |
| TC-14 | Send without login | `SEND\|alice\|hi\|body` | `ERR NOT_LOGGED_IN` | PASS |

---

## 5. Sending mail (TC-15 … TC-20)

| ID | Test case | Input | Expected | Result |
|----|-----------|-------|----------|--------|
| TC-15 | Single recipient | `SEND\|bob\|Internet Technology Lab\|Please complete the experiment.` | `OK SEND\|1\|bob` | PASS |
| TC-16 | Multiple recipients | `SEND\|bob,carol\|Group mail\|Hello both` | `OK SEND\|2\|bob,carol` | PASS |
| TC-17 | Unknown recipient | `SEND\|ghost\|Hello\|Are you there?` | `ERR UNKNOWN_RECIPIENT` | PASS |
| TC-18 | Empty subject | `SEND\|bob\|\|No subject` | `ERR EMPTY_SUBJECT` | PASS |
| TC-19 | Empty body | `SEND\|bob\|Subject\|` | `ERR EMPTY_BODY` | PASS |
| TC-20 | Special characters | subject `Pipes \p and \\ backslash`, body with `\n` and `\p` | `OK SEND\|3\|bob` | PASS |

---

## 6. Inbox and reading (TC-21 … TC-31)

| ID | Test case | Expected | Result |
|----|-----------|----------|--------|
| TC-21 | Inbox header | `OK LIST\|3` | PASS |
| TC-22 | Record shape | 7 `\|`-separated fields per record | PASS |
| TC-23 | New mail is unread | all `read` flags `0` | PASS |
| TC-24 | Read message 1 | `OK READ\|1\|alice\|bob\|Internet Technology Lab\|...` | PASS |
| TC-25 | Body returned intact | body text matches what was sent | PASS |
| TC-26 | Escaped body round-trips | `Line one\nLine two with \| pipe` and `Pipes \| and \\ backslash` decoded correctly | PASS |
| TC-27 | Read marks message read | flag for id 1 becomes `1` | PASS |
| TC-28 | Other messages stay unread | flag for id 2 remains `0` | PASS |
| TC-29 | Read unknown id | `ERR NO_SUCH_MAIL` | PASS |

---

## 7. Sent mail (TC-30 … TC-32)

| ID | Test case | Expected | Result |
|----|-----------|----------|--------|
| TC-30 | Sent header | `OK LIST\|3` | PASS |
| TC-31 | Sent record count | 3 records | PASS |
| TC-32 | Sent shows all recipients | `to` field is `bob,carol` for the group mail | PASS |

---

## 8. Protocol robustness (TC-33 … TC-36)

| ID | Test case | Expected | Result |
|----|-----------|----------|--------|
| TC-33 | Unknown command | `ERR UNKNOWN_COMMAND` | PASS |
| TC-34 | Empty request line | `ERR BAD_REQUEST` | PASS |
| TC-35 | 20 000-byte line | `ERR LINE_TOO_LONG` | PASS |
| TC-36 | Connection usable after oversized line | `INBOX` still answers `OK LIST` | PASS |

---

## 9. Concurrency (TC-37 … TC-42)

Three clients registered and logged in simultaneously, each sending to the next.

| ID | Test case | Expected | Result |
|----|-----------|----------|--------|
| TC-38 | Client 0 sends while 1 and 2 are connected | `OK SEND` | PASS |
| TC-39 | Client 1 sends | `OK SEND` | PASS |
| TC-40 | Client 2 sends | `OK SEND` | PASS |
| TC-41 | Client 0 receives | `OK LIST\|1` | PASS |
| TC-42 | Client 1 receives | `OK LIST\|1` | PASS |
| TC-43 | Client 2 receives | `OK LIST\|1` | PASS |

---

## 10. Session and persistence (TC-43 … TC-46)

| ID | Test case | Expected | Result |
|----|-----------|----------|--------|
| TC-44 | `LOGOUT` | `OK LOGOUT\|alice` | PASS |
| TC-45 | `QUIT` acknowledged | `OK BYE\|Goodbye` | PASS |
| TC-46 | Reconnect after logout | `OK LOGIN\|bob` | PASS |
| TC-47 | Inbox survives reconnect | `OK LIST\|3` | PASS |

---

## 11. Manual / interactive tests

Performed with the real `./client` binary against `./server -p 9701`. These are
the checks that mirror the live demonstration.

| ID | Test case | Steps | Observed | Result |
|----|-----------|-------|----------|--------|
| MT-01 | Register via menu | choice 1, fill fields | `[+] Registered 'alice' (alice@iitlab.local).` | PASS |
| MT-02 | Login via menu | choice 2 | `[+] Logged in as alice <alice@iitlab.local>` | PASS |
| MT-03 | Compose multi-line body | choice 3, body lines, `.` | `[+] Message sent (id 1) to bob.` | PASS |
| MT-04 | Sent view | choice 6 | table with `TO`, `SUBJECT`, `DATE` | PASS |
| MT-05 | Inbox view | choice 4 as bob | table with `FROM`, `SUBJECT`, `DATE`, `UNREAD` | PASS |
| MT-06 | Read message | choice 5, id 1 | full header + body, `\|` preserved | PASS |
| MT-07 | Logout and quit | choices 7, 0 | `[+] Logged out.` then `[+] Disconnected.` | PASS |
| MT-08 | Three clients at once | three `./client` windows, all logged in | all three send and receive while connected | PASS |

---

## 12. Error-handling tests (manual)

| ID | Scenario | Command | Observed | Result |
|----|----------|---------|----------|--------|
| EH-01 | Server unavailable | `./client -p 9999` | `[!] Could not connect to 127.0.0.1:9999: Connection refused` | PASS |
| EH-02 | Invalid port | `./server -p 70000` | `Invalid port: 70000`, exit 1 | PASS |
| EH-03 | Invalid bind address | `./server -b 999.1.1.1` | `Invalid bind address: 999.1.1.1` | PASS |
| EH-04 | Port already in use | second `./server -p 9701` | `bind: Address already in use` | PASS |
| EH-05 | Unwritable data dir | `./server -d /proc/nope` | `Cannot initialise storage in '/proc/nope': ...` | PASS |
| EH-06 | Abrupt client disconnect | close socket after `LOGIN` | server logs `DISCONNECT`, keeps serving | PASS |

---

## 13. Summary

| Category | Checks | Passed |
|----------|--------|--------|
| Registration | 8 | 8 |
| Authentication | 4 | 4 |
| Authorisation guards | 2 | 2 |
| Sending | 6 | 6 |
| Inbox / reading | 9 | 9 |
| Sent mail | 3 | 3 |
| Protocol robustness | 4 | 4 |
| Concurrency | 6 | 6 |
| Session / persistence | 4 | 4 |
| **Automated total** | **44** | **44** |
| Manual interactive | 8 | 8 |
| Manual error handling | 6 | 6 |

No failures. The server never crashed or exited during any test, including
malformed, oversized and abruptly terminated requests.

---

## 14. Defects found and fixed

| ID | Symptom | Cause | Fix |
|----|---------|-------|-----|
| D-01 | Client segfaulted (`core dumped`) when the server returned an `ERR` reply, e.g. registering with an invalid e-mail | `print_error()` declared `char *fields[4]` uninitialized and printed `fields[1]`/`fields[2]`. `split_fields()` fills only `fields[0]` and `fields[1]` for a two-field reply, so `fields[2]` was read as a wild pointer | Zero-initialized the array (`= {0}`) and corrected the indices to `fields[0]`/`fields[1]` |
| D-02 | Same latent crash in the `SEND` success path | `do_send()` used an uninitialized `char *fields[4]` | Zero-initialized the array |

Both defects were in the client's response-display code only. The automated
suite drives the protocol directly over TCP and never calls these functions,
which is why it did not catch them; they were found during manual interactive
testing. After the fix, every `ERR` reply renders as
`[!] ERR <CODE>: <message>` and the suite still passes 44/44.
