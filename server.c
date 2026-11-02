/* server.c - MiniMail email server: one pthread per client, one mutex for shared state. */
#define _POSIX_C_SOURCE 200809L

#include "common.h"
#include "storage.h"

#include <arpa/inet.h>
#include <errno.h>
#include <netinet/in.h>
#include <pthread.h>
#include <signal.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <time.h>
#include <unistd.h>

#define MAX_LIST 256

/* Global state */
static pthread_mutex_t g_lock = PTHREAD_MUTEX_INITIALIZER;
static volatile sig_atomic_t g_stop = 0;
static int g_listen_fd = -1;

/* Usernames with a live session; one entry per logged-in user. */
static char g_active[MAX_CLIENTS][MAX_USER];
static int  g_active_count = 0;

typedef struct {
    int fd;
    char peer[64];
    char username[MAX_USER];
    int logged_in;
} client_ctx;

/* Helpers */
static void now_string(char *out, size_t n)
{
    time_t t = time(NULL);
    struct tm tm;

    localtime_r(&t, &tm);
    strftime(out, n, "%Y-%m-%d %H:%M:%S", &tm);
}

static void log_msg(const char *fmt, ...)
{
    va_list ap;
    char stamp[MAX_DATE];

    now_string(stamp, sizeof(stamp));
    printf("[%s] ", stamp);
    va_start(ap, fmt);
    vprintf(fmt, ap);
    va_end(ap);
    printf("\n");
    fflush(stdout);
}

static void reply(int fd, const char *fmt, ...)
{
    char buf[MAX_LINE];
    va_list ap;

    va_start(ap, fmt);
    vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    send_line(fd, buf);
}

/* Active session table */
static int session_add(const char *user)
{
    int rc = 0;

    pthread_mutex_lock(&g_lock);
    for (int i = 0; i < g_active_count; i++) {
        if (strcmp(g_active[i], user) == 0) {
            rc = -1;
            break;
        }
    }
    if (rc == 0) {
        if (g_active_count >= MAX_CLIENTS)
            rc = -2;                    /* session table full */
        else
            snprintf(g_active[g_active_count++], MAX_USER, "%s", user);
    }
    pthread_mutex_unlock(&g_lock);
    return rc;
}

static void session_remove(const char *user)
{
    pthread_mutex_lock(&g_lock);
    for (int i = 0; i < g_active_count; i++) {
        if (strcmp(g_active[i], user) == 0) {
            g_active[i][0] = '\0';
            for (int j = i; j < g_active_count - 1; j++)
                memcpy(g_active[j], g_active[j + 1], MAX_USER);
            g_active_count--;
            break;
        }
    }
    pthread_mutex_unlock(&g_lock);
}

/* Command handlers */
static void cmd_register(client_ctx *c, char **f, int nf)
{
    user_record u;

    if (nf != 4) {
        reply(c->fd, "ERR BAD_REQUEST|REGISTER needs username, email, password");
        return;
    }
    if (!valid_username(f[1])) {
        reply(c->fd, "ERR INVALID_USERNAME|3-31 chars, start with a letter, "
                     "letters/digits/._ only");
        return;
    }
    if (!valid_email(f[2])) {
        reply(c->fd, "ERR INVALID_EMAIL|Not a valid e-mail address");
        return;
    }
    if (!valid_password(f[3])) {
        reply(c->fd, "ERR WEAK_PASSWORD|Password must be 6-64 characters");
        return;
    }

    pthread_mutex_lock(&g_lock);
    if (store_find_user(f[1], &u) == 0) {
        pthread_mutex_unlock(&g_lock);
        reply(c->fd, "ERR USER_EXISTS|Username '%s' is already registered", f[1]);
        return;
    }
    if (store_email_exists(f[2])) {
        pthread_mutex_unlock(&g_lock);
        reply(c->fd, "ERR EMAIL_EXISTS|E-mail '%s' is already registered", f[2]);
        return;
    }

    memset(&u, 0, sizeof(u));
    snprintf(u.username, sizeof(u.username), "%s", f[1]);
    snprintf(u.email, sizeof(u.email), "%s", f[2]);
    snprintf(u.password, sizeof(u.password), "%s", f[3]);
    now_string(u.created, sizeof(u.created));

    if (store_add_user(&u) < 0) {
        pthread_mutex_unlock(&g_lock);
        reply(c->fd, "ERR STORAGE|Could not persist the new account");
        return;
    }
    pthread_mutex_unlock(&g_lock);

    log_msg("REGISTER ok user=%s email=%s", u.username, u.email);
    reply(c->fd, "OK REGISTER|%s|%s", u.username, u.email);
}

