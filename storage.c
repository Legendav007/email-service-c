// storage.c - File based persistence for users and mailboxes.
#define _POSIX_C_SOURCE 200809L

#include "storage.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <time.h>

static char g_data_dir[512];

// Builds the path of users.dat.
static void path_users(char *out, size_t n)
{
    snprintf(out, n, "%s/users.dat", g_data_dir);
}

// Builds the path of next_id.dat.
static void path_next_id(char *out, size_t n)
{
    snprintf(out, n, "%s/next_id.dat", g_data_dir);
}

// Builds the path of a user's inbox.dat or sent.dat.
static void path_box(const char *owner, int box, char *out, size_t n)
{
    snprintf(out, n, "%s/mail/%s/%s.dat", g_data_dir, owner,
             box == BOX_SENT ? "sent" : "inbox");
}

// Creates a directory and any missing parents.
static int mkdir_p(const char *path)
{
    char tmp[512];
    size_t len;

    snprintf(tmp, sizeof(tmp), "%s", path);
    len = strlen(tmp);
    if (len && tmp[len - 1] == '/')
        tmp[len - 1] = '\0';

    for (char *p = tmp + 1; *p; p++) {
        if (*p == '/') {
            *p = '\0';
            if (mkdir(tmp, 0755) < 0 && errno != EEXIST)
                return -1;
            *p = '/';
        }
    }
    if (mkdir(tmp, 0755) < 0 && errno != EEXIST)
        return -1;
    return 0;
}

// Creates the data directory tree and the empty files on first run.
int store_init(const char *data_dir)
{
    char path[600];

    snprintf(g_data_dir, sizeof(g_data_dir), "%s", data_dir);
    if (mkdir_p(g_data_dir) < 0)
        return -1;

    snprintf(path, sizeof(path), "%s/mail", g_data_dir);
    if (mkdir_p(path) < 0)
        return -1;

    path_users(path, sizeof(path));
    FILE *f = fopen(path, "a");
    if (!f)
        return -1;
    fclose(f);

    path_next_id(path, sizeof(path));
    f = fopen(path, "a");
    if (!f)
        return -1;
    fclose(f);
    return 0;
}

// Parses one users.dat line into a user_record.
static int parse_user_line(char *line, user_record *u)
{
    char *fields[8];

    if (split_fields(line, fields, 8) != 4)
        return -1;
    snprintf(u->username, sizeof(u->username), "%s", fields[0]);
    snprintf(u->email, sizeof(u->email), "%s", fields[1]);
    snprintf(u->password, sizeof(u->password), "%s", fields[2]);
    snprintf(u->created, sizeof(u->created), "%s", fields[3]);
    return 0;
}

// Scans users.dat for a username; returns 0 if found.
int store_find_user(const char *username, user_record *out)
{
    char path[600], line[MAX_LINE];
    FILE *f;

    path_users(path, sizeof(path));
    f = fopen(path, "r");
    if (!f)
        return -1;

    while (fgets(line, sizeof(line), f)) {
        line[strcspn(line, "\r\n")] = '\0';
        if (line[0] && parse_user_line(line, out) == 0 &&
            strcmp(out->username, username) == 0) {
            fclose(f);
            return 0;
        }
    }
    fclose(f);
    return -1;
}

// Returns 1 if the e-mail address is already registered.
int store_email_exists(const char *email)
{
    char path[600], line[MAX_LINE];
    user_record u;
    FILE *f;

    path_users(path, sizeof(path));
    f = fopen(path, "r");
    if (!f)
        return 0;

    while (fgets(line, sizeof(line), f)) {
        line[strcspn(line, "\r\n")] = '\0';
        if (line[0] && parse_user_line(line, &u) == 0 &&
            strcasecmp(u.email, email) == 0) {
            fclose(f);
            return 1;
        }
    }
    fclose(f);
    return 0;
}

// Appends a user and creates their mailbox directory and box files.
int store_add_user(const user_record *u)
{
    char path[600], box[600];
    FILE *f;

    path_users(path, sizeof(path));
    f = fopen(path, "a");
    if (!f)
        return -1;
    fprintf(f, "%s|%s|%s|%s\n", u->username, u->email, u->password, u->created);
    fclose(f);

    snprintf(box, sizeof(box), "%s/mail/%s", g_data_dir, u->username);
    if (mkdir_p(box) < 0)
        return -1;

    for (int b = 0; b < 2; b++) {
        path_box(u->username, b, path, sizeof(path));
        f = fopen(path, "a");
        if (!f)
            return -1;
        fclose(f);
    }
    return 0;
}

