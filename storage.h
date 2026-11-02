/* storage.h - Plain-text persistence for users and mail; caller must hold the server mutex. */
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

/* Creates the data directory tree. Returns 0 on success, -1 on failure. */
int store_init(const char *data_dir);

/* Looks up a user by username. Returns 0 if found, -1 otherwise. */
int store_find_user(const char *username, user_record *out);

/* Returns 1 if the e-mail address is already registered, 0 otherwise. */
int store_email_exists(const char *email);

/* Appends a new user. Returns 0 on success, -1 on I/O failure. */
int store_add_user(const user_record *u);

/* Allocates the next global email id. Returns the id, or -1 on failure. */
long store_next_id(void);

/* Appends a mail record to a user's inbox or sent box. 0 ok, -1 error. */
int store_append_mail(const char *owner, int box, const mail_record *m);

/* Loads up to max records of a box into out[]; returns the count or -1. */
int store_list_mail(const char *owner, int box, mail_record *out, int max);

/* Fetches one record by id from a box. 0 found, -1 not found/error. */
int store_get_mail(const char *owner, int box, long id, mail_record *out);

/* Marks an inbox message as read. 0 ok, -1 error. */
int store_mark_read(const char *owner, long id);

/* Removes a message from a box. 0 ok, -1 error. */
int store_delete_mail(const char *owner, int box, long id);

/* Counts unread messages in the inbox. */
int store_unread_count(const char *owner);

#endif /* STORAGE_H */