static void cmd_login(client_ctx *c, char **f, int nf)
{
    user_record u;
    int unread;

    if (nf != 3) {
        reply(c->fd, "ERR BAD_REQUEST|LOGIN needs username and password");
        return;
    }
    if (c->logged_in) {
        reply(c->fd, "ERR ALREADY_LOGGED_IN|You are already logged in as %s",
              c->username);
        return;
    }

    pthread_mutex_lock(&g_lock);
    if (store_find_user(f[1], &u) < 0) {
        pthread_mutex_unlock(&g_lock);
        reply(c->fd, "ERR NO_SUCH_USER|No account named '%s'", f[1]);
        return;
    }

    if (strcmp(f[2], u.password) != 0) {
        pthread_mutex_unlock(&g_lock);
        reply(c->fd, "ERR BAD_PASSWORD|Incorrect password");
        return;
    }
    unread = store_unread_count(u.username);
    pthread_mutex_unlock(&g_lock);

    int add_rc = session_add(u.username);
    if (add_rc == -1) {
        reply(c->fd, "ERR ALREADY_ACTIVE|'%s' already has an active session",
              u.username);
        return;
    }
    if (add_rc == -2) {
        reply(c->fd, "ERR SERVER_BUSY|Too many active sessions, try again later");
        return;
    }

    snprintf(c->username, sizeof(c->username), "%s", u.username);
    c->logged_in = 1;

    log_msg("LOGIN ok user=%s from %s", u.username, c->peer);
    reply(c->fd, "OK LOGIN|%s|%s|%d", u.username, u.email, unread);
}

static void cmd_logout(client_ctx *c)
{
    if (!c->logged_in) {
        reply(c->fd, "ERR NOT_LOGGED_IN|No active session");
        return;
    }
    session_remove(c->username);
    log_msg("LOGOUT user=%s", c->username);
    reply(c->fd, "OK LOGOUT|%s", c->username);
    c->logged_in = 0;
    c->username[0] = '\0';
}

