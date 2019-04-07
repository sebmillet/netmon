// webserver.c

// Copyright Sébastien Millet, 2013, 2019

#include "main.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <sys/time.h>
#include <wchar.h>
#include <sys/stat.h>

#ifdef MY_WINDOWS

// WINDOWS

#define DEFAULT_HTML_DIRECTORY (".")
#define DEFAULT_HTML_FILE "status.html"

#include <winsock2.h>

#else

// NOT WINDOWS

#define DEFAULT_HTML_DIRECTORY (".")
#define DEFAULT_HTML_FILE "status.html"

#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>

#endif

#define DEFAULT_HTML_REFRESH_PERIOD 20
#define DEFAULT_HTML_NB_COLUMNS 2

#define WEBSERVER_LOG_PREFIX    "WEBSERVER"
#ifdef MY_WINDOWS
#define DEFAULT_WEBSERVER_ON TRUE
#define DEFAULT_WEBSERVER_PORT 80
#else
#define DEFAULT_WEBSERVER_ON FALSE
#define DEFAULT_WEBSERVER_PORT 8080
#endif

struct img_file_t img_files[_ST_NBELEMS] = {
    {"st-undef.png", NULL, 0},      // ST_UNDEF
    {"st-unknown.png", NULL, 0},    // ST_UNKNOWN
    {"st-ok.png", NULL, 0},             // ST_OK
    {"st-fail.png", NULL, 0}            // ST_FAIL
};

const char *ST_TO_BGCOLOR_FORHTML[_ST_NBELEMS] = {
    "#FFFFFF", // ST_UNDEF
    "#B0B0B0", // ST_UNKNOWN
    "#00FF00", // ST_OK
    "#FF0000"  // ST_FAIL
};

const char *POEM =
    "<!DOCTYPE HTML PUBLIC \"-//W3C//DTD HTML 4.01//EN\" \"http://www.w3.org/TR/html4/strict.dtd\">"
    "<html lang=\"fr\"><head><meta charset=\"UTF-8\"/><title>Poème</title></head><body>\015\012"
    "<h3>Charles Baudelaire</h3>\015\012"
    "<h4>Recueil&nbsp;: Les fleurs du mal</h4><hr>\015\012"
    "<h4>L'avertisseur</h4>\015\012"
    "<p>Tout homme digne de ce nom<br>\015\012"
    "A dans le coeur un Serpent jaune,<br>\015\012"
    "Installé comme sur un trône,<br>\015\012"
    "Qui, s'il dit&nbsp;: &laquo;&nbsp;Je veux !&nbsp;&raquo; répond&nbsp;: &laquo;&nbsp;Non !&nbsp;&raquo;<br>\015\012"
    "<br>\015\012"
    "Plonge tes yeux dans les yeux fixes<br>\015\012"
    "Des Satyresses ou des Nixes,<br>\015\012"
    "La Dent dit&nbsp;: &laquo;&nbsp;Pense à ton devoir&nbsp;!&nbsp;&raquo;<br>\015\012"
    "<br>\015\012"
    "Fais des enfants, plante des arbres,<br>\015\012"
    "Polis des vers, sculpte des marbres,<br>\015\012"
    "La Dent dit : &laquo;&nbsp;Vivras-tu ce soir&nbsp?&nbsp;&raquo;<br>\015\012"
    "<br>\015\012"
    "Quoi qu'il ébauche ou qu'il espère,<br>\015\012"
    "L'homme ne vit pas un moment<br>\015\012"
    "Sans subir l'avertissement<br>\015\012"
    "De l'insupportable Vipère.<br>\015\012"
    "</p></body></html>\015\012";
const char *POEM_URL = "poem";
const char *POEM_TYPE = "text/html";

char g_html_directory[BIGSTRSIZE] = DEFAULT_HTML_DIRECTORY;
char g_html_file[SMALLSTRSIZE] = DEFAULT_HTML_FILE;
char g_html_title[SMALLSTRSIZE] = PACKAGE_STRING;
extern char g_html_directory[BIGSTRSIZE];

