// util.c

// Copyright Sébastien Millet, 2013

#include "util.h"

#include <stdarg.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <fcntl.h>
#include <sys/time.h>
#include <time.h>
#include <ctype.h>
#include <unistd.h>
#include <sys/stat.h>

#include <openssl/err.h>

loglevel_t g_current_log_level = LL_NORMAL;

char g_log_file[SMALLSTRSIZE];
long int g_print_subst_error = DEFAULT_PRINT_SUBST_ERROR;
int g_date_df = (DEFAULT_DATE_FORMAT == DF_FRENCH);
int g_print_log = DEFAULT_PRINT_LOG;
long int g_log_usec = DEFAULT_LOG_USEC;
FILE *log_fd;

static pthread_mutex_t util_mutex;

const struct connection_table_t connection_table[] = {
  {conn_plain_read, "<<< ", conn_plain_write, ">>> "},  // CONNTYPE_PLAIN
  {conn_ssl_read,   "S<< ", conn_ssl_write,   "S>> "}   // CONNTYPE_SSL
};

#if defined(_WIN32) || defined(_WIN64)

  // * ******* *
  // * WINDOWS *
  // * ******* *

#include <windows.h>

const char FS_SEPARATOR = '\\';

// HAS_TM_GMTOFF is NOT defined under Windows
// and thus the fnuction below is needed.
long int os_gmtoff() {
  TIME_ZONE_INFORMATION TimeZoneInfo;
  GetTimeZoneInformation(&TimeZoneInfo);
  return -(TimeZoneInfo.Bias + TimeZoneInfo.DaylightBias) * 60;
}

void os_sleep(long int seconds) {
  unsigned long int msecs = (unsigned long int)seconds * 1000;
  Sleep(msecs);
}

static void os_set_sock_nonblocking_mode(int sock) {
  u_long iMode = 1;
  int iResult = ioctlsocket(sock, FIONBIO, &iMode);
  if (iResult != NO_ERROR)
    fatal_error("ioctlsocket failed with error: %ld", iResult);
}

static void os_set_sock_blocking_mode(int sock) {
  u_long iMode = 0;
  int iResult = ioctlsocket(sock, FIONBIO, &iMode);
  if (iResult != NO_ERROR)
    fatal_error("ioctlsocket failed with error: %ld", iResult);
}

int os_last_err() {
  int r = WSAGetLastError();
  //WSACleanup();
  return r;
}

char *os_last_err_desc(char *s, size_t s_bufsize) {
  LPVOID lpMsgBuf;
  DWORD last_err = WSAGetLastError();
  FormatMessage(FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
    NULL, last_err, MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT), (LPTSTR)&lpMsgBuf, 0, NULL);
  char tmp[ERR_STR_BUFSIZE];
  strncpy(tmp, (char *)lpMsgBuf, sizeof(tmp));
  int n = strlen(tmp);
  if (n >= 2) {
    if (tmp[n - 2] == '\r' && tmp[n - 1] == '\n')
      tmp[n - 2] = '\0';
  }
  snprintf(s, s_bufsize, "code=%lu (%s)", last_err, tmp);
  //WSACleanup();
  return s;
}

void os_init_network() {
  WSADATA wsaData;
  int e;
  char s_err[ERR_STR_BUFSIZE];
  if ((e = WSAStartup(MAKEWORD(2, 2), &wsaData)) != 0) {
    fatal_error("WSAStartup() returned value %i, error: %s", e, os_last_err_desc(s_err, sizeof(s_err)));
    WSACleanup();
  }
}

int os_last_network_op_is_in_progress() {
  return (WSAGetLastError() == WSAEINPROGRESS || WSAGetLastError() == WSAEWOULDBLOCK);
}

static void os_closesocket(int sock) {
  closesocket(sock);
}

int os_wexitstatus(const int r) {
  return r;
}

int add_reader_access_right(const char *f) {
UNUSED(f);

  return 0;
}

#define strcasecmp _stricmp
#define strncasecmp _strnicmp

