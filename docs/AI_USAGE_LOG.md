# AI / LLM Usage Log

Assignment policy permits and encourages LLM assistance, provided the student
understands and can explain the submitted code. This log records the significant
interactions, what the assistant produced, and how each output was verified.

**Tool used:** GitHub Copilot (VS Code) — an LLM-based coding assistant.
**Verification method for every entry:** compile with
`gcc -std=c11 -Wall -Wextra -Wpedantic -O2 -g`, run `make test` (44 automated
checks), and exercise the interactive client manually.

---

| # | Date | Task | Prompt / question | What was used | Student verification |
|---|------|------|-------------------|---------------|----------------------|
| 1 | 2026-11-02 | Socket API basics | "Explain the sequence socket → bind → listen → accept → recv/send → close and what each call returns on error." | Explanation only | Re-read `man 2 socket`, `man 2 accept`; confirmed return values and `errno` handling in `server.c` |
| 2 | 2026-11-02 | Concurrency choice | "Compare thread-per-client, select(), poll() and fork() for a small TCP server. Which is easiest to explain in a viva?" | Concept / recommendation | Chose pthreads; wrote the rationale in REPORT §10.2; verified 3 simultaneous clients in the test suite |
| 3 | 2026-11-02 | Protocol design | "Design a line-oriented text protocol for email with subjects and bodies that may contain `\|` and newlines." | Design suggestion (escaping scheme) | Implemented `proto_escape`/`proto_unescape`; added the `escaped body round-trips` test |
| 4 | 2026-11-02 | Framing implementation | "Write a `recv_line()` that reads until `\n`, strips `\r`, and reports overflow without desynchronising the stream." | Code suggestion | Reviewed the drain-on-overflow logic; tested with a 20 000-byte line and confirmed the connection still answers `INBOX` |
| 5 | 2026-11-02 | Password storage | "How should I store passwords in a file-based C app without a crypto library?" | Concept + SHA-256 implementation sketch | Implemented `sha256.c` and verified it against `sha256sum`. **Later removed** on request: the assignment does not require hashing, so the final version stores passwords as plain text to keep the code simple. The trade-off is documented in REPORT §15/§16 |
| 6 | 2026-11-02 | Storage layout | "Suggest a simple file layout for users and per-user inbox/sent boxes." | Design suggestion | Adopted `users.dat` + `mail/<user>/{inbox,sent}.dat`; inspected files with `cat` after a live session |
| 7 | 2026-11-02 | Atomic mailbox update | "How do I mark one record as read in a text file safely?" | Concept (write temp + rename) | Implemented `rewrite_box()`; verified read/unread transitions and deletion in tests |
| 8 | 2026-11-02 | Compiler errors | "Fix `implicit declaration of strcasecmp` and `va_start` under `-std=c11`." | Code fix | Added `<strings.h>` and `<stdarg.h>`; rebuild is warning-free |
| 9 | 2026-11-02 | Buffer warnings | "Fix `-Wformat-truncation` warnings on `snprintf` into fixed buffers." | Code fix | Bounded the body copy with `%.*s`; rebuild is warning-free |
| 10 | 2026-11-02 | Test matrix | "List test cases for registration, login, send, inbox, read, sent, delete, malformed input and concurrency." | Test case list | Implemented as `tests/test_protocol.py`; all 44 checks pass |
| 11 | 2026-11-02 | Bug found by tests | "The `escaped body round-trips` test fails: a subject containing `\|` is split into extra fields." | Diagnosis + fix (`proto_unescape_inplace` applied to payload fields in `handle_line`) | Confirmed the root cause (client escaped, server never unescaped); applied the fix; test now passes; re-ran the full suite |
| 12 | 2026-11-02 | Documentation | "Draft a README with compilation, execution, port, configuration, data locations, examples and limitations." | Draft text | Rewrote in own words; verified every command by running it |
| 13 | 2026-11-02 | Report structure | "Suggest a structure for the project report matching the assignment's recommended sections." | Outline | Followed the assignment's 19-section structure; wrote the content from the actual implementation |
| 14 | 2026-11-02 | Error handling review | "Review the server for paths where a bad client request could crash it." | Review comments | Added `SIGPIPE` ignore, per-connection error paths, `pthread_create` failure handling; tested with binary garbage and abrupt disconnects |

---

## Notes on responsible use

* No AI-generated code was submitted without being compiled, run and tested.
* The one substantive defect introduced by an AI suggestion (missing server-side
  unescaping, entry 11) was found by the automated tests, not by inspection, and
  is documented rather than hidden.
* All test results in [TEST_RESULTS.md](TEST_RESULTS.md) come from actual runs on
  this machine; no results or screenshots were fabricated.
* The student is responsible for being able to explain every function in
  `server.c`, `client.c`, `protocol.c` and `storage.c`.