long int g_html_refresh_interval = DEFAULT_HTML_REFRESH_PERIOD;

long int g_html_nb_columns = DEFAULT_HTML_NB_COLUMNS;

long int g_webserver_on = DEFAULT_WEBSERVER_ON;
long int g_webserver_port = DEFAULT_WEBSERVER_PORT;

extern char const st_undef[];
extern size_t const st_undef_len;
extern char const st_unknown[];
extern size_t const st_unknown_len;
extern char const st_ok[];
extern size_t const st_ok_len;
extern char const st_fail[];
extern size_t const st_fail_len;

extern const char *netmon[];
extern size_t const netmon_len;

extern int g_trace_network_traffic;

const size_t buffer_size = 5000;

//
// Create files used for HTML display
//
void web_create_files_for_web() {
    char buf[BIGSTRSIZE];

    img_files[ST_UNDEF].var = st_undef;
    img_files[ST_UNDEF].var_len = st_undef_len;
    img_files[ST_UNKNOWN].var = st_unknown;
    img_files[ST_UNKNOWN].var_len = st_unknown_len;
    img_files[ST_OK].var = st_ok;
    img_files[ST_OK].var_len = st_ok_len;
    img_files[ST_FAIL].var = st_fail;
    img_files[ST_FAIL].var_len = st_fail_len;

    my_logf(LL_VERBOSE, LP_DATETIME,
            "Will create image files in html directory");

    int i;
    for (i = 0; i <= _ST_LAST; ++i) {
        strncpy(buf, g_html_directory, sizeof(buf));
        fs_concatene(buf, img_files[i].file_name, sizeof(buf));
        FILE *IMG = my_fopen(buf, "wb", 1, 0);

        if (IMG == NULL) {
            my_logf(LL_ERROR, LP_DATETIME, "Unable to create %s", buf);
        } else {
            int j;
            const char const *v = img_files[i].var;
            size_t l = img_files[i].var_len;

            /*      dbg_write("Creating %s of size %lu\n", buf, l);*/

            if (IMG != NULL) {
                for (j = 0; (unsigned int)j < l; ++j) {
                    fputc(v[j], IMG);
                }
                fclose(IMG);
            }
        }

        add_reader_access_right(buf);
    }

    strncpy(buf, g_html_directory, sizeof(buf));
    fs_concatene(buf, FILE_MAN_EN, sizeof(buf));
    FILE *IMG = my_fopen(buf, "w", 1, 0);

    if (IMG == NULL) {
        my_logf(LL_ERROR, LP_DATETIME, "Unable to create %s", buf);
    } else {
        int j;
        for (j = 0; (unsigned)j < netmon_len; ++j) {
            fputs(netmon[j], IMG);
        }
        fclose(IMG);

        add_reader_access_right(buf);
    }
}