static void cmd_send(client_ctx *c, char **f, int nf)
{
    char to_list[MAX_TO];
    char recipients[MAX_RECIPIENTS][MAX_USER];
    int nrec = 0;
    mail_record m;
    long id;
    char *save = NULL;
    char *tok;

    if (!c->logged_in) {
        reply(c->fd, "ERR NOT_LOGGED_IN|Please LOGIN first");
        return;
    }
    if (nf != 4) {
        reply(c->fd, "ERR BAD_REQUEST|SEND needs recipients, subject, body");
        return;
    }
    if (f[1][0] == '\0') {
        reply(c->fd, "ERR NO_RECIPIENTS|At least one recipient is required");
        return;
    }
    if (f[2][0] == '\0') {
        reply(c->fd, "ERR EMPTY_SUBJECT|Subject must not be empty");
        return;
    }
    if (strlen(f[2]) >= MAX_SUBJECT) {
        reply(c->fd, "ERR SUBJECT_TOO_LONG|Subject exceeds %d characters",
              MAX_SUBJECT - 1);
        return;
    }
    if (f[3][0] == '\0') {
        reply(c->fd, "ERR EMPTY_BODY|Message body must not be empty");
        return;
    }
    if (strlen(f[3]) > MAX_BODY) {
        reply(c->fd, "ERR MESSAGE_TOO_LONG|Body exceeds %d bytes", MAX_BODY);
        return;
    }

    snprintf(to_list, sizeof(to_list), "%s", f[1]);
    for (tok = strtok_r(to_list, ",", &save); tok;
         tok = strtok_r(NULL, ",", &save)) {
        while (*tok == ' ')
            tok++;
        if (*tok == '\0')
            continue;
        if (nrec >= MAX_RECIPIENTS) {
            reply(c->fd, "ERR TOO_MANY_RECIPIENTS|At most %d recipients",
                  MAX_RECIPIENTS);
            return;
        }
        if (!valid_username(tok)) {
            reply(c->fd, "ERR UNKNOWN_RECIPIENT|No such user: '%s'", tok);
            return;
        }
        snprintf(recipients[nrec++], MAX_USER, "%s", tok);
    }
    if (nrec == 0) {
        reply(c->fd, "ERR NO_RECIPIENTS|At least one recipient is required");
        return;
    }

    pthread_mutex_lock(&g_lock);
    for (int i = 0; i < nrec; i++) {
        user_record u;
        if (store_find_user(recipients[i], &u) < 0) {
            pthread_mutex_unlock(&g_lock);
            reply(c->fd, "ERR UNKNOWN_RECIPIENT|No such user: '%s'",
                  recipients[i]);
            return;
        }
    }

    id = store_next_id();
    if (id < 0) {
        pthread_mutex_unlock(&g_lock);
        reply(c->fd, "ERR STORAGE|Could not allocate a message id");
        return;
    }

    memset(&m, 0, sizeof(m));
    m.id = id;
    snprintf(m.from, sizeof(m.from), "%s", c->username);
    snprintf(m.subject, sizeof(m.subject), "%s", f[2]);
    snprintf(m.body, sizeof(m.body), "%s", f[3]);
    now_string(m.date, sizeof(m.date));

    /* One copy per recipient inbox, plus one copy in the sender's sent box. */
    for (int i = 0; i < nrec; i++) {
        snprintf(m.to, sizeof(m.to), "%s", recipients[i]);
        m.read = 0;
        if (store_append_mail(recipients[i], BOX_INBOX, &m) < 0) {
            pthread_mutex_unlock(&g_lock);
            reply(c->fd, "ERR STORAGE|Could not deliver to '%s'",
                  recipients[i]);
            return;
        }
    }

    snprintf(m.to, sizeof(m.to), "%s", f[1]);
    m.read = 1;
    if (store_append_mail(c->username, BOX_SENT, &m) < 0) {
        pthread_mutex_unlock(&g_lock);
        reply(c->fd, "ERR STORAGE|Could not save a copy in Sent");
        return;
    }
    pthread_mutex_unlock(&g_lock);

    log_msg("SEND id=%ld from=%s to=%s", id, c->username, f[1]);
    reply(c->fd, "OK SEND|%ld|%s", id, f[1]);
}

static void send_list(client_ctx *c, int box)
{
    mail_record *recs;
    int n;

    recs = malloc(sizeof(*recs) * MAX_LIST);
    if (!recs) {
        reply(c->fd, "ERR STORAGE|Out of memory");
        return;
    }

    pthread_mutex_lock(&g_lock);
    n = store_list_mail(c->username, box, recs, MAX_LIST);
    pthread_mutex_unlock(&g_lock);

    reply(c->fd, "OK LIST|%d", n);
    for (int i = 0; i < n; i++) {
        char efrom[MAX_USER * 2], eto[MAX_TO * 2], esub[MAX_SUBJECT * 2];

        proto_escape(recs[i].from, efrom, sizeof(efrom));
        proto_escape(recs[i].to, eto, sizeof(eto));
        proto_escape(recs[i].subject, esub, sizeof(esub));
        reply(c->fd, "M|%ld|%s|%s|%s|%s|%d", recs[i].id, efrom, eto,
              esub, recs[i].date, recs[i].read);
    }
    reply(c->fd, "END");
    free(recs);
}

