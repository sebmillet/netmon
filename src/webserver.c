// webserver.c

// Copyright Sébastien Millet, 2013

#include "main.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <sys/time.h>
#include <wchar.h>
#include <sys/stat.h>

#if defined(_WIN32) || defined(_WIN64)

  // WINDOWS

#define DEFAULT_HTML_DIRECTORY (".")
#define DEFAULT_HTML_FILE (PACKAGE_NAME ".html")

#include <winsock2.h>

#else

  // NOT WINDOWS

#define DEFAULT_HTML_DIRECTORY (".")
#define DEFAULT_HTML_FILE (PACKAGE_NAME ".html")

#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>

#endif

#define DEFAULT_HTML_REFRESH_PERIOD 20
#define DEFAULT_HTML_NB_COLUMNS 2

#define WEBSERVER_LOG_PREFIX  "WEBSERVER"
#if defined(_WIN32) || defined(_WIN64)
#define DEFAULT_WEBSERVER_ON TRUE
#define DEFAULT_WEBSERVER_PORT 80
#else
#define DEFAULT_WEBSERVER_ON FALSE
#define DEFAULT_WEBSERVER_PORT 8080
#endif

struct img_file_t img_files[_ST_NBELEMS] = {
  {"st-undef.png", NULL, 0},    // ST_UNDEF
  {"st-unknown.png", NULL, 0},  // ST_UNKNOWN
  {"st-ok.png", NULL, 0},       // ST_OK
  {"st-fail.png", NULL, 0}      // ST_FAIL
};

const char *ST_TO_BGCOLOR_FORHTML[_ST_NBELEMS] = {
  "#FFFFFF", // ST_UNDEF
  "#B0B0B0", // ST_UNKNOWN
  "#00FF00", // ST_OK
  "#FF0000"  // ST_FAIL
};

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

extern int g_trace_network_traffic;
extern long int g_buffer_size;

extern pthread_mutex_t mutex;

//
// Create files used for HTML display
//
void web_create_img_files() {
  char buf[BIGSTRSIZE];

  img_files[ST_UNDEF].var = st_undef;
  img_files[ST_UNDEF].var_len = st_undef_len;
  img_files[ST_UNKNOWN].var = st_unknown;
  img_files[ST_UNKNOWN].var_len = st_unknown_len;
  img_files[ST_OK].var = st_ok;
  img_files[ST_OK].var_len = st_ok_len;
  img_files[ST_FAIL].var = st_fail;
  img_files[ST_FAIL].var_len = st_fail_len;

  my_logf(LL_VERBOSE, LP_DATETIME, "Will create image files in html directory");

  int i;
  for (i = 0; i <= _ST_LAST; ++i) {
    strncpy(buf, g_html_directory, sizeof(buf));
    fs_concatene(buf, img_files[i].file_name, sizeof(buf));
    FILE *IMG = fopen(buf, "wb+");

    if (IMG == NULL) {
      my_logf(LL_ERROR, LP_DATETIME, "Unable to create %s", buf);
    } else {
      int j;
      const char const *v = img_files[i].var;
      size_t l = img_files[i].var_len;

  /*    dbg_write("Creating %s of size %lu\n", buf, l);*/

      if (IMG != NULL) {
        for (j = 0; (unsigned int)j < l; ++j) {
          fputc(v[j], IMG);
        }
        fclose(IMG);
      }
    }

    add_reader_access_right(buf);
  }

}

//
// Start server listening
// Return 1 if OK, 0 if error
//
int server_listen(int listen_port, const char *prefix, connection_t *conn) {
  struct sockaddr_in listen_sa;

  char s_err[ERR_STR_BUFSIZE];

  conn->sock = socket(PF_INET, SOCK_STREAM, IPPROTO_TCP);
  if (conn->sock == -1) {
    my_logf(LL_ERROR, LP_DATETIME, "%s: error creating socket, error %s", prefix, os_last_err_desc(s_err, sizeof(s_err)));
    return 0;
  }

  int bOptVal = TRUE;
  socklen_t bOptLen = sizeof(int);
  if (setsockopt(conn->sock, SOL_SOCKET, SO_REUSEADDR, (char *)&bOptVal, bOptLen) == SOCKET_ERROR) {
    my_logf(LL_ERROR, LP_DATETIME, "%s: cannot set socket option, error %s", prefix, os_last_err_desc(s_err, sizeof(s_err)));
    conn_close(conn);
    return 0;
  }

  listen_sa.sin_addr.s_addr = htonl(INADDR_ANY);
  listen_sa.sin_port = htons((u_short)listen_port);
  listen_sa.sin_family = AF_INET;

  if (bind(conn->sock, (struct sockaddr*)&listen_sa, sizeof(listen_sa)) == SOCKET_ERROR) {
    my_logf(LL_ERROR, LP_DATETIME, "%s: cannot bind, error %s", prefix, os_last_err_desc(s_err, sizeof(s_err)));
    conn_close(conn);
    return 0;
  }
  if (listen(conn->sock, SOMAXCONN) == SOCKET_ERROR) {
    my_logf(LL_ERROR, LP_DATETIME, "%s: cannot listen, error %s", prefix, os_last_err_desc(s_err, sizeof(s_err)));
    conn_close(conn);
    return 0;
  }
  return 1;
}

