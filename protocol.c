/* protocol.c - Line framing, field escaping and input validation for MiniMail. */
#define _POSIX_C_SOURCE 200809L

#include "common.h"

#include <ctype.h>
#include <errno.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

static int send_all(int fd, const char *buf, size_t len)
{
    size_t off = 0;

    while (off < len) {
        ssize_t n = send(fd, buf + off, len - off, 0);
        if (n < 0) {
            if (errno == EINTR)
                continue;
            return -1;
        }
        if (n == 0)
            return -1;
        off += (size_t)n;
    }
    return 0;
}

int send_line(int fd, const char *line)
{
    if (send_all(fd, line, strlen(line)) < 0)
        return -1;
    if (send_all(fd, "\n", 1) < 0)
        return -1;
    return 0;
}

int recv_line(int fd, char *buf, size_t size)
{
    size_t i = 0;
    int overflow = 0;

    if (size == 0)
        return -1;

    for (;;) {
        char ch;
        ssize_t n = recv(fd, &ch, 1, 0);

        if (n == 0) {
            if (i == 0 && !overflow)
                return 0;
            buf[i] = '\0';
            return overflow ? -2 : 1;
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

void proto_escape(const char *in, char *out, size_t outsz)
{
    size_t o = 0;

    if (outsz == 0)
        return;

    for (const char *p = in; *p; p++) {
        const char *rep = NULL;

        switch (*p) {
        case '\\': rep = "\\\\"; break;
        case '|':  rep = "\\p";  break;
        case '\n': rep = "\\n";  break;
        case '\r': rep = "\\r";  break;
        default:   break;
        }

        if (rep) {
            if (o + 2 >= outsz)
                break;
            out[o++] = rep[0];
            out[o++] = rep[1];
        } else {
            if (o + 1 >= outsz)
                break;
            out[o++] = *p;
        }
    }
    out[o] = '\0';
}

int proto_unescape(const char *in, char *out, size_t outsz)
{
    size_t o = 0;

    if (outsz == 0)
        return -1;

    for (const char *p = in; *p; p++) {
        char ch = *p;

        if (ch == '\\') {
            p++;
            if (*p == '\0')
                return -1;
            switch (*p) {
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

int proto_unescape_inplace(char *s)
{
    char *out = s;

    for (const char *p = s; *p; p++) {
        char ch = *p;

        if (ch == '\\') {
            p++;
            if (*p == '\0')
                return -1;
            switch (*p) {
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

int split_fields(char *line, char *fields[], int max_fields)
{
    int n = 0;
    char *p = line;

    if (max_fields <= 0)
        return 0;

    fields[n++] = p;
    while (*p) {
        if (*p == '|') {
            *p = '\0';
            if (n >= max_fields)
                return -1;
            fields[n++] = p + 1;
        }
        p++;
    }
    return n;
}

int valid_username(const char *s)
{
    size_t len;

    if (!s)
        return 0;
    len = strlen(s);
    if (len < 3 || len >= MAX_USER)
        return 0;
    if (!isalpha((unsigned char)s[0]))
        return 0;
    for (size_t i = 0; i < len; i++) {
        unsigned char c = (unsigned char)s[i];
        if (!isalnum(c) && c != '_' && c != '.')
            return 0;
    }
    return 1;
}

int valid_email(const char *s)
{
    const char *at;
    size_t len;

    if (!s)
        return 0;
    len = strlen(s);
    if (len < 5 || len >= MAX_EMAIL)
        return 0;

    at = strchr(s, '@');
    if (!at || at == s)
        return 0;
    if (strchr(at + 1, '@'))
        return 0;
    if (!strchr(at + 1, '.'))
        return 0;
    if (at[1] == '.' || s[len - 1] == '.')
        return 0;

    for (const char *p = s; *p; p++) {
        unsigned char c = (unsigned char)*p;
        if (isspace(c) || c == '|' || c == '\\' || c == ',' || c < 0x21)
            return 0;
    }
    return 1;
}

int valid_password(const char *s)
{
    size_t len;

    if (!s)
        return 0;
    len = strlen(s);
    if (len < 6 || len > 64)
        return 0;
    for (const char *p = s; *p; p++) {
        if (*p == '|' || *p == '\n' || *p == '\r')
            return 0;
    }
    return 1;
}
