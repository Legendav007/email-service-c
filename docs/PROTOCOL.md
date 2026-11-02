# MiniMail Application-Layer Protocol — Specification v1.0

MiniMail uses a **line-oriented, text-based request/response protocol** on top of
a TCP connection. It is deliberately simple so that it can be read, typed by
hand over `netcat`, and parsed with a few dozen lines of C.

---

## 1. Transport

| Property | Value |
|----------|-------|
| Transport | TCP |
| Default port | 9090 |
| Direction | Client initiates; server never sends unsolicited data |
| Encoding | UTF-8 / ASCII bytes |
| Connection lifetime | One TCP connection per client session; multiple requests per connection |

---

## 2. Framing

* **One request = one line.** A line ends with `\n` (LF). A trailing `\r` is
  tolerated and stripped, so `telnet`/`nc` on any platform works.
* **One response = one line**, same rule.
* **Maximum line length: 16 384 bytes** (`MAX_LINE`). A longer line is drained
  and answered with `ERR LINE_TOO_LONG`; the connection stays usable.
* Multi-line results (`INBOX`, `SENT`) are sent as a header line, then one line
  per record, then a terminator line `END`.

---

## 3. Field separators and escaping

Fields inside a line are separated by the pipe character `|`.

Because `|` is the separator, any literal `|`, `\`, CR or LF inside a field is
escaped by the sender:

| Literal | Encoded as |
|---------|-----------|
| `\` | `\\` |
| `\|` | `\p` |
| newline | `\n` |
| carriage return | `\r` |

The receiver decodes these sequences before using the value. An unknown escape
(`\q`) is a protocol error and yields `ERR BAD_REQUEST|Malformed escape sequence`.

This is what allows a subject such as `Lab report | final` or a multi-line body
to travel inside a single line without breaking the framing.

---

## 4. Request format

```
COMMAND[|field1[|field2[|field3]]]
```

* `COMMAND` is an uppercase keyword.
* Field count is fixed per command; a wrong count yields `ERR BAD_REQUEST`.
* Commands are case-sensitive.

---

## 5. Response format

Success:

```
OK <CODE>[|field...]
```

Failure:

```
ERR <CODE>|<human readable message>
```

The first token after `OK`/`ERR` is a machine-readable code; the remaining text
is meant for humans and may change. Clients should branch on the code, not the
message.

---

## 6. Commands

### 6.1 `REGISTER`

```
REGISTER|<username>|<email>|<password>
```

| Field | Rules |
|-------|-------|
| username | 3–31 chars, starts with a letter, then letters/digits/`.`/`_` |
| email | `local@domain.tld`, no spaces, `< 128` chars |
| password | at least 6 chars, stored as plain text on the server |

Responses:

```
OK REGISTER|<username>|<email>
ERR BAD_REQUEST|REGISTER needs username, email, password
ERR INVALID_USERNAME|...
ERR INVALID_EMAIL|...
ERR WEAK_PASSWORD|...
ERR USER_EXISTS|Username 'alice' is already registered
ERR EMAIL_EXISTS|E-mail 'alice@iitlab.local' is already registered
```

### 6.2 `LOGIN`

```
LOGIN|<username>|<password>
```

Responses:

```
OK LOGIN|<username>|<email>|<unread_count>
ERR BAD_REQUEST|LOGIN needs username and password
ERR NO_SUCH_USER|No account named 'x'
ERR BAD_PASSWORD|Incorrect password
ERR ALREADY_LOGGED_IN|Already logged in as alice
```

A second `LOGIN` on the same connection is rejected. The session ends on
`LOGOUT`, `QUIT`, or disconnect.

### 6.3 `LOGOUT`

```
LOGOUT
```

```
OK LOGOUT|<username>
ERR NOT_LOGGED_IN|No active session
```

### 6.4 `SEND`

```
SEND|<recipients>|<subject>|<body>
```

* `recipients` — comma-separated usernames, 1–10 entries.
* `subject` — non-empty.
* `body` — non-empty; newlines encoded as `\n`.

Responses:

```
OK SEND|<id>|<recipients>
ERR NOT_LOGGED_IN|Please LOGIN first
ERR BAD_REQUEST|SEND needs recipients, subject, body
ERR NO_RECIPIENTS|At least one recipient is required
ERR EMPTY_SUBJECT|Subject must not be empty
ERR EMPTY_BODY|Message body must not be empty
ERR TOO_MANY_RECIPIENTS|At most 10 recipients
ERR UNKNOWN_RECIPIENT|No such user: 'ghost'
```

Every recipient is validated before anything is written. One copy goes to each
recipient inbox, plus one copy in the sender's sent box.

### 6.5 `INBOX` / `SENT`

```
INBOX
SENT
```

Response is a multi-line block:

```
OK LIST|<count>
M|<id>|<from>|<to>|<subject>|<date>|<read>
M|...
END
```

`read` is `1` for read, `0` for unread. In `SENT` the `read` flag is always `1`
and the `to` field may list several recipients.

Errors: `ERR NOT_LOGGED_IN`.

### 6.6 `READ`

```
READ|<id>
```

```
OK READ|<id>|<from>|<to>|<subject>|<date>|1|<body>
ERR BAD_REQUEST|READ needs a message id
ERR NO_SUCH_MAIL|No inbox message with id 42
```

Reading an inbox message marks it read as a side effect.

### 6.7 `QUIT`

```
QUIT
```

```
OK BYE|Goodbye
```

The server closes the connection after replying.

---

## 7. Connection banner

Immediately after `accept()`, the server sends:

```
OK WELCOME|MiniMail/1.0|Connected
```

---

## 8. Error catalogue

| Code | Meaning |
|------|---------|
| `BAD_REQUEST` | Wrong field count, empty line, invalid id, malformed escape |
| `LINE_TOO_LONG` | Request exceeded `MAX_LINE` |
| `UNKNOWN_COMMAND` | Command keyword not recognised |
| `INVALID_USERNAME` / `INVALID_EMAIL` / `WEAK_PASSWORD` | Registration validation |
| `USER_EXISTS` / `EMAIL_EXISTS` | Duplicate registration |
| `NO_SUCH_USER` | Login for an unknown account |
| `BAD_PASSWORD` | Wrong password |
| `ALREADY_LOGGED_IN` | Second `LOGIN` on the same connection |
| `NOT_LOGGED_IN` | Command requires authentication |
| `NO_RECIPIENTS` / `EMPTY_SUBJECT` / `EMPTY_BODY` / `TOO_MANY_RECIPIENTS` | `SEND` validation |
| `UNKNOWN_RECIPIENT` | Recipient account does not exist |
| `NO_SUCH_MAIL` | Message id not found in the inbox |
| `STORAGE` | Server-side file I/O failure |

---

## 9. Limits summary

| Item | Limit |
|------|-------|
| Line length | 16 384 bytes |
| Body | 4 096 bytes |
| Subject | 127 chars |
| Username | 31 chars |
| E-mail | 127 chars |
| Password | at least 6 chars |
| Recipients per message | 10 |
| Concurrent clients | limited by threads |

---

## 10. Example exchange

```
S: OK WELCOME|MiniMail/1.0|Connected
C: REGISTER|alice|alice@iitlab.local|secret123
S: OK REGISTER|alice|alice@iitlab.local
C: LOGIN|alice|secret123
S: OK LOGIN|alice|alice@iitlab.local|0
C: SEND|bob|Lab report \p final|Line one\nLine two
S: OK SEND|1|bob
C: SENT
S: OK LIST|1
S: M|1|alice|bob|Lab report \p final|2026-11-02 10:00:12|1
S: END
C: QUIT
S: OK BYE|Goodbye
```
