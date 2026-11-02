// client.c - MiniMail interactive client: menu action -> one protocol request line.
#define _POSIX_C_SOURCE 200809L

#include "common.h"

#include <arpa/inet.h>
#include <errno.h>
#include <netdb.h>
#include <netinet/in.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>

static int g_fd = -1;
static char g_user[MAX_USER];
static char g_email[MAX_EMAIL];
static int g_logged_in = 0;

// Builds one request line, escaping each field, and sends it to the server.
static int request(const char *cmd, const char *a, const char *b, const char *c)
{
    char buf[MAX_LINE];
    char esc[MAX_LINE];
    size_t used;

    snprintf(buf, sizeof(buf), "%s", cmd);
    used = strlen(buf);

    const char *fields[3] = { a, b, c };
    for (int i = 0; i < 3 && fields[i]; i++) {
        proto_escape(fields[i], esc, sizeof(esc));
        int n = snprintf(buf + used, sizeof(buf) - used, "|%s", esc);
        if (n < 0 || (size_t)n >= sizeof(buf) - used) {
            printf("\n[!] Request too long.\n");
            return -1;
        }
        used += (size_t)n;
    }

    if (send_line(g_fd, buf) < 0) {
        printf("\n[!] Connection lost while sending.\n");
        return -1;
    }
    return 0;
}

// Reads one response line; returns -1 if the server went away.
static int read_response(char *buf, size_t n)
{
    int rc = recv_line(g_fd, buf, n);

    if (rc <= 0) {
        printf("\n[!] Server closed the connection.\n");
        return -1;
    }
    return 0;
}

// Shows a label and reads one line of input; returns 0 on EOF.
static int prompt(const char *label, char *out, size_t n)
{
    printf("%s", label);
    fflush(stdout);

    if (!fgets(out, (int)n, stdin))
        return 0;
    out[strcspn(out, "\r\n")] = '\0';
    return 1;
}

// Reads the message body line by line until a single "." line.
static int prompt_body(char *out, size_t n)
{
    size_t used = 0;

    printf("Message body (end with a single '.' on its own line):\n");
    out[0] = '\0';

    for (;;) {
        char line[1024];

        printf("> ");
        fflush(stdout);
        if (!fgets(line, sizeof(line), stdin))
            return 0;
        line[strcspn(line, "\r\n")] = '\0';

        if (strcmp(line, ".") == 0)
            return 1;

        size_t need = strlen(line) + (used ? 1 : 0);
        if (used + need + 1 >= n) {
            printf("[!] Body too long, truncating.\n");
            return 1;
        }
        if (used) {
            out[used++] = '\n';
            out[used] = '\0';
        }
        strcat(out, line);
        used += strlen(line);
    }
}

// Prints an ERR response as "[!] CODE: message".
static void print_error(const char *resp)
{
    char copy[MAX_LINE];
    char *fields[4] = {0};

    snprintf(copy, sizeof(copy), "%s", resp);
    if (split_fields(copy, fields, 4) >= 2)
        printf("[!] %s: %s\n", fields[0], fields[1]);
    else
        printf("[!] %s\n", resp);
}

// Menu 1: asks for username, email and password, then sends REGISTER.
static void do_register(void)
{
    char user[MAX_USER], email[MAX_EMAIL], pass[MAX_PASSWORD], resp[MAX_LINE];

    if (!prompt("Username: ", user, sizeof(user)) ||
        !prompt("Email: ", email, sizeof(email)) ||
        !prompt("Password: ", pass, sizeof(pass)))
        return;

    if (request("REGISTER", user, email, pass) < 0)
        return;
    if (read_response(resp, sizeof(resp)) < 0)
        return;

    if (strncmp(resp, "OK ", 3) == 0)
        printf("[+] Registered '%s' (%s). You can now log in.\n", user, email);
    else
        print_error(resp);
}

// Menu 2: asks for username and password, then sends LOGIN.
static void do_login(void)
{
    char user[MAX_USER], pass[MAX_PASSWORD], resp[MAX_LINE];
    char copy[MAX_LINE], *fields[8];

    if (!prompt("Username: ", user, sizeof(user)) ||
        !prompt("Password: ", pass, sizeof(pass)))
        return;

    if (request("LOGIN", user, pass, NULL) < 0)
        return;
    if (read_response(resp, sizeof(resp)) < 0)
        return;

    if (strncmp(resp, "OK ", 3) != 0) {
        print_error(resp);
        return;
    }

    snprintf(copy, sizeof(copy), "%s", resp);
    int nf = split_fields(copy, fields, 8);
    snprintf(g_user, sizeof(g_user), "%s", nf > 1 ? fields[1] : user);
    snprintf(g_email, sizeof(g_email), "%s", nf > 2 ? fields[2] : "");
    g_logged_in = 1;

    printf("[+] Logged in as %s <%s>\n", g_user, g_email);
    if (nf > 3)
        printf("    Unread messages: %s\n", fields[3]);
}