static void cmd_inbox(client_ctx *c)
{
    if (!c->logged_in) {
        reply(c->fd, "ERR NOT_LOGGED_IN|Please LOGIN first");
        return;
    }
    send_list(c, BOX_INBOX);
}

static void cmd_sent(client_ctx *c)
{
    if (!c->logged_in) {
        reply(c->fd, "ERR NOT_LOGGED_IN|Please LOGIN first");
        return;
    }
    send_list(c, BOX_SENT);
}

static void cmd_read(client_ctx *c, char **f, int nf)
{
    mail_record m;
    char ebody[MAX_BODY * 2];
    char efrom[MAX_USER * 2], eto[MAX_TO * 2], esub[MAX_SUBJECT * 2];
    long id;

    if (!c->logged_in) {
        reply(c->fd, "ERR NOT_LOGGED_IN|Please LOGIN first");
        return;
    }
    if (nf != 2) {
        reply(c->fd, "ERR BAD_REQUEST|READ needs a message id");
        return;
    }
    id = strtol(f[1], NULL, 10);
    if (id <= 0) {
        reply(c->fd, "ERR BAD_REQUEST|Invalid message id");
        return;
    }

    pthread_mutex_lock(&g_lock);
    if (store_get_mail(c->username, BOX_INBOX, id, &m) < 0) {
        pthread_mutex_unlock(&g_lock);
        reply(c->fd, "ERR NO_SUCH_MAIL|No inbox message with id %ld", id);
        return;
    }
    store_mark_read(c->username, id);
    pthread_mutex_unlock(&g_lock);

    proto_escape(m.from, efrom, sizeof(efrom));
    proto_escape(m.to, eto, sizeof(eto));
    proto_escape(m.subject, esub, sizeof(esub));
    proto_escape(m.body, ebody, sizeof(ebody));

    reply(c->fd, "OK READ|%ld|%s|%s|%s|%s|1|%s", m.id, efrom, eto, esub,
          m.date, ebody);
}

static void cmd_delete(client_ctx *c, char **f, int nf)
{
    long id;
    int box = BOX_INBOX;

    if (!c->logged_in) {
        reply(c->fd, "ERR NOT_LOGGED_IN|Please LOGIN first");
        return;
    }
    if (nf < 2 || nf > 3) {
        reply(c->fd, "ERR BAD_REQUEST|DELETE needs id [inbox|sent]");
        return;
    }
    id = strtol(f[1], NULL, 10);
    if (id <= 0) {
        reply(c->fd, "ERR BAD_REQUEST|Invalid message id");
        return;
    }
    if (nf == 3) {
        if (strcmp(f[2], "sent") == 0)
            box = BOX_SENT;
        else if (strcmp(f[2], "inbox") != 0) {
            reply(c->fd, "ERR BAD_REQUEST|Box must be 'inbox' or 'sent'");
            return;
        }
    }

    pthread_mutex_lock(&g_lock);
    if (store_delete_mail(c->username, box, id) < 0) {
        pthread_mutex_unlock(&g_lock);
        reply(c->fd, "ERR NO_SUCH_MAIL|No message with id %ld", id);
        return;
    }
    pthread_mutex_unlock(&g_lock);

    reply(c->fd, "OK DELETE|%ld", id);
}

static void cmd_stats(client_ctx *c)
{
    mail_record *recs;
    int n, unread = 0, sent = 0;

    if (!c->logged_in) {
        reply(c->fd, "ERR NOT_LOGGED_IN|Please LOGIN first");
        return;
    }

    recs = malloc(sizeof(*recs) * MAX_LIST);
    if (!recs) {
        reply(c->fd, "ERR STORAGE|Out of memory");
        return;
    }

    pthread_mutex_lock(&g_lock);
    n = store_list_mail(c->username, BOX_INBOX, recs, MAX_LIST);
    for (int i = 0; i < n; i++)
        if (!recs[i].read)
            unread++;
    sent = store_list_mail(c->username, BOX_SENT, recs, MAX_LIST);
    pthread_mutex_unlock(&g_lock);

    free(recs);
    reply(c->fd, "OK STATS|%d|%d|%d", n, unread, sent);
}