#else

  // * *********** *
  // * NOT WINDOWS *
  // * *********** *

#include <sys/wait.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netdb.h>

#define HAS_TM_GMTOFF
// Because HAS_TM_GMTOFF is defined, the fnuction
// os_gmtoff is not needed.

const char FS_SEPARATOR = '/';

void os_sleep(long int seconds) {
  sleep(seconds);
}

void os_set_sock_nonblocking_mode(int sock) {
  long arg = fcntl(sock, F_GETFL, NULL);
  arg |= O_NONBLOCK;
  fcntl(sock, F_SETFL, arg);
}

void os_set_sock_blocking_mode(int sock) {
  long arg = fcntl(sock, F_GETFL, NULL);
  arg &= ~O_NONBLOCK;
  fcntl(sock, F_SETFL, arg);
}

int os_last_err() {
  return errno;
}

char *os_last_err_desc(char *s, size_t s_bufsize) {
  snprintf(s, s_bufsize, "code=%i (%s)", errno, strerror(errno));
  return s;
}

void os_init_network() {
  signal(SIGPIPE, SIG_IGN);
}

int os_last_network_op_is_in_progress() {
  return (errno == EINPROGRESS);
}

static void os_closesocket(int sock) {
  close(sock);
}

int os_wexitstatus(const int r) {
  return WEXITSTATUS(r);
}

int add_reader_access_right(const char *f) {
  struct stat s;
  int r = 0;
  if (!stat(f, &s)) {
    s.st_mode |= S_IRUSR | S_IRGRP | S_IROTH;
    r = chmod(f, s.st_mode);
  }
  if (r) {
    char s_err[SMALLSTRSIZE];
    errno_error(s_err, sizeof(s_err));
    my_logf(LL_ERROR, LP_DATETIME, "Unable to change mode of file '%s': ", f, s_err);
  }
  return -1;
}

#endif


  // * *** *
  // * ALL *
  // * *** *

#ifdef DEBUG
void dbg_write(const char *fmt, ...) {
  va_list args;
  va_start(args, fmt);
  vprintf(fmt, args);
  va_end(args);
}
#endif

void my_pthread_mutex_init(pthread_mutex_t *m) {
  if ((errno = pthread_mutex_init(m, NULL)) != 0) {
    char s_err[SMALLSTRSIZE];
    fatal_error("pthread_mutex_init(): %s", errno_error(s_err, sizeof(s_err)));
  }
}

void util_my_pthread_init() {
  my_pthread_mutex_init(&util_mutex);
}

void my_pthread_mutex_lock(pthread_mutex_t *m) {
  char s_err[SMALLSTRSIZE];
  if ((errno = pthread_mutex_lock(m)) != 0)
    fatal_error("pthread_mutex_lock(): %s", errno_error(s_err, sizeof(s_err)));
}

void my_pthread_mutex_unlock(pthread_mutex_t *m) {
  char s_err[SMALLSTRSIZE];
  if ((errno = pthread_mutex_unlock(m)) != 0)
    fatal_error("pthread_mutex_unlock(): %s", errno_error(s_err, sizeof(s_err)));
}

//
// Converts errno into a readable string
//
char *errno_error(char *s, size_t s_len) {
  snprintf(s, s_len, "Error %i, %s", errno, strerror(errno));
  return s;
}

