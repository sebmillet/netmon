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

#define PREFIX_RECEIVED "<<< "
#define PREFIX_SENT ">>> "

loglevel_t g_current_log_level = LL_NORMAL;

char g_log_file[SMALLSTRSIZE];
long int g_print_subst_error = DEFAULT_PRINT_SUBST_ERROR;
int g_date_df = (DEFAULT_DATE_FORMAT == DF_FRENCH);
int g_print_log = DEFAULT_PRINT_LOG;
long int g_log_usec = DEFAULT_LOG_USEC;
FILE *log_fd;

static pthread_mutex_t util_mutex;

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

void os_set_sock_nonblocking_mode(int sock) {
  u_long iMode = 1;
  int iResult = ioctlsocket(sock, FIONBIO, &iMode);
  if (iResult != NO_ERROR)
    fatal_error("ioctlsocket failed with error: %ld", iResult);
}

void os_set_sock_blocking_mode(int sock) {
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

void os_closesocket(int sock) {
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

void os_closesocket(int sock) {
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
// Return 0 if success, a non-zero value if failure
//
int connect_with_timeout(const struct sockaddr_in *server, int *connection_sock, struct timeval *tv, const char *desc) {
  fd_set fdset;
  FD_ZERO(&fdset);
  FD_SET((unsigned int)*connection_sock, &fdset);

  os_set_sock_nonblocking_mode(*connection_sock);

  char s_err[ERR_STR_BUFSIZE];

  int res = 0;
  if (connect(*connection_sock, (struct sockaddr *)server, sizeof(*server)) == CONNECT_ERROR) {
    if (os_last_network_op_is_in_progress()) {
      if (select((*connection_sock) + 1, NULL, &fdset, NULL, tv) <= 0) {
        my_logf(LL_ERROR, LP_DATETIME, "Timeout connecting to %s, %s", desc, os_last_err_desc(s_err, sizeof(s_err)));
        res = 1;
      } else {
        char so_error;
        socklen_t len = sizeof(so_error);
        getsockopt(*connection_sock, SOL_SOCKET, SO_ERROR, &so_error, &len);
        if (so_error != 0) {
          my_logf(LL_ERROR, LP_DATETIME, "Socket error connecting to %s, code=%i (%s)", desc, so_error, strerror(so_error));
        }
        res = (so_error != 0);
      }
    } else {
      my_logf(LL_ERROR, LP_DATETIME, "Error connecting to %s, %s", desc, os_last_err_desc(s_err, sizeof(s_err)));
      res = 1;
    }
  } else {
    abort();
  }

  os_set_sock_blocking_mode(*connection_sock);

  return res;
}

//
// Receives a line from a socket (terminated by \015\010)
// Return -1 if an error occured, 1 if reading is successful,
// 0 if transmission is closed.
//
int socket_read_line_alloc(int sock, char **out, int trace, int *size) {
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
    if ((nb = recv(sock, &ch, 1, 0)) == SOCKET_ERROR) {
      char s_err[ERR_STR_BUFSIZE];
      my_logf(LL_ERROR, LP_DATETIME, "Error reading socket, error %s", os_last_err_desc(s_err, sizeof(s_err)));
      os_closesocket(sock);
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
      my_logf(LL_VERBOSE, LP_DATETIME, PREFIX_RECEIVED "%s", *out);
    }
    return 1;
  }
}

//
// Send a line to a socket
// Return 0 if OK, -1 if error.
// Manage logging an error code and closing socket.
//
int socket_line_sendf(int *s, int trace, const char *fmt, ...) {

  if (*s == -1) {
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
    my_logf(LL_VERBOSE, LP_DATETIME, PREFIX_SENT "%s", tmp);

  strncat(tmp, "\015\012", sizeof(tmp));

  int e = send(*s, tmp, strlen(tmp), 0);

/*  free(tmp);*/

  if (e == SOCKET_ERROR) {
    char s_err[ERR_STR_BUFSIZE];
    my_logf(LL_ERROR, LP_DATETIME, "Network I/O error: %s", os_last_err_desc(s_err, sizeof(s_err)));
    os_closesocket(*s);
    *s = -1;
    return -1;
  }

  return 0;
}

//
// Send a string to a socket and chck answer (telnet-style communication)
//
int socket_round_trip(int sock, const char *expect, int trace, const char *fmt, ...) {
  int l = strlen(fmt) + 100;
  char *tmp;
  tmp = (char *)malloc(l + 1);

  va_list args;
  va_start(args, fmt);
  vsnprintf(tmp, l, fmt, args);
  va_end(args);

  int e = socket_line_sendf(&sock, trace, tmp);
  free(tmp);
  if (e)
    return SRT_SOCKET_ERROR;

  char *response = NULL;
  int response_size;
  if (socket_read_line_alloc(sock, &response, trace, &response_size) < 0) {
    free(response);
    return SRT_SOCKET_ERROR;
  }

  if (s_begins_with(response, expect)) {
    free(response);
    return SRT_SUCCESS;
  }

  free(response);
  return SRT_UNEXPECTED_ANSWER;
}

//
// Sets the string passed as argument to the last SSL error
//
char *ssl_get_error(const unsigned long e, char *s, const size_t s_len) {
  ERR_error_string_n(e, s, s_len);
  return s;
}