// Menu 7: sends LOGOUT and clears the local session.
static void do_logout(void)
{
    char resp[MAX_LINE];

    if (!g_logged_in) {
        printf("[!] You are not logged in.\n");
        return;
    }
    if (request("LOGOUT", NULL, NULL, NULL) < 0)
        return;
    if (read_response(resp, sizeof(resp)) < 0)
        return;

    if (strncmp(resp, "OK ", 3) == 0) {
        printf("[+] Logged out.\n");
        g_logged_in = 0;
        g_user[0] = '\0';
    } else {
        print_error(resp);
    }
}

// Menu 3: asks for recipients, subject and body, then sends SEND.
static void do_send(void)
{
    char to[MAX_TO], subject[MAX_SUBJECT], body[MAX_BODY], resp[MAX_LINE];

    if (!g_logged_in) {
        printf("[!] Please log in first.\n");
        return;
    }
    if (!prompt("To (comma separated usernames): ", to, sizeof(to)) ||
        !prompt("Subject: ", subject, sizeof(subject)) ||
        !prompt_body(body, sizeof(body)))
        return;

    if (request("SEND", to, subject, body) < 0)
        return;
    if (read_response(resp, sizeof(resp)) < 0)
        return;

    if (strncmp(resp, "OK ", 3) == 0) {
        char copy[MAX_LINE], *fields[4] = {0};
        snprintf(copy, sizeof(copy), "%s", resp);
        split_fields(copy, fields, 4);
        printf("[+] Message sent (id %s) to %s.\n",
               fields[1] ? fields[1] : "?", fields[2] ? fields[2] : to);
    } else {
        print_error(resp);
    }
}

// Menu 4/6: prints the inbox or sent box as a table.
static void print_mail_table(const char *title, int sent_box)
{
    char resp[MAX_LINE];
    int count = 0;

    if (!g_logged_in) {
        printf("[!] Please log in first.\n");
        return;
    }
    if (request(sent_box ? "SENT" : "INBOX", NULL, NULL, NULL) < 0)
        return;
    if (read_response(resp, sizeof(resp)) < 0)
        return;

    if (strncmp(resp, "OK ", 3) != 0) {
        print_error(resp);
        return;
    }

    printf("\n-------- %s --------\n", title);
    printf("%-5s %-12s %-28s %-20s %s\n",
           "ID", sent_box ? "TO" : "FROM", "SUBJECT", "DATE", "STATUS");
    printf("--------------------------------------------------------------------------\n");

    for (;;) {
        char line[MAX_LINE], copy[MAX_LINE], *f[8];
        char from[MAX_USER], to[MAX_TO], subj[MAX_SUBJECT];

        if (read_response(line, sizeof(line)) < 0)
            return;
        if (strcmp(line, "END") == 0)
            break;
        if (strncmp(line, "M|", 2) != 0)
            continue;

        snprintf(copy, sizeof(copy), "%s", line);
        if (split_fields(copy, f, 8) != 7)
            continue;

        proto_unescape(f[2], from, sizeof(from));
        proto_unescape(f[3], to, sizeof(to));
        proto_unescape(f[4], subj, sizeof(subj));

        printf("%-5s %-12s %-28.28s %-20s %s\n", f[1],
               sent_box ? to : from, subj, f[5],
               sent_box ? "-" : (atoi(f[6]) ? "read" : "UNREAD"));
        count++;
    }

    if (count == 0)
        printf("(no messages)\n");
    printf("--------------------------------------------------------------------------\n");
    printf("%d message(s)\n", count);
}