//
// Have server accept incoming connections
// Returns 0 if failure, 1 if OK
//
int server_accept(connection_t *listen_conn, struct sockaddr_in* remote_sin, int listen_port, const char* prefix,
    connection_t *connect_conn) {
  socklen_t remote_sin_len;

  char s_err[SMALLSTRSIZE];

  my_logf(LL_VERBOSE, LP_DATETIME, "%s: listening on port %u...", prefix, listen_port);
  remote_sin_len = sizeof(*remote_sin);
  connect_conn->sock = accept(listen_conn->sock, (struct sockaddr *)remote_sin, &remote_sin_len);
  if (connect_conn->sock == -1) {
    my_logf(LL_ERROR, LP_DATETIME, "%s: cannot accept, error %s", prefix, os_last_err_desc(s_err, sizeof(s_err)));
    conn_close(connect_conn);
    return 0;
  }
  my_logf(LL_VERBOSE, LP_DATETIME, "%s: connection accepted from %s", prefix, inet_ntoa(remote_sin->sin_addr));
  return 1;
}

//
// Send error page over HTTP connection
//
void http_send_error_page(connection_t *conn, const char *e, const char *t) {
  my_logf(LL_DEBUG, LP_DATETIME, "Sending HTTP error %s / %s", e, t);

  conn_line_sendf(conn, g_trace_network_traffic, "HTTP/1.1 %s", e);
  conn_line_sendf(conn, g_trace_network_traffic, "Connection: close");
  conn_line_sendf(conn, g_trace_network_traffic, "Content-type: text/html");
  conn_line_sendf(conn, g_trace_network_traffic, "");
  conn_line_sendf(conn, g_trace_network_traffic, "<html><head><title>Not Found</title></head>");
  conn_line_sendf(conn, g_trace_network_traffic, "<body><p><b>%s</b></p></body></html>", t);

  my_logf(LL_DEBUG, LP_DATETIME, "Finished sending HTTP error %s / %s", e, t);
}