//
// My implementation of getline()
//
ssize_t my_getline(char **lineptr, size_t *n, FILE *stream) {
  if (*lineptr == NULL || *n == 0) {
    *n = MY_GETLINE_INITIAL_ALLOCATE;
    *lineptr = (char *)malloc(*n);
    if (*lineptr == NULL) {
      errno = ENOMEM;
      return -1;
    }
  }
  char *write_head = *lineptr;
  size_t char_read = 0;
  int c;
  while (1) {

      // Check there's enough memory to store read characters
    if (*n - char_read <= 2) {
      size_t increase = *n * MY_GETLINE_COEF_INCREASE;
      if (increase < MY_GETLINE_MIN_INCREASE)
        increase = MY_GETLINE_MIN_INCREASE;
        (*n) += increase;

      char *old_lineptr = *lineptr;
      *lineptr = (char *)realloc(*lineptr, *n);
      write_head += (*lineptr - old_lineptr);

      if (*lineptr == NULL) {
        errno = ENOMEM;
        return -1;
      }
    }

      // Now read one character from stream
    c = getc(stream);

/*    dbg_write("Char: %i (%c) [*n = %lu]\n", c, c, *n);*/

      // Deal with /IO error
    if (ferror(stream) != 0)
      return -1;

      // Deal with end of file
    if (c == EOF) {
      if (char_read == 0)
        return -1;
      else
        break;
    }

    *write_head++ = c;
    ++char_read;

      // Deal with newline character
    if (c == '\n' || c == '\r')
      break;
  }

  *write_head = '\0';
  return char_read;
}

//
// concatene a path and a file name
//
void fs_concatene(char *dst, const char *src, size_t dst_len) {
  char t[2] = "\0";
  t[0] = FS_SEPARATOR;
  if (dst[strlen(dst) - 1] != FS_SEPARATOR)
    strncat(dst, t, dst_len);
  strncat(dst, src, dst_len);
  dst[dst_len - 1] = '\0';
}

//
// Finds a word in a table, return index found or -1 if not found.
// Case insensitive search
//
int find_string(const char **table, int n, const char *elem) {
  int i;
  for (i = 0; i < n; ++i) {
    if (strcasecmp(table[i], elem) == 0)
      return i;
  }
  return FIND_STRING_NOT_FOUND;
}

//
// Removes leading and traling spaces in a *MODIFYABLE* string.
// To remove trailing spaces, it just writes '\0' after the last non-space
// found from right to left.
//
char *trim(char *str) {
  char *end;

  while(isspace(*str)) str++;

  if(*str == 0) {
    return str;
  }

  end = str + strlen(str) - 1;
  while (end > str && isspace(*end))
    end--;

  *(end + 1) = '\0';
  return str;
}

//
// Print an error in standard error and exit program
// if exit_program is true.
//
void fatal_error(const char *format, ...) {
  va_list args;
  va_start(args, format);

  char str[REGULAR_STR_STRBUFSIZE];
  vsnprintf(str, sizeof(str), format, args);
  strncat(str, "\n", sizeof(str));
  fprintf(stderr, str, NULL);
  va_end(args);
  exit(EXIT_FAILURE);
}

//
// Stops after an internal error
//
void internal_error(const char *desc, const char *source_file, const unsigned long int line) {
  fatal_error("Internal error %s, file %s, line %lu", desc, source_file, line);
}

//
// Initializes the program log
//
void my_log_open() {
  if (strlen(g_log_file) >= 1)
    log_fd = fopen(g_log_file, "a");
  else
    log_fd = NULL;
}

//
// Closes the program log
//
void my_log_close() {
  if (log_fd)
    fclose(log_fd);
}

