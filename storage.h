// storage.h - Plain-text persistence for users and mail; caller holds the server mutex.
#ifndef STORAGE_H
#define STORAGE_H

#include "common.h"

#define BOX_INBOX 0
#define BOX_SENT  1

typedef struct {
    char username[MAX_USER];
    char email[MAX_EMAIL];
    char password[MAX_PASSWORD];
    char created[MAX_DATE];
} user_record;

typedef struct {
    long id;
    char from[MAX_USER];
    char to[MAX_TO];
    char subject[MAX_SUBJECT];
    char date[MAX_DATE];
    int  read;
    char body[MAX_BODY];
} mail_record;

int store_init(const char *data_dir);
int store_find_user(const char *username, user_record *out);
int store_email_exists(const char *email);
int store_add_user(const user_record *u);
long store_next_id(void);
int store_append_mail(const char *owner, int box, const mail_record *m);
int store_list_mail(const char *owner, int box, mail_record *out, int max);
int store_get_mail(const char *owner, int box, long id, mail_record *out);
int store_mark_read(const char *owner, long id);

#endif /* STORAGE_H */