//
// Date & time to str for network HTTP usage
//
char *my_ctime_r(const time_t *timep, char *buf, size_t buflen) {
  my_pthread_mutex_lock(&mutex);

  char *c = ctime(timep);
  if (c == NULL)
    return c;
  strncpy(buf, c, buflen);
  buf[buflen - 1] = '\0';
  trim(buf);

  my_pthread_mutex_unlock(&mutex);

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
  if ((read_res = conn_read_line_alloc(conn, &received, g_trace_network_traffic, &size)) < 0) {
    free(received);
    return -1;
  }

  my_logf(LL_DEBUG, LP_DATETIME, "Received request '%s'", received);

  char *p = received;
  if (strncmp(p, "GET", 3)) {
    free(received);
    http_send_error_page(conn, "400 Bad Request", "Could not understand request");
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
  if (p != '\0') {
    *p = '\0';
    ++p;
  }

  while (*p == ' ')
    ++p;
  if (strncmp(p, "HTTP/1.1", 8)) {
    free(received);
    http_send_error_page(conn, "400 Bad Request", "Could not understand request");
    return -1;
  }

  char *t = strrchr(tmpurl, '/');
  if (t != NULL)
    tmpurl = t + 1;
  if (strstr(received, "..") != 0) {
    free(received);
    http_send_error_page(conn, "401 Unauthorized", "Not allowed to go up in directory tree");
    return -1;
  }

  char url[BIGSTRSIZE];
  strncpy(url, tmpurl, sizeof(url));

  int keep_alive = TRUE;
  while (strlen(received) != 0) {
    if ((read_res = conn_read_line_alloc(conn, &received, g_trace_network_traffic, &size)) < 0) {
      free(received);
      return -1;
    }

    if (strncasecmp(received, "connection:", 11) == 0 && strstr(received, "close") != NULL) {
      my_logf(LL_DEBUG, LP_DATETIME, "Connection will be closed afterwards");
      keep_alive = FALSE;
    }
  }

  free(received);

  my_logf(LL_DEBUG, LP_DATETIME, WEBSERVER_LOG_PREFIX ": client requested '%s'", url);

  char path[BIGSTRSIZE];
  strncpy(path, g_html_directory, sizeof(path));
  fs_concatene(path, strlen(url) == 0 ? g_html_file : url, sizeof(path));

  my_logf(LL_DEBUG, LP_DATETIME, "Will stat file '%s'", path);

  struct stat s;
  if (stat(path, &s)) {
    char s_err[SMALLSTRSIZE];
    errno_error(s_err, sizeof(s_err));
    my_logf(LL_ERROR, LP_DATETIME, "%s", s_err);
    http_send_error_page(conn, "404 Not found", s_err);
    return -1;
  }

  char dt_fileupdate[50];
  char dt_now[50];
  struct timeval tv;
  if (my_ctime_r(&s.st_mtime, dt_fileupdate, sizeof(dt_fileupdate)) == 0 ||
      gettimeofday(&tv, NULL) != 0 ||
      my_ctime_r(&tv.tv_sec, dt_now, sizeof(dt_now)) == 0) {
    http_send_error_page(conn, "500 Server error", "Internal server error");
    return -1;
  }

  FILE *F = fopen(path, "rb");
  if (F == NULL) {
    http_send_error_page(conn, "404 Not found", "File not found");
    return -1;
  }
  char *pos;
  char *content_type = "application/octet-stream";
  if ((pos = strrchr(path, '.')) != NULL) {
    ++pos;
    if (strcasecmp(pos, "png") == 0)
      content_type = "image/png";
    else if (strcasecmp(pos, "htm") == 0 || strcasecmp(pos, "html") == 0)
      content_type = "text/html";
    else if (strcasecmp(pos, "ini") == 0 || strcasecmp(pos, "log") == 0)
      content_type = "text/ascii";
  }

    // No way to work with binary files and kep-alive? I don't find...
  keep_alive = FALSE;

  conn_line_sendf(conn, g_trace_network_traffic, "HTTP/1.1 200 OK");
  conn_line_sendf(conn, g_trace_network_traffic, "Connection: %s", keep_alive ? "keep-alive" : "close");
  conn_line_sendf(conn, g_trace_network_traffic, "Content-length: %li", (long int)s.st_size);
  conn_line_sendf(conn, g_trace_network_traffic, "Content-type: %s", content_type);
  conn_line_sendf(conn, g_trace_network_traffic, "Date: %s", dt_now);
  conn_line_sendf(conn, g_trace_network_traffic, "Last-modified: %s", dt_fileupdate);
  conn_line_sendf(conn, g_trace_network_traffic, "");

  char *buffer = (char *)malloc((size_t)g_buffer_size);
  size_t n;
  ssize_t e;
  while (feof(F) == 0) {

    if ((n = fread(buffer, 1, sizeof(buffer), F)) == 0) {
      if (feof(F) == 0) {
        my_logf(LL_ERROR, LP_DATETIME, "Error reading file %s", path);
        keep_alive = FALSE;
        break;
      }
    }

    e = conn->sock_write(conn, buffer, n);
    if (e == SOCKET_ERROR) {
      my_logf(LL_ERROR, LP_DATETIME, "Socket error");
        keep_alive = FALSE;
      break;
    }
  }

  conn_line_sendf(conn, g_trace_network_traffic, "");

  free(buffer);
  fclose(F);

  return keep_alive ? 0 : -1;
}

//
// Manages web server
//
void *webserver(void *p) {
UNUSED(p);

  connection_t listen_conn;
  conn_init(&listen_conn, CONNTYPE_PLAIN);

  if (server_listen((int)g_webserver_port, WEBSERVER_LOG_PREFIX, &listen_conn) == 0) {
    my_logf(LL_NORMAL, LP_DATETIME, "%s: stop", WEBSERVER_LOG_PREFIX);
    return NULL;
  }
  my_logf(LL_NORMAL, LP_DATETIME, "%s: start", WEBSERVER_LOG_PREFIX);

  struct sockaddr_in remote_sin;

  connection_t connect_conn;
  conn_init(&connect_conn, CONNTYPE_PLAIN);

  while (1) {
    while (server_accept(&listen_conn, &remote_sin, (int)g_webserver_port, WEBSERVER_LOG_PREFIX, &connect_conn) != -1) {
      if (manage_web_transaction(&connect_conn) != 0) {
        conn_close(&connect_conn);
        my_logf(LL_VERBOSE, LP_DATETIME, WEBSERVER_LOG_PREFIX ": terminated connection with client");
      } else {
        my_logf(LL_VERBOSE, LP_DATETIME, WEBSERVER_LOG_PREFIX ": continuing connection with client");
      }
    }
  }
  return NULL;
}