//
// Do string substitution
// The subst_t array can be NULL (meaning, no substitution to do), in that case n MUST be set to 0
//
char *dollar_subst_alloc(const char *s, const struct subst_t *subst, int n) {
  size_t sc_len = strlen(s) + 1;
  char *sc = (char *)malloc(sc_len);
  strncpy(sc, s, sc_len);

  char var[SMALLSTRSIZE];
  char var2[SMALLSTRSIZE];

  char *p = sc;
  for (; *p != '\0'; ++p) {
    if (*p == '$' && *(p + 1) == '{') {
      char *c = p + 2;
      for (; *c != '}' && *c != '\0'; ++c)
        ;
      int l = c - p - 2;
      if (*c == '}' && l >= 1) {
        *c = '\0';
        strncpy(var, p + 2, sizeof(var));
        var[sizeof(var) - 1] = '\0';

/*        dbg_write("Found variable '%s'\n", var);*/

        int i;

        if (g_print_subst_error) {
          strncpy(var2, SUBST_ERROR_PREFIX, sizeof(var2));
          strncat(var2, var, sizeof(var2));
          strncat(var2, SUBST_ERROR_POSTFIX, sizeof(var2));
        } else {
          strncpy(var2, "", sizeof(var2));
        }

        char *rep = var2;
        for (i = 0; i < n; ++i) {
          if (strcasecmp(subst[i].find, var) == 0) {
            rep = (char *)subst[i].replace;
            break;
          }
        }

/*        dbg_write("=== before: '%s'\n", sc);*/

        size_t buf_len = strlen(c + 1) + 1;
        char *buf = (char *)malloc(buf_len);
        strncpy(buf, c + 1, buf_len);
        *p = '\0';
        size_t need_len = strlen(sc) + strlen(rep) + strlen(buf) + 1;
        if (need_len > sc_len) {
          char *new_sc = (char *)realloc(sc, need_len);

/*          dbg_write("Reallocated from %lu to %lu\n", sc_len, need_len);*/

          sc_len = need_len;
          p += (new_sc - sc);
          sc = new_sc;
        }
        strncat(sc, rep, sc_len);
        strncat(sc, buf, sc_len);
        free(buf);

/*        dbg_write("=== after:  '%s'\n", sc);*/
      }
    }
  }
  return sc;
}

//
// Get date/time of day
//
void get_datetime_of_day(int *wday, int *year, int *month, int *day, int *hour, int *minute, int *second,
       long unsigned int *usec, long int *gmtoff) {
  time_t ltime = time(NULL);
  struct tm ts;
  ts = *localtime(&ltime);

  struct timeval tv;
  struct timezone tz;
  if (gettimeofday(&tv, &tz) == GETTIMEOFDAY_ERROR) {
    char s_err[ERR_STR_BUFSIZE];
    fatal_error("gettimeofday() error, %s", os_last_err_desc(s_err, sizeof(s_err)));
  }

  *wday = ts.tm_wday;
  *year = ts.tm_year + 1900;
  *month = ts.tm_mon + 1;
  *day = ts.tm_mday;
  *hour = ts.tm_hour;
  *minute = ts.tm_min;
  *second = ts.tm_sec;
  *usec = tv.tv_usec;

#ifdef HAS_TM_GMTOFF
  *gmtoff = ts.tm_gmtoff;
#else
  *gmtoff = os_gmtoff();
#endif
}

//
// Remplit la structure avec les date/heure actuelles
void set_current_tm(struct tm *ts) {
  time_t ltime = time(NULL);
  *ts = *localtime(&ltime);
}

//
//
//
void set_log_timestamp(char *s, size_t s_len,
                       int year, int month, int day, int hour, int minute, int second, long int usec) {
  if (g_log_usec && usec >= 0) {
    snprintf(s, s_len, "%02i/%02i/%02i %02i:%02i:%02i.%06lu", g_date_df ? day : month, g_date_df ? month : day,
      year % 100, hour, minute, second, usec);
  } else {
    snprintf(s, s_len, "%02i/%02i/%02i %02i:%02i:%02i", g_date_df ? day : month, g_date_df ? month : day,
      year % 100, hour, minute, second);
  }
  s[s_len - 1] = '\0';
}

//
// Prepare prefix string, used by my_log only
//
void my_log_core_get_dt_str(const logdisp_t log_disp, char *dt, size_t dt_len) {
  int wday; int year; int month; int day;
  int hour; int minute; int second; long unsigned int usec;
  long int gmtoff;
  get_datetime_of_day(&wday, &year, &month, &day, &hour, &minute, &second, &usec, &gmtoff);

  set_log_timestamp(dt, dt_len, year, month, day, hour, minute, second, usec);
  size_t l = strlen(dt);

  if (log_disp == LP_NOTHING) {
    strncpy(dt, "", l);
  } else if (log_disp == LP_INDENT) {
    memset(dt, ' ', l);
  }
}