// Returns the next global message id and increments the counter file.
long store_next_id(void)
{
    char path[600];
    long id = 1;
    FILE *f;

    path_next_id(path, sizeof(path));
    f = fopen(path, "r");
    if (f) {
        if (fscanf(f, "%ld", &id) != 1)
            id = 1;
        fclose(f);
    }
    if (id < 1)
        id = 1;

    f = fopen(path, "w");
    if (!f)
        return -1;
    fprintf(f, "%ld\n", id + 1);
    fclose(f);
    return id;
}

// Parses one mailbox line into a mail_record, unescaping the text fields.
static int parse_mail_line(char *line, mail_record *m)
{
    char *fields[8];

    if (split_fields(line, fields, 8) != 7)
        return -1;
    m->id = strtol(fields[0], NULL, 10);
    if (proto_unescape(fields[1], m->from, sizeof(m->from)) < 0 ||
        proto_unescape(fields[2], m->to, sizeof(m->to)) < 0 ||
        proto_unescape(fields[3], m->subject, sizeof(m->subject)) < 0 ||
        proto_unescape(fields[6], m->body, sizeof(m->body)) < 0)
        return -1;
    snprintf(m->date, sizeof(m->date), "%s", fields[4]);
    m->read = atoi(fields[5]);
    return 0;
}

// Appends one message to a user's inbox or sent box.
int store_append_mail(const char *owner, int box, const mail_record *m)
{
    char path[600];
    char efrom[MAX_USER * 2], eto[MAX_TO * 2];
    char esub[MAX_SUBJECT * 2], ebody[MAX_BODY * 2];
    FILE *f;

    path_box(owner, box, path, sizeof(path));
    f = fopen(path, "a");
    if (!f)
        return -1;

    proto_escape(m->from, efrom, sizeof(efrom));
    proto_escape(m->to, eto, sizeof(eto));
    proto_escape(m->subject, esub, sizeof(esub));
    proto_escape(m->body, ebody, sizeof(ebody));
    fprintf(f, "%ld|%s|%s|%s|%s|%d|%s\n", m->id, efrom, eto, esub,
            m->date, m->read, ebody);
    fclose(f);
    return 0;
}

// Loads up to max messages from a box; returns how many were read.
int store_list_mail(const char *owner, int box, mail_record *out, int max)
{
    char path[600], line[MAX_LINE];
    int count = 0;
    FILE *f;

    path_box(owner, box, path, sizeof(path));
    f = fopen(path, "r");
    if (!f)
        return 0;

    while (count < max && fgets(line, sizeof(line), f)) {
        line[strcspn(line, "\r\n")] = '\0';
        if (line[0] && parse_mail_line(line, &out[count]) == 0)
            count++;
    }
    fclose(f);
    return count;
}

// Finds one message by id in a box; returns 0 if found.
int store_get_mail(const char *owner, int box, long id, mail_record *out)
{
    char path[600], line[MAX_LINE];
    FILE *f;

    path_box(owner, box, path, sizeof(path));
    f = fopen(path, "r");
    if (!f)
        return -1;

    while (fgets(line, sizeof(line), f)) {
        line[strcspn(line, "\r\n")] = '\0';
        if (line[0] && parse_mail_line(line, out) == 0 && out->id == id) {
            fclose(f);
            return 0;
        }
    }
    fclose(f);
    return -1;
}

// Rewrites the inbox with that message flagged read.
int store_mark_read(const char *owner, long id)
{
    char path[600], tmp[620], line[MAX_LINE];
    char efrom[MAX_USER * 2], eto[MAX_TO * 2];
    char esub[MAX_SUBJECT * 2], ebody[MAX_BODY * 2];
    FILE *in, *out;
    int found = 0;

    path_box(owner, BOX_INBOX, path, sizeof(path));
    snprintf(tmp, sizeof(tmp), "%s.tmp", path);

    in = fopen(path, "r");
    if (!in)
        return -1;
    out = fopen(tmp, "w");
    if (!out) {
        fclose(in);
        return -1;
    }

    while (fgets(line, sizeof(line), in)) {
        mail_record m;

        line[strcspn(line, "\r\n")] = '\0';
        if (line[0] == '\0' || parse_mail_line(line, &m) < 0)
            continue;
        if (m.id == id) {
            m.read = 1;
            found = 1;
        }
        proto_escape(m.from, efrom, sizeof(efrom));
        proto_escape(m.to, eto, sizeof(eto));
        proto_escape(m.subject, esub, sizeof(esub));
        proto_escape(m.body, ebody, sizeof(ebody));
        fprintf(out, "%ld|%s|%s|%s|%s|%d|%s\n", m.id, efrom, eto, esub,
                m.date, m.read, ebody);
    }

    fclose(in);
    fclose(out);
    if (rename(tmp, path) < 0)
        return -1;
    return found ? 0 : -1;
}