//
// Start server listening
// Return 1 if OK, 0 if error
//
int server_listen(int listen_port, const char *prefix,
                  connection_t *conn) {
    struct sockaddr_in listen_sa;

    char s_err[ERR_STR_BUFSIZE];

    conn->sock = socket(PF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (conn->sock == -1) {
        my_logf(LL_ERROR, LP_DATETIME, "%s: error creating socket, error %s",
                prefix, os_last_err_desc(s_err, sizeof(s_err)));
        return 0;
    }

    int bOptVal = TRUE;
    socklen_t bOptLen = sizeof(int);
    if (setsockopt(conn->sock, SOL_SOCKET, SO_REUSEADDR,
                   (char *)&bOptVal, bOptLen)
            == SOCKET_ERROR) {
        my_logf(LL_ERROR, LP_DATETIME, "%s: cannot set socket option, error %s",
                prefix, os_last_err_desc(s_err, sizeof(s_err)));
        conn_close(conn);
        return 0;
    }

    listen_sa.sin_addr.s_addr = htonl(INADDR_ANY);
    listen_sa.sin_port = htons((u_short)listen_port);
    listen_sa.sin_family = AF_INET;

    if (bind(conn->sock, (struct sockaddr*)&listen_sa, sizeof(listen_sa))
            == SOCKET_ERROR) {
        my_logf(LL_ERROR, LP_DATETIME, "%s: cannot bind, error %s", prefix,
                os_last_err_desc(s_err, sizeof(s_err)));
        conn_close(conn);
        return 0;
    }
    if (listen(conn->sock, SOMAXCONN) == SOCKET_ERROR) {
        my_logf(LL_ERROR, LP_DATETIME, "%s: cannot listen, error %s", prefix,
                os_last_err_desc(s_err, sizeof(s_err)));
        conn_close(conn);
        return 0;
    }
    return 1;
}

//
// Have server accept incoming connections
// Returns 0 if failure, 1 if OK
//
int server_accept(connection_t *listen_conn,
                  struct sockaddr_in* remote_sin,
                  int listen_port, const char* prefix,
                  connection_t *connect_conn) {
    socklen_t remote_sin_len;

    char s_err[SMALLSTRSIZE];

    my_logf(LL_VERBOSE, LP_DATETIME, "%s: listening on port %u...",
            prefix, listen_port);
    remote_sin_len = sizeof(*remote_sin);
    connect_conn->sock = accept(listen_conn->sock,
                                (struct sockaddr *)remote_sin,
                                &remote_sin_len);

    my_logf(LL_DEBUG, LP_DATETIME, "%s: accept() function returned");

    if (connect_conn->sock == -1) {
        my_logf(LL_ERROR, LP_DATETIME, "%s: cannot accept, error %s",
                prefix, os_last_err_desc(s_err, sizeof(s_err)));
        conn_close(connect_conn);
        return 0;
    }
    my_logf(LL_VERBOSE, LP_DATETIME, "%s: connection accepted from %s",
            prefix, inet_ntoa(remote_sin->sin_addr));
    return 1;
}

//
// Send error page over HTTP connection
//
void http_send_error_page(connection_t *conn, const char *e,
                          const char *t) {
    my_logf(LL_DEBUG, LP_DATETIME, "Sending HTTP error %s / %s", e, t);

    conn_line_sendf(conn, g_trace_network_traffic, "HTTP/1.1 %s", e);
    conn_line_sendf(conn, g_trace_network_traffic, "Connection: close");
    conn_line_sendf(conn, g_trace_network_traffic, "Content-type: text/html");
    conn_line_sendf(conn, g_trace_network_traffic, "");
    conn_line_sendf(conn, g_trace_network_traffic,
                    "<html><head><title>Not Found</title></head>");
    conn_line_sendf(conn, g_trace_network_traffic,
                    "<body><p><b>%s</b></p></body></html>", t);

    my_logf(LL_DEBUG, LP_DATETIME, "Finished sending HTTP error %s / %s", e,
            t);
}

//
// Date & time to str for network HTTP usage
//
char *my_ctime_r(const time_t *timep, char *buf, size_t buflen) {
    char *c = ctime(timep);
    if (c == NULL)
        return c;
    strncpy(buf, c, buflen);
    buf[buflen - 1] = '\0';
    trim(buf);
    return buf;
}

//
// Answers web connections
//
int manage_web_transaction(connection_t *conn) {

    my_logf(LL_DEBUG, LP_DATETIME, "Entering web transaction...");

    char *received = NULL;
    size_t size;
    int read_res = 0;
    if ((read_res = conn_read_line_alloc(conn, &received,
                                         g_trace_network_traffic, &size)) < 0) {
        MYFREE(received);
        return -1;
    }

    my_logf(LL_DEBUG, LP_DATETIME, "Received request '%s'", received);

    char *p = received;
    if (strncmp(p, "GET", 3)) {
        MYFREE(received);
        http_send_error_page(conn, "400 Bad Request",
                             "Could not understand request");
        return -1;
    }
    p += 3;
    while (*p == ' ')
        ++p;
    if (strncmp(p, "http://", 7) == 0)
        p += 7;
    if (*p == '/')
        ++p;

    char *tmpurl = p;

    while (*p != '\0' && *p != ' ')
        ++p;
    if (*p != '\0') {
        *p = '\0';
        ++p;
    }

    while (*p == ' ')
        ++p;
    if (strncmp(p, "HTTP/1.1", 8)) {
        MYFREE(received);
        http_send_error_page(conn, "400 Bad Request",
                             "Could not understand request");
        return -1;
    }

    char *t = strrchr(tmpurl, '/');
    if (t != NULL)
        tmpurl = t + 1;
    if (strstr(received, "..") != 0) {
        MYFREE(received);
        http_send_error_page(conn, "401 Unauthorized",
                             "Not allowed to go up in directory tree");
        return -1;
    }

    char url[BIGSTRSIZE];
    strncpy(url, tmpurl, sizeof(url));

    int keep_alive = TRUE;
    while (strlen(received) != 0) {
        if ((read_res = conn_read_line_alloc(conn, &received,
                                             g_trace_network_traffic,
                                             &size))
                < 0) {
            MYFREE(received);
            return -1;
        }

        if (strncasecmp(received, "connection:", 11) == 0
                && strstr(received, "close") != NULL) {
            my_logf(LL_DEBUG, LP_DATETIME,
                    "Connection will be closed afterwards");
            keep_alive = FALSE;
        }
    }

    MYFREE(received);

    my_logf(LL_DEBUG, LP_DATETIME,
            WEBSERVER_LOG_PREFIX ": client requested '%s'", url);

    char path[BIGSTRSIZE];
    strncpy(path, g_html_directory, sizeof(path));

    const char *internal_content = NULL;
    const char *type_internal_content = NULL;
    size_t size_internal_content = 0;
    size_t content_length = 0;

    if (strcasecmp(url, MAN_EN) == 0)
        fs_concatene(path, FILE_MAN_EN, sizeof(path));
    else if (strcasecmp(url, POEM_URL) == 0) {
        internal_content = POEM;
        size_internal_content = strlen(internal_content);
        my_logf(LL_DEBUG, LP_DATETIME,
                "Size of poem: %lu", size_internal_content);
        type_internal_content = POEM_TYPE;
    } else {
        fs_concatene(path, strlen(url) == 0 ? g_html_file : url, sizeof(path));
    }

    struct stat s;

    if (internal_content == NULL) {
        my_logf(LL_DEBUG, LP_DATETIME, "Will stat file '%s'", path);

        if (stat(path, &s)) {
            char s_err[SMALLSTRSIZE];
            errno_error(s_err, sizeof(s_err));
            my_logf(LL_ERROR, LP_DATETIME, "%s", s_err);
            http_send_error_page(conn, "404 Not found", s_err);
            return -1;
        }
        content_length = (size_t)s.st_size;
    }

    char dt_fileupdate[50];
    char dt_now[50];
    struct timeval tv;

    FILE *F = NULL;

    const char *content_type = "application/octet-stream";

    if (internal_content == NULL) {
        if (my_ctime_r(&s.st_mtime, dt_fileupdate,
                       sizeof(dt_fileupdate)) == 0) {
            http_send_error_page(conn, "500 Server error",
                                 "Internal server error");
            return -1;
        }
    }
    if (gettimeofday(&tv, NULL) != 0 ||
            my_ctime_r(&tv.tv_sec, dt_now, sizeof(dt_now)) == 0) {
        http_send_error_page(conn, "500 Server error", "Internal server error");
        return -1;
    }
    if (internal_content == NULL) {
        my_logf(LL_DEBUG, LP_DATETIME, "path opened: '%s'", path);
        F = my_fopen(path, "rb", 5, 800);
        if (F == NULL) {
            http_send_error_page(conn, "404 Not found", "File not found");
            return -1;
        }
        char *pos;
        if ((pos = strrchr(path, '.')) != NULL) {
            ++pos;
            if (strcasecmp(pos, "png") == 0)
                content_type = "image/png";
            else if (strcasecmp(pos, "htm") == 0
                     || strcasecmp(pos, "html") == 0)
                content_type = "text/html";
            else if (strcasecmp(pos, "ini") == 0 || strcasecmp(pos, "log") == 0)
                content_type = "text/ascii";
        }
    } else {
        strncpy(dt_fileupdate, dt_now, sizeof(dt_fileupdate));
        content_type = type_internal_content;
        content_length = size_internal_content;
    }

    // No way to work with binary files and kep-alive? I don't find...
    keep_alive = FALSE;

    conn_line_sendf(conn, g_trace_network_traffic, "HTTP/1.1 200 OK");
    conn_line_sendf(conn, g_trace_network_traffic, "Connection: %s",
                    keep_alive ? "keep-alive" : "close");
    conn_line_sendf(conn, g_trace_network_traffic, "Content-length: %lu",
                    (long unsigned int)content_length);
    conn_line_sendf(conn, g_trace_network_traffic, "Content-type: %s",
                    content_type);
    conn_line_sendf(conn, g_trace_network_traffic, "Date: %s", dt_now);
    conn_line_sendf(conn, g_trace_network_traffic, "Last-modified: %s",
                    dt_fileupdate);
    conn_line_sendf(conn, g_trace_network_traffic, "");

    my_logf(LL_DEBUG, LP_DATETIME, "Will send content over the network");

    size_t n;
    ssize_t e;
    if (internal_content == NULL) {
        char *buffer = (char *)MYMALLOC(buffer_size, buffer);
        while (feof(F) == 0) {
            if ((n = fread(buffer, 1, buffer_size, F)) == 0) {
                if (feof(F) == 0) {
                    my_logf(LL_ERROR, LP_DATETIME, "Error reading file %s",
                            path);
                    keep_alive = FALSE;
                    MYFREE(buffer);
                    fclose(F);
                    break;
                }
            }
            e = conn->sock_write(conn, buffer, n);
            if (e == SOCKET_ERROR) {
                my_logf(LL_ERROR, LP_DATETIME, "Socket error");
                keep_alive = FALSE;
                MYFREE(buffer);
                fclose(F);
                break;
            }
        }
        MYFREE(buffer);
        fclose(F);
        conn_line_sendf(conn, g_trace_network_traffic, "");
    } else {
        n = size_internal_content;
        char *walker = (char *)internal_content;
        while (n >= 1) {
            size_t to_send = (n >= buffer_size ? buffer_size : n);
            n -= to_send;
            e = conn->sock_write(conn, walker, to_send);
            if (e == SOCKET_ERROR) {
                my_logf(LL_ERROR, LP_DATETIME, "Socket error");
                keep_alive = FALSE;
                break;
            }
            walker += to_send;
        }
    }

    my_logf(LL_DEBUG, LP_DATETIME,
            "Finished sending content over the network");

    return keep_alive ? 0 : -1;
}

//
// Manages web server
//
void *webserver(void *p) {
    UNUSED(p);

    connection_t listen_conn;
    conn_init(&listen_conn, CONNTYPE_PLAIN);

    if (!server_listen((int)g_webserver_port, WEBSERVER_LOG_PREFIX,
                       &listen_conn)) {
        my_logf(LL_NORMAL, LP_DATETIME, "%s: stop", WEBSERVER_LOG_PREFIX);
        return NULL;
    }
    my_logf(LL_NORMAL, LP_DATETIME, "%s: start", WEBSERVER_LOG_PREFIX);

    struct sockaddr_in remote_sin;

    connection_t connect_conn;
    conn_init(&connect_conn, CONNTYPE_PLAIN);

    while (1) {
        while (server_accept(&listen_conn, &remote_sin, (int)g_webserver_port,
                             WEBSERVER_LOG_PREFIX, &connect_conn) != -1) {
            if (manage_web_transaction(&connect_conn) != 0) {
                conn_close(&connect_conn);
                my_logf(LL_VERBOSE, LP_DATETIME,
                        WEBSERVER_LOG_PREFIX ": terminated connection with client");
            } else {
                my_logf(LL_VERBOSE, LP_DATETIME,
                        WEBSERVER_LOG_PREFIX ": continuing connection with client");
            }
        }
        my_logf(LL_VERBOSE, LP_DATETIME,
                WEBSERVER_LOG_PREFIX ": OULALA");
    }
    return NULL;
}