//
// Output log string, used by my_log only
//
void my_log_core_output(const char *s, size_t dt_len) {
  my_pthread_mutex_lock(&util_mutex);

  if (log_fd) {
    fputs(s, log_fd);
    fputs("\n", log_fd);
    fflush(log_fd);
  }
  if (g_print_log) {
    const char *t = s;
    size_t i = 0;
    for (i = 0; i < dt_len; ++i) {
      if (*t == '\0')
        break;
      ++t;
    }
    puts(t);
    fflush(stdout);
  }

  my_pthread_mutex_unlock(&util_mutex);
}

//
// Output a string in the program log
//
void my_logs(const loglevel_t log_level, const logdisp_t log_disp, const char *s) {
  if (log_level > g_current_log_level)
    return;

  char dt[REGULAR_STR_STRBUFSIZE];

  my_log_core_get_dt_str(log_disp, dt, sizeof(dt));
  strncat(dt, LOG_AFTER_TIMESTAMP, sizeof(dt));
  size_t dt_len = strlen(dt);
  strncat(dt, s, sizeof(dt));
  my_log_core_output(dt, dt_len);
}

//
// Output a formatted string in the program log
//
void my_logf(const loglevel_t log_level, const logdisp_t log_disp, const char *format, ...) {

  if (log_level > g_current_log_level)
    return;

  char dt[REGULAR_STR_STRBUFSIZE];
  char str[REGULAR_STR_STRBUFSIZE];
  my_log_core_get_dt_str(log_disp, dt, sizeof(dt));
  va_list args;
  va_start(args, format);
  vsnprintf(str, sizeof(str), format, args);
  va_end(args);
  strncat(dt, LOG_AFTER_TIMESTAMP, sizeof(dt));
  size_t dt_len = strlen(dt);
  strncat(dt, str, sizeof(dt));
  my_log_core_output(dt, dt_len);
}

//
// Return true if s begins with prefix, false otherwise
// String comparison is case insensitive
//
int s_begins_with(const char *s, const char *begins_with) {
  return (strncasecmp(s, begins_with, strlen(begins_with)) == 0);
}