// Menu 5: asks for a message id and prints the full message.
static void do_read(void)
{
    char idbuf[32], resp[MAX_LINE];
    char copy[MAX_LINE], *f[8];
    char from[MAX_USER], to[MAX_TO], subj[MAX_SUBJECT], body[MAX_BODY];

    if (!g_logged_in) {
        printf("[!] Please log in first.\n");
        return;
    }
    if (!prompt("Message ID: ", idbuf, sizeof(idbuf)))
        return;

    if (request("READ", idbuf, NULL, NULL) < 0)
        return;
    if (read_response(resp, sizeof(resp)) < 0)
        return;

    if (strncmp(resp, "OK ", 3) != 0) {
        print_error(resp);
        return;
    }

    snprintf(copy, sizeof(copy), "%s", resp);
    if (split_fields(copy, f, 8) != 8) {
        printf("[!] Malformed READ response.\n");
        return;
    }

    proto_unescape(f[2], from, sizeof(from));
    proto_unescape(f[3], to, sizeof(to));
    proto_unescape(f[4], subj, sizeof(subj));
    proto_unescape(f[7], body, sizeof(body));

    printf("\n============================================================\n");
    printf("ID      : %s\n", f[1]);
    printf("From    : %s\n", from);
    printf("To      : %s\n", to);
    printf("Subject : %s\n", subj);
    printf("Date    : %s\n", f[5]);
    printf("------------------------------------------------------------\n");
    printf("%s\n", body);
    printf("============================================================\n");
}

// Draws the main menu and the current login state.
static void print_menu(void)
{
    printf("\n================ MiniMail ================\n");
    if (g_logged_in)
        printf(" Logged in as: %s <%s>\n", g_user, g_email);
    else
        printf(" Not logged in\n");
    printf("------------------------------------------\n");
    printf(" 1. Register\n");
    printf(" 2. Login\n");
    printf(" 3. Compose / Send mail\n");
    printf(" 4. View Inbox\n");
    printf(" 5. Read a message\n");
    printf(" 6. View Sent mail\n");
    printf(" 7. Logout\n");
    printf(" 0. Quit\n");
    printf("==========================================\n");
    printf("Choice: ");
    fflush(stdout);
}

// Resolves the host and opens a TCP connection; returns the socket or -1.
static int connect_to(const char *host, int port)
{
    struct addrinfo hints, *res = NULL, *p;
    char portstr[16];
    int fd = -1;

    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    snprintf(portstr, sizeof(portstr), "%d", port);

    int rc = getaddrinfo(host, portstr, &hints, &res);
    if (rc != 0) {
        fprintf(stderr, "[!] Cannot resolve '%s': %s\n", host, gai_strerror(rc));
        return -1;
    }

    for (p = res; p; p = p->ai_next) {
        fd = socket(p->ai_family, p->ai_socktype, p->ai_protocol);
        if (fd < 0)
            continue;
        if (connect(fd, p->ai_addr, p->ai_addrlen) == 0)
            break;
        close(fd);
        fd = -1;
    }
    freeaddrinfo(res);

    if (fd < 0)
        fprintf(stderr, "[!] Could not connect to %s:%d: %s\n",
                host, port, strerror(errno));
    return fd;
}

// Prints the client command-line options.
static void usage(const char *prog)
{
    printf("Usage: %s [-h host] [-p port]\n", prog);
    printf("  -h host   server host (default %s)\n", DEFAULT_HOST);
    printf("  -p port   server port (default %d)\n", DEFAULT_PORT);
}

// Parses options, connects to the server, and runs the menu loop.
int main(int argc, char **argv)
{
    const char *host = DEFAULT_HOST;
    int port = DEFAULT_PORT;
    int opt;
    char resp[MAX_LINE];

    while ((opt = getopt(argc, argv, "h:p:")) != -1) {
        switch (opt) {
        case 'h': host = optarg; break;
        case 'p':
            port = atoi(optarg);
            if (port <= 0 || port > 65535) {
                fprintf(stderr, "Invalid port: %s\n", optarg);
                return 1;
            }
            break;
        default: usage(argv[0]); return 1;
        }
    }

    g_fd = connect_to(host, port);
    if (g_fd < 0)
        return 1;

    if (read_response(resp, sizeof(resp)) < 0) {
        close(g_fd);
        return 1;
    }
    printf("[+] Connected to %s:%d\n", host, port);

    for (;;) {
        char choice[16];

        print_menu();
        if (!fgets(choice, sizeof(choice), stdin))
            break;

        switch (choice[0]) {
        case '1': do_register(); break;
        case '2': do_login(); break;
        case '3': do_send(); break;
        case '4': print_mail_table("INBOX", 0); break;
        case '5': do_read(); break;
        case '6': print_mail_table("SENT MAIL", 1); break;
        case '7': do_logout(); break;
        case '0':
            request("QUIT", NULL, NULL, NULL);
            read_response(resp, sizeof(resp));
            printf("[+] Disconnected.\n");
            close(g_fd);
            return 0;
        case '\n':
            break;
        default:
            printf("[!] Invalid choice.\n");
            break;
        }
    }

    request("QUIT", NULL, NULL, NULL);
    close(g_fd);
    printf("\n[+] Disconnected.\n");
    return 0;
}
