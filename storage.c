/* storage.c - File based persistence for users and mailboxes. */
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
#include <unistd.h>

static char g_data_dir[512];

static void path_users(char *out, size_t n)
{
    snprintf(out, n, "%s/users.dat", g_data_dir);
}

static void path_next_id(char *out, size_t n)
{
    snprintf(out, n, "%s/next_id.dat", g_data_dir);
}

static void path_box(const char *owner, int box, char *out, size_t n)
{
    snprintf(out, n, "%s/mail/%s/%s.dat", g_data_dir, owner,
             box == BOX_SENT ? "sent" : "inbox");
}

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

/* Users */

static int parse_user_line(char *line, user_record *u)
{
    char *fields[8];
    int n = split_fields(line, fields, 8);

    if (n != 4)
        return -1;

    snprintf(u->username, sizeof(u->username), "%s", fields[0]);
    snprintf(u->email, sizeof(u->email), "%s", fields[1]);
    snprintf(u->password, sizeof(u->password), "%s", fields[2]);
    snprintf(u->created, sizeof(u->created), "%s", fields[3]);
    return 0;
}

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
        if (line[0] == '\0')
            continue;
        if (parse_user_line(line, out) == 0 &&
            strcmp(out->username, username) == 0) {
            fclose(f);
            return 0;
        }
    }
    fclose(f);
    return -1;
}

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
        if (line[0] == '\0')
            continue;
        if (parse_user_line(line, &u) == 0 &&
            strcasecmp(u.email, email) == 0) {
            fclose(f);
            return 1;
        }
    }
    fclose(f);
    return 0;
}

int store_add_user(const user_record *u)
{
    char path[600], box[600];
    FILE *f;

    path_users(path, sizeof(path));
    f = fopen(path, "a");
    if (!f)
        return -1;

    fprintf(f, "%s|%s|%s|%s\n", u->username, u->email,
            u->password, u->created);
    if (fflush(f) != 0 || ferror(f)) {
        fclose(f);
        return -1;
    }
    fclose(f);

    /* Create the user's mailbox directory and empty box files. */
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

/* Mail */

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

static int parse_mail_line(char *line, mail_record *m)
{
    char *fields[8];
    char tmp[MAX_BODY * 2 + 1];
    int n = split_fields(line, fields, 8);

    if (n != 7)
        return -1;

    m->id = strtol(fields[0], NULL, 10);
    if (proto_unescape(fields[1], m->from, sizeof(m->from)) < 0)
        return -1;
    if (proto_unescape(fields[2], m->to, sizeof(m->to)) < 0)
        return -1;
    if (proto_unescape(fields[3], m->subject, sizeof(m->subject)) < 0)
        return -1;
    snprintf(m->date, sizeof(m->date), "%s", fields[4]);
    m->read = atoi(fields[5]);
    if (proto_unescape(fields[6], tmp, sizeof(tmp)) < 0)
        return -1;
    snprintf(m->body, sizeof(m->body), "%.*s", (int)sizeof(m->body) - 1, tmp);
    return 0;
}

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

    if (fflush(f) != 0 || ferror(f)) {
        fclose(f);
        return -1;
    }
    fclose(f);
    return 0;
}

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
        if (line[0] == '\0')
            continue;
        if (parse_mail_line(line, &out[count]) == 0)
            count++;
    }
    fclose(f);
    return count;
}

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
        if (line[0] == '\0')
            continue;
        if (parse_mail_line(line, out) == 0 && out->id == id) {
            fclose(f);
            return 0;
        }
    }
    fclose(f);
    return -1;
}

/* Rewrites a box through a filter callback; atomic via temp file + rename. */
typedef int (*record_filter)(mail_record *m, long id, void *arg);

static int rewrite_box(const char *owner, int box, record_filter fn,
                       long id, void *arg)
{
    char path[600], tmp[620], line[MAX_LINE];
    char efrom[MAX_USER * 2], eto[MAX_TO * 2];
    char esub[MAX_SUBJECT * 2], ebody[MAX_BODY * 2];
    FILE *in, *outf;
    int changed = 0;

    path_box(owner, box, path, sizeof(path));
    snprintf(tmp, sizeof(tmp), "%s.tmp", path);

    in = fopen(path, "r");
    if (!in)
        return -1;
    outf = fopen(tmp, "w");
    if (!outf) {
        fclose(in);
        return -1;
    }

    while (fgets(line, sizeof(line), in)) {
        mail_record m;
        char copy[MAX_LINE];

        snprintf(copy, sizeof(copy), "%s", line);
        copy[strcspn(copy, "\r\n")] = '\0';
        if (copy[0] == '\0')
            continue;
        if (parse_mail_line(copy, &m) < 0)
            continue;

        int keep = fn(&m, id, arg);
        if (keep < 0) {
            changed = 1;
            continue;               /* drop the record */
        }
        if (keep > 0)
            changed = 1;

        proto_escape(m.from, efrom, sizeof(efrom));
        proto_escape(m.to, eto, sizeof(eto));
        proto_escape(m.subject, esub, sizeof(esub));
        proto_escape(m.body, ebody, sizeof(ebody));
        fprintf(outf, "%ld|%s|%s|%s|%s|%d|%s\n", m.id, efrom, eto, esub,
                m.date, m.read, ebody);
    }

    fclose(in);
    if (fflush(outf) != 0 || ferror(outf)) {
        fclose(outf);
        unlink(tmp);
        return -1;
    }
    fclose(outf);

    if (rename(tmp, path) < 0) {
        unlink(tmp);
        return -1;
    }
    return changed ? 0 : -1;
}

static int filter_mark_read(mail_record *m, long id, void *arg)
{
    (void)arg;
    if (m->id == id && !m->read) {
        m->read = 1;
        return 1;
    }
    return 0;
}

static int filter_delete(mail_record *m, long id, void *arg)
{
    (void)arg;
    return m->id == id ? -1 : 0;
}

int store_mark_read(const char *owner, long id)
{
    return rewrite_box(owner, BOX_INBOX, filter_mark_read, id, NULL);
}

int store_delete_mail(const char *owner, int box, long id)
{
    return rewrite_box(owner, box, filter_delete, id, NULL);
}

int store_unread_count(const char *owner)
{
    mail_record *recs;
    int n, unread = 0;

    recs = malloc(sizeof(*recs) * 256);
    if (!recs)
        return 0;

    n = store_list_mail(owner, BOX_INBOX, recs, 256);
    for (int i = 0; i < n; i++)
        if (!recs[i].read)
            unread++;
    free(recs);
    return unread;
}