//
// Connect to a remote host, with a timeout
// Return EC_* code.
//
int conn_connect(const struct sockaddr_in *server, connection_t *conn, struct timeval *tv, const char *desc, const char *prefix) {
  fd_set fdset;
  FD_ZERO(&fdset);
  FD_SET((unsigned int)(conn->sock), &fdset);

  os_set_sock_nonblocking_mode(conn->sock);

  char s_err[ERR_STR_BUFSIZE];

  int cr = CONNRES_OK;
  if (connect(conn->sock, (struct sockaddr *)server, sizeof(*server)) == CONNECT_ERROR) {
    if (os_last_network_op_is_in_progress()) {
      if (select((conn->sock) + 1, NULL, &fdset, NULL, tv) <= 0) {
        my_logf(LL_ERROR, LP_DATETIME, "%s timeout connecting to %s, %s", prefix, desc, os_last_err_desc(s_err, sizeof(s_err)));
        cr = CONNRES_CONNECTION_TIMEOUT;
      } else {
        char so_error;
        socklen_t len = sizeof(so_error);
        getsockopt(conn->sock, SOL_SOCKET, SO_ERROR, &so_error, &len);
        if (so_error != 0) {
          my_logf(LL_ERROR, LP_DATETIME, "%s network error connecting to %s, code=%i (%s)", prefix, desc, so_error, strerror(so_error));
        }
        cr = so_error != 0 ? CONNRES_NETIO : CONNRES_OK;
      }
    } else {
      my_logf(LL_ERROR, LP_DATETIME, "%s error connecting to %s, %s", prefix, desc, os_last_err_desc(s_err, sizeof(s_err)));
      cr = CONNRES_CONNECTION_ERROR;
    }
  } else {
    my_logf(LL_ERROR, LP_DATETIME, "%s unknown error connecting to %s", prefix, desc);
    cr = CONNRES_CONNECTION_ERROR;
  }

  if (cr != CONNRES_OK) {
    conn->sock = -1;
    return cr;
  }

  if (conn->type == CONNTYPE_PLAIN) {
    os_set_sock_blocking_mode(conn->sock);
    return CONNRES_OK;
  }

  int r;

    // Assume we are a v2 or v3 client
  if ((conn->ssl_context = SSL_CTX_new(SSLv23_client_method())) == NULL) {
    my_logf(LL_ERROR, LP_DATETIME, "%s SSL error: %d (%s)", prefix, ERR_get_error(), ssl_get_error(ERR_get_error(), s_err, sizeof(s_err)));
    cr = CONNRES_SSL_CONNECTION_ERROR;
  } else if ((conn->ssl_handle = SSL_new(conn->ssl_context)) == NULL) /* Create SSL connection */ {
    my_logf(LL_ERROR, LP_DATETIME, "%s SSL error: %d (%s)", prefix, ERR_get_error(), ssl_get_error(ERR_get_error(), s_err, sizeof(s_err)));
    cr =  CONNRES_SSL_CONNECTION_ERROR;
  } else if (!SSL_set_fd(conn->ssl_handle, conn->sock)) /* Connect the SSL struct to our connection */ {
    my_logf(LL_ERROR, LP_DATETIME, "%s SSL error: %d (%s)", prefix, ERR_get_error(), ssl_get_error(ERR_get_error(), s_err, sizeof(s_err)));
    cr = CONNRES_SSL_CONNECTION_ERROR;
  }
  
  if (cr == CONNRES_OK) {
    r = SSL_connect(conn->ssl_handle);  /* Initiate SSL handshake */
    dbg_write("r = %d, want_write = %d, want_read = %d, lasterr = %d\n", r, SSL_ERROR_WANT_WRITE, SSL_ERROR_WANT_READ, ERR_get_error());
    if (select((conn->sock) + 1, NULL, &fdset, NULL, tv) <= 0) {
      my_logf(LL_ERROR, LP_DATETIME, "%s timeout handshaking to %s, %s", prefix, desc, os_last_err_desc(s_err, sizeof(s_err)));
      cr = CONNRES_CONNECTION_TIMEOUT;
    } else {
      char so_error;
      socklen_t len = sizeof(so_error);
      getsockopt(conn->sock, SOL_SOCKET, SO_ERROR, &so_error, &len);
      if (so_error != 0) {
        my_logf(LL_ERROR, LP_DATETIME, "%s network error connecting to %s, code=%i (%s)", prefix, desc, so_error, strerror(so_error));
      }
      cr = so_error != 0 ? CONNRES_NETIO : CONNRES_OK;
    }
  }
  
  if (cr == CONNRES_OK) {
    my_logf(LL_DEBUG, LP_DATETIME, "%s SSL: handshake [%s] successful", prefix, desc);
    os_set_sock_blocking_mode(conn->sock);
  } else {
    conn_close(conn);
  }

  return cr;
}