static void cmd_whoami(client_ctx *c)
{
    user_record u;

    if (!c->logged_in) {
        reply(c->fd, "ERR NOT_LOGGED_IN|No active session");
        return;
    }
    pthread_mutex_lock(&g_lock);
    int found = store_find_user(c->username, &u);
    pthread_mutex_unlock(&g_lock);

    if (found < 0) {
        reply(c->fd, "ERR STORAGE|Account record missing");
        return;
    }
    reply(c->fd, "OK WHOAMI|%s|%s", u.username, u.email);
}

/* Request dispatch */
static void handle_line(client_ctx *c, char *line)
{
    char *fields[8];
    int nf;
    char cmd[32];

    if (line[0] == '\0') {
        reply(c->fd, "ERR BAD_REQUEST|Empty request");
        return;
    }

    nf = split_fields(line, fields, 8);
    if (nf < 0) {
        reply(c->fd, "ERR BAD_REQUEST|Too many fields");
        return;
    }
    snprintf(cmd, sizeof(cmd), "%s", fields[0]);

    /* Decode escaped payload fields before the handlers see them. */
    for (int i = 1; i < nf; i++) {
        if (proto_unescape_inplace(fields[i]) < 0) {
            reply(c->fd, "ERR BAD_REQUEST|Malformed escape sequence");
            return;
        }
    }

    if (strcmp(cmd, "REGISTER") == 0) {
        cmd_register(c, fields, nf);
    } else if (strcmp(cmd, "LOGIN") == 0) {
        cmd_login(c, fields, nf);
    } else if (strcmp(cmd, "LOGOUT") == 0) {
        cmd_logout(c);
    } else if (strcmp(cmd, "SEND") == 0) {
        cmd_send(c, fields, nf);
    } else if (strcmp(cmd, "INBOX") == 0) {
        cmd_inbox(c);
    } else if (strcmp(cmd, "SENT") == 0) {
        cmd_sent(c);
    } else if (strcmp(cmd, "READ") == 0) {
        cmd_read(c, fields, nf);
    } else if (strcmp(cmd, "DELETE") == 0) {
        cmd_delete(c, fields, nf);
    } else if (strcmp(cmd, "STATS") == 0) {
        cmd_stats(c);
    } else if (strcmp(cmd, "WHOAMI") == 0) {
        cmd_whoami(c);
    } else if (strcmp(cmd, "PING") == 0) {
        reply(c->fd, "OK PONG|%s", PROTOCOL_VERSION);
    } else if (strcmp(cmd, "HELP") == 0) {
        reply(c->fd, "OK HELP|REGISTER|LOGIN|LOGOUT|SEND|INBOX|SENT|READ|"
                     "DELETE|STATS|WHOAMI|PING|QUIT");
    } else {
        reply(c->fd, "ERR UNKNOWN_COMMAND|Unrecognised command '%s'", cmd);
    }
}

/* Per-client thread */
static void *client_thread(void *arg)
{
    client_ctx *c = (client_ctx *)arg;
    char line[MAX_LINE];

    log_msg("CONNECT %s", c->peer);
    reply(c->fd, "OK WELCOME|%s|Send HELP for the command list", PROTOCOL_VERSION);

    for (;;) {
        int rc = recv_line(c->fd, line, sizeof(line));

        if (rc == 0) {
            log_msg("DISCONNECT %s (peer closed)", c->peer);
            break;
        }
        if (rc == -1) {
            log_msg("DISCONNECT %s (socket error)", c->peer);
            break;
        }
        if (rc == -2) {
            reply(c->fd, "ERR LINE_TOO_LONG|Request exceeds %d bytes", MAX_LINE);
            continue;
        }

        if (strcmp(line, "QUIT") == 0) {
            reply(c->fd, "OK BYE|Goodbye");
            log_msg("QUIT %s", c->peer);
            break;
        }

        handle_line(c, line);
    }

    if (c->logged_in)
        session_remove(c->username);

    close(c->fd);
    free(c);
    return NULL;
}

