// common.h - Protocol limits and helpers shared by the server and the client.
#ifndef COMMON_H
#define COMMON_H

#include <stddef.h>

#define DEFAULT_PORT      9090
#define DEFAULT_HOST      "127.0.0.1"
#define DEFAULT_DATA_DIR  "data"
#define PROTOCOL_VERSION  "MiniMail/1.0"

#define MAX_LINE        16384   /* max bytes in one protocol line */
#define MAX_BODY         4096   /* max message body */
#define MAX_USER           32   /* username buffer */
#define MAX_EMAIL         128   /* e-mail buffer */
#define MAX_SUBJECT       128   /* subject buffer */
#define MAX_TO            512   /* recipient list buffer */
#define MAX_DATE           32   /* timestamp buffer */
#define MAX_PASSWORD       65   /* password buffer */
#define MAX_RECIPIENTS     10   /* recipients per SEND */

// Framing and encoding (protocol.c)
int send_line(int fd, const char *line);
int recv_line(int fd, char *buf, size_t size);
void proto_escape(const char *in, char *out, size_t outsz);
int proto_unescape(const char *in, char *out, size_t outsz);
int proto_unescape_inplace(char *s);
int split_fields(char *line, char *fields[], int max_fields);

// Validation (protocol.c)
int valid_username(const char *s);
int valid_email(const char *s);

#endif /* COMMON_H */