//
// Establish a connection, including all what it takes ->
//    Host name resolution
//    TCP connection open
//    Check server answer
//
int conn_establish_connection(const char *host_name, int port, const char *expect, int timeout,
    connection_t *conn, const char *prefix, int trace) {
  my_logf(LL_DEBUG, LP_DATETIME, "%s connecting to %s:%i...", prefix, host_name, port);

  char server_desc[SMALLSTRSIZE];
  char s_err[ERR_STR_BUFSIZE];

  snprintf(server_desc, sizeof(server_desc), "%s:%i", host_name, port);

    // Resolving server name
  struct sockaddr_in server;
  struct hostent *hostinfo = NULL;
  my_logf(LL_DEBUG, LP_DATETIME, "Running gethosbyname() on %s", host_name);
  hostinfo = gethostbyname(host_name);
  if (hostinfo == NULL) {
    my_logf(LL_ERROR, LP_DATETIME, "Unknown host %s, %s", host_name, os_last_err_desc(s_err, sizeof(s_err)));
    return CONNRES_RESOLVE_ERROR;
  }

  int ret = CONNRES_CONNECTION_ERROR;

  if ((conn->sock = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP)) == SOCKET_ERROR) {
    fatal_error("socket() error to create connection socket, %s", os_last_err_desc(s_err, sizeof(s_err)));
  }
  server.sin_family = AF_INET;
  server.sin_port = htons((uint16_t)port);
  server.sin_addr = *(struct in_addr *)hostinfo->h_addr;
    // tv value is undefined after call to connect() as per documentation, so
    // it is to be re-set every time.
  struct timeval tv;
  tv.tv_sec = timeout;
  tv.tv_usec = 0;

  my_logf(LL_DEBUG, LP_DATETIME, "Will connect to %s:%i, timeout = %i", host_name, port, timeout);

  if ((ret = conn_connect(&server, conn, &tv, server_desc, prefix)) == CONNRES_OK) {
    my_logf(LL_DEBUG, LP_DATETIME, "Connected to %s", server_desc);

    if (expect != NULL && strlen(expect) >= 1) {
      char *response = NULL;
      int response_size;
      if (conn_read_line_alloc(conn, &response, trace, &response_size) < 0) {
        ;
      } else if (s_begins_with(response, expect)) {
        my_logf(LL_DEBUG, LP_DATETIME, "Received the expected answer: '%s' (expected '%s')", response, expect);
        ret = CONNRES_OK;
      } else {
        my_logf(LL_VERBOSE, LP_DATETIME, "Received an unexpected answer: '%s' (expected '%s')", response, expect);
        ret = CONNRES_UNEXPECTED_ANSWER;
      }
      free(response);
    } else {
      ret = CONNRES_OK;
    }

  }

  return ret;
}

//
// Receives a line from a socket (terminated by \015\010)
// Return -1 if an error occured, 1 if reading is successful,
// 0 if transmission is closed.
//
int conn_read_line_alloc(connection_t *conn, char **out, int trace, int *size) {
  const int INITIAL_READLINE_BUFFER_SIZE = 100;

  int i = 0;
  int cr = FALSE;
  char ch;
  int nb;

  if (*out == NULL) {
    *size = INITIAL_READLINE_BUFFER_SIZE;
    *out = (char *)malloc(*size);
  }

  for (;;) {
    if ((nb = conn->sock_read(conn, &ch, 1)) == SOCKET_ERROR) {
      char s_err[ERR_STR_BUFSIZE];
      my_logf(LL_ERROR, LP_DATETIME, "Error reading socket, error %s", os_last_err_desc(s_err, sizeof(s_err)));
      conn_close(conn);
      return -1;
    }

    if (i >= *size) {
      if (*size * 2 <= MAX_READLINE_SIZE) {
        *size *= 2;
        *out = (char *)realloc(*out, *size);
      } else {
        (*out)[*size - 1] = '\0';
        break;
      }
    }

    if (nb == 0) {
      (*out)[i] = '\0';
      break;
    }

    if (ch == '\n') {
      if (cr && i > 0) {
        i--;
      }
      (*out)[i] = '\0';
      break;
    } else {
      cr = (ch == '\r' ? TRUE : FALSE);
      (*out)[i] = ch;
    }
    i++;
  }

  if (nb == 0) {
    return 0;
  } else {
    if (trace) {
      my_logf(LL_DEBUGTRACE, LP_DATETIME, "%s%s", conn->log_prefix_received, *out);
    }
    return 1;
  }
}

