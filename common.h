/* common.h - Protocol limits and helpers shared by the server and the client. */
#ifndef COMMON_H
#define COMMON_H

#include <stddef.h>

/* Defaults */
#define DEFAULT_PORT      9090
#define DEFAULT_HOST      "127.0.0.1"
#define DEFAULT_DATA_DIR  "data"
#define PROTOCOL_VERSION  "MiniMail/1.0"

/* Protocol limits */
#define MAX_LINE        16384   /* maximum length of one protocol line     */
#define MAX_BODY         4096   /* maximum message body length (bytes)     */
#define MAX_USER           32   /* username buffer size                    */
#define MAX_EMAIL         128   /* e-mail address buffer size              */
#define MAX_SUBJECT       128   /* subject buffer size                     */
#define MAX_TO            512   /* recipient list buffer size              */
#define MAX_DATE           32   /* timestamp buffer size                   */
#define MAX_RECIPIENTS     10   /* recipients allowed per SEND             */
#define MAX_PASSWORD       65   /* password buffer size                    */
#define MAX_CLIENTS       128   /* concurrent client slots                 */

/* Framing / encoding helpers (protocol.c) */

/* Writes "line\n" to fd. Returns 0 on success, -1 on error. */
int send_line(int fd, const char *line);

/* Reads one line into buf: 1 = ok, 0 = peer closed, -1 = error, -2 = too long. */
int recv_line(int fd, char *buf, size_t size);

/* Escapes '\', '|', '\n' and '\r' so a field can travel inside one line. */
void proto_escape(const char *in, char *out, size_t outsz);

/* Reverses proto_escape(). Returns 0 on success, -1 on malformed input. */
int proto_unescape(const char *in, char *out, size_t outsz);

/* Same as proto_unescape() but decodes in place (output never grows). */
int proto_unescape_inplace(char *s);

/* Splits a line on '|' in place. Returns field count, or -1 if too many. */
int split_fields(char *line, char *fields[], int max_fields);

/* Validation helpers. Return 1 when valid, 0 otherwise. */
int valid_username(const char *s);
int valid_email(const char *s);
int valid_password(const char *s);

#endif /* COMMON_H */
