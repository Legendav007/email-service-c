// protocol.c - Line framing, field escaping and input validation for MiniMail.
#define _POSIX_C_SOURCE 200809L

#include "common.h"

#include <ctype.h>
#include <errno.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

// Sends one line followed by "\n", retrying on partial writes.
int send_line(int fd, const char *line)
{
    size_t len = strlen(line) + 1;
    char buf[MAX_LINE + 1];

    if (len > sizeof(buf))
        return -1;
    memcpy(buf, line, len - 1);
    buf[len - 1] = '\n';

    for (size_t off = 0; off < len; ) {
        ssize_t n = send(fd, buf + off, len - off, 0);
        if (n <= 0) {
            if (n < 0 && errno == EINTR)
                continue;
            return -1;
        }
        off += (size_t)n;
    }
    return 0;
}

// Reads one line: 1 = ok, 0 = closed, -1 = error, -2 = line too long.
int recv_line(int fd, char *buf, size_t size)
{
    size_t i = 0;
    int overflow = 0;

    for (;;) {
        char ch;
        ssize_t n = recv(fd, &ch, 1, 0);

        if (n == 0) {
            buf[i] = '\0';
            return i ? 1 : 0;
        }
        if (n < 0) {
            if (errno == EINTR)
                continue;
            return -1;
        }
        if (ch == '\n') {
            if (i > 0 && buf[i - 1] == '\r')
                i--;
            buf[i] = '\0';
            return overflow ? -2 : 1;
        }
        if (i + 1 < size)
            buf[i++] = ch;
        else
            overflow = 1;   /* keep draining so the stream stays in sync */
    }
}

// Escapes \, |, CR and LF so a field can travel inside one line.
void proto_escape(const char *in, char *out, size_t outsz)
{
    size_t o = 0;

    for (const char *p = in; *p && o + 2 < outsz; p++) {
        switch (*p) {
        case '\\': out[o++] = '\\'; out[o++] = '\\'; break;
        case '|':  out[o++] = '\\'; out[o++] = 'p';  break;
        case '\n': out[o++] = '\\'; out[o++] = 'n';  break;
        case '\r': out[o++] = '\\'; out[o++] = 'r';  break;
        default:   out[o++] = *p;                    break;
        }
    }
    out[o] = '\0';
}

// Decodes escapes in place; returns -1 on an unknown escape.
int proto_unescape_inplace(char *s)
{
    char *out = s;

    for (const char *p = s; *p; p++) {
        char ch = *p;

        if (ch == '\\') {
            switch (*++p) {
            case '\\': ch = '\\'; break;
            case 'p':  ch = '|';  break;
            case 'n':  ch = '\n'; break;
            case 'r':  ch = '\r'; break;
            default:   return -1;
            }
        }
        *out++ = ch;
    }
    *out = '\0';
    return 0;
}

// Decodes escapes into a separate buffer; returns -1 if it does not fit.
int proto_unescape(const char *in, char *out, size_t outsz)
{
    size_t o = 0;

    for (const char *p = in; *p; p++) {
        char ch = *p;

        if (ch == '\\') {
            switch (*++p) {
            case '\\': ch = '\\'; break;
            case 'p':  ch = '|';  break;
            case 'n':  ch = '\n'; break;
            case 'r':  ch = '\r'; break;
            default:   return -1;
            }
        }
        if (o + 1 >= outsz)
            return -1;
        out[o++] = ch;
    }
    out[o] = '\0';
    return 0;
}

// Splits a line on "|" in place and returns the field count.
int split_fields(char *line, char *fields[], int max_fields)
{
    int n = 0;

    fields[n++] = line;
    for (char *p = line; *p; p++) {
        if (*p == '|') {
            *p = '\0';
            if (n >= max_fields)
                return -1;
            fields[n++] = p + 1;
        }
    }
    return n;
}

// Checks length and allowed characters for a username.
int valid_username(const char *s)
{
    size_t len = strlen(s);

    if (len < 3 || len >= MAX_USER || !isalpha((unsigned char)s[0]))
        return 0;
    for (size_t i = 0; i < len; i++) {
        unsigned char c = (unsigned char)s[i];
        if (!isalnum(c) && c != '_' && c != '.')
            return 0;
    }
    return 1;
}

// Checks that an address looks like local@domain.tld.
int valid_email(const char *s)
{
    const char *at = strchr(s, '@');

    if (strlen(s) >= MAX_EMAIL || !at || at == s || !strchr(at + 1, '.'))
        return 0;
    for (const char *p = s; *p; p++)
        if (isspace((unsigned char)*p) || *p == '|' || *p == '\\')
            return 0;
    return 1;
}