//
// Send a line to a socket
// Return 0 if OK, -1 if error.
// Manage logging an error code and closing socket.
//
int conn_line_sendf(connection_t *conn, int trace, const char *fmt, ...) {

  if (conn->sock == -1) {
    return -1;
  }

    // FIXME, used to be malloc'ed but the instruction free(tmp) (later)
    // crashes the code...
/*  int l = strlen(fmt) + 100;*/
/*  char *tmp;*/
/*  tmp = (char *)malloc(l + 1);*/
  char tmp[1000];

  va_list args;
  va_start(args, fmt);
/*  vsnprintf(tmp, l, fmt, args);*/
  vsnprintf(tmp, sizeof(tmp), fmt, args);
  va_end(args);

  if (trace)
    my_logf(LL_DEBUGTRACE, LP_DATETIME, "%s%s", conn->log_prefix_sent, tmp);

  strncat(tmp, "\015\012", sizeof(tmp));

  int e = conn->sock_write(conn, tmp, strlen(tmp));

/*  free(tmp);*/

  if (e == SOCKET_ERROR) {
    char s_err[ERR_STR_BUFSIZE];
    my_logf(LL_ERROR, LP_DATETIME, "Network I/O error: %s", os_last_err_desc(s_err, sizeof(s_err)));
    conn_close(conn);
    return -1;
  }

  return 0;
}

//
// Send a string to a socket and chck answer (telnet-style communication)
//
int conn_round_trip(connection_t *conn, const char *expect, int trace, const char *fmt, ...) {
  int l = strlen(fmt) + 100;
  char *tmp;
  tmp = (char *)malloc(l + 1);

  va_list args;
  va_start(args, fmt);
  vsnprintf(tmp, l, fmt, args);
  va_end(args);

  int e = conn_line_sendf(conn, trace, tmp);
  free(tmp);
  if (e)
    return CONNRES_NETIO;

  char *response = NULL;
  int response_size;
  if (conn_read_line_alloc(conn, &response, trace, &response_size) < 0) {
    free(response);
    return CONNRES_NETIO;
  }

  if (s_begins_with(response, expect)) {
    free(response);
    return CONNRES_OK;
  }

  free(response);
  return CONNRES_UNEXPECTED_ANSWER;
}

//
// Sets the string passed as argument to the last SSL error
//
char *ssl_get_error(const unsigned long e, char *s, const size_t s_len) {
  ERR_error_string_n(e, s, s_len);
  return s;
}

//
// Initializes the connection_t object
//
void conn_init(connection_t *conn, int type) {
  conn->type = type;
  conn->sock = -1;
  conn->ssl_handle = NULL;
  conn->ssl_context = NULL;

  conn->sock_read = connection_table[conn->type].sock_read;
  conn->log_prefix_received = connection_table[conn->type].log_prefix_received;
  conn->sock_write = connection_table[conn->type].sock_write;
  conn->log_prefix_sent = connection_table[conn->type].log_prefix_sent;
}

//
// Closes the connection
//
void conn_close(connection_t *conn) {
  os_closesocket(conn->sock);
  if (conn->ssl_handle != NULL) {
    SSL_shutdown(conn->ssl_handle);
    SSL_free(conn->ssl_handle);
    conn->ssl_handle = NULL;
  }
  if (conn->ssl_context != NULL) {
    SSL_CTX_free(conn->ssl_context);
    conn->ssl_context = NULL;
  }
  conn->sock = -1;
}

//
// CONNTYPE_PLAIN -> read sock
//
ssize_t conn_plain_read(connection_t *conn, void *buf, const size_t buf_len) {
  return recv(conn->sock, buf, buf_len, 0);
}

//
// CONNTYPE_PLAIN -> write sock
//
ssize_t conn_plain_write(connection_t *conn, void *buf, const size_t buf_len) {
  return send(conn->sock, buf, buf_len, 0);
}

//
// CONNTYPE_SSL -> read sock
//
ssize_t conn_ssl_read(connection_t *conn, void *buf, const size_t buf_len) {
  return SSL_read(conn->ssl_handle, buf, buf_len);
}

//
// CONNTYPE_SSL -> write sock
//
ssize_t conn_ssl_write(connection_t *conn, void *buf, const size_t buf_len) {
  return SSL_write(conn->ssl_handle, buf, buf_len);
}