static void on_signal(int sig)
{
    (void)sig;
    g_stop = 1;
    if (g_listen_fd >= 0)
        close(g_listen_fd);
    g_listen_fd = -1;
}

static void usage(const char *prog)
{
    printf("Usage: %s [-p port] [-d data_dir] [-b bind_address]\n", prog);
    printf("  -p port          TCP port to listen on (default %d)\n", DEFAULT_PORT);
    printf("  -d data_dir      storage directory (default '%s')\n", DEFAULT_DATA_DIR);
    printf("  -b bind_address  local address to bind (default 0.0.0.0)\n");
}

int main(int argc, char **argv)
{
    int port = DEFAULT_PORT;
    const char *data_dir = DEFAULT_DATA_DIR;
    const char *bind_addr = "0.0.0.0";
    struct sockaddr_in addr;
    int opt;

    while ((opt = getopt(argc, argv, "p:d:b:h")) != -1) {
        switch (opt) {
        case 'p':
            port = atoi(optarg);
            if (port <= 0 || port > 65535) {
                fprintf(stderr, "Invalid port: %s\n", optarg);
                return 1;
            }
            break;
        case 'd':
            data_dir = optarg;
            break;
        case 'b':
            bind_addr = optarg;
            break;
        case 'h':
            usage(argv[0]);
            return 0;
        default:
            usage(argv[0]);
            return 1;
        }
    }

    signal(SIGPIPE, SIG_IGN);
    signal(SIGINT, on_signal);
    signal(SIGTERM, on_signal);

    if (store_init(data_dir) < 0) {
        fprintf(stderr, "Cannot initialise storage in '%s': %s\n",
                data_dir, strerror(errno));
        return 1;
    }

    g_listen_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (g_listen_fd < 0) {
        perror("socket");
        return 1;
    }

    int yes = 1;
    setsockopt(g_listen_fd, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(yes));

    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons((uint16_t)port);
    if (inet_pton(AF_INET, bind_addr, &addr.sin_addr) != 1) {
        fprintf(stderr, "Invalid bind address: %s\n", bind_addr);
        close(g_listen_fd);
        return 1;
    }

    if (bind(g_listen_fd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        perror("bind");
        close(g_listen_fd);
        return 1;
    }
    if (listen(g_listen_fd, 64) < 0) {
        perror("listen");
        close(g_listen_fd);
        return 1;
    }

    log_msg("MiniMail server %s listening on %s:%d (data: %s)",
            PROTOCOL_VERSION, bind_addr, port, data_dir);

    while (!g_stop) {
        struct sockaddr_in peer;
        socklen_t plen = sizeof(peer);
        int fd = accept(g_listen_fd, (struct sockaddr *)&peer, &plen);

        if (fd < 0) {
            if (errno == EINTR)
                continue;
            if (g_stop)
                break;
            perror("accept");
            continue;
        }

        client_ctx *c = calloc(1, sizeof(*c));
        if (!c) {
            close(fd);
            continue;
        }
        c->fd = fd;
        snprintf(c->peer, sizeof(c->peer), "%s:%d",
                 inet_ntoa(peer.sin_addr), ntohs(peer.sin_port));

        pthread_t tid;
        if (pthread_create(&tid, NULL, client_thread, c) != 0) {
            log_msg("ERROR cannot create thread for %s", c->peer);
            close(fd);
            free(c);
            continue;
        }
        pthread_detach(tid);
    }

    log_msg("Server shutting down");
    if (g_listen_fd >= 0)
        close(g_listen_fd);
    return 0;
}
