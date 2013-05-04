// netmon.c

// Copyright Sébastien Millet, 2013

#include "netmon.h"

#include <stdio.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/types.h>
#include <sys/time.h>

extern char const st_undef[];
extern size_t const st_undef_len;
extern char const st_unknown[];
extern size_t const st_unknown_len;
extern char const st_ok[];
extern size_t const st_ok_len;
extern char const st_fail[];
extern size_t const st_fail_len;

/*#define DEBUG*/

loglevel_t current_log_level = LL_NORMAL;

#if defined(_WIN32) || defined(_WIN64)

  // WINDOWS
#include <winsock2.h>

#else

  // NOT WINDOWS
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>

#endif

#include <signal.h>
#include <ctype.h>
#include <getopt.h>
#include <time.h>

#define SMALLSTRSIZE  200
#define BIGSTRSIZE    1000

enum {ST_UNDEF = 0, ST_UNKNOWN = 1, ST_OK = 2, ST_FAIL = 3, ST_LAST = 3};
const char ST_TO_CHAR[] = {
  ' ', // ST_UNDEF
  '?', // ST_UNKNOWN
  '.', // ST_OK
  'X'  // ST_FAIL
};
const char *ST_TO_STR2[] = {
  "  ", // ST_UNDEF
  "??", // ST_UNKNOWN
  "ok", // ST_OK
  "KO"  // ST_FAIL
};
const char *ST_TO_LONGSTR[] = {
  "<undef>",  // ST_UNDEF
  "** ?? **", // ST_UNKNOWN
  "ok",       // ST_OK
  "** KO **"  // ST_FAIL
};
const char *ST_TO_LONGSTR_FORHTML[] = {
  "Undefined",  // ST_UNDEF
  "Unknown",    // ST_UNKNOWN
  "Ok",         // ST_OK
  "Fail"        // ST_FAIL
};

struct img_file_t {
  const char *file_name;
  const char *var;
  size_t var_len;
};
struct img_file_t img_files[] = {
  {"st-undef.png", NULL, 0},    // ST_UNDEF
  {"st-unknown.png", NULL, 0},  // ST_UNKNOWN
  {"st-ok.png", NULL, 0},       // ST_OK
  {"st-fail.png", NULL, 0}      // ST_FAIL
};

/*const char *ST_TO_IMAGENAME_FORHTML[] = {*/
/*  "st-undef.png",   // ST_UNDEF*/
/*  "st-unknown.png", // ST_UNKNOWN*/
/*  "st-ok.png",      // ST_OK*/
/*  "st-fail.png"     // ST_FAIL*/
/*};*/
const char *ST_TO_BGCOLOR_FORHTML[] = {
  "#FFFFFF", // ST_UNDEF
  "#B0B0B0", // ST_UNKNOWN
  "#00FF00", // ST_OK
  "#FF0000"  // ST_FAIL
};

char g_html_title[SMALLSTRSIZE] = PACKAGE_STRING;
int g_html_title_set = FALSE;

#define MAX_CHECKS 2000
struct check_t checks[MAX_CHECKS];
int g_nb_checks = 0;
int nb_valid_checks = 0;

const char *PREFIX_RECEIVED = "<<< ";
const char *PREFIX_SENT = ">>> ";

const char *DEFAULT_LOGFILE = PACKAGE_TARNAME ".log";
char g_log_file[SMALLSTRSIZE];

const char *DEFAULT_CFGFILE = PACKAGE_TARNAME ".ini";
char g_cfg_file[SMALLSTRSIZE];

#define DEFAULT_CHECK_INTERVAL 180
long int g_check_interval;
int g_check_interval_set = FALSE;
#define DEFAULT_NB_KEEP_LAST_STATUS 20
long int g_nb_keep_last_status = -1;
int g_nb_keep_last_status_set = FALSE;

#define DEFAULT_DISPLAY_NAME_WIDTH 20
long int g_display_name_width = DEFAULT_DISPLAY_NAME_WIDTH;
int g_display_name_width_set = FALSE;

#define DEFAULT_HTML_REFRESH_PERIOD 20
long int g_html_refresh_interval = DEFAULT_HTML_REFRESH_PERIOD;
int g_html_refresh_interval_set = FALSE;

#define DEFAULT_HTML_NB_COLUMNS 2
long int g_html_nb_columns = DEFAULT_HTML_NB_COLUMNS;
int g_html_nb_columns_set = FALSE;

long int g_buffer_size = DEFAULT_BUFFER_SIZE;
int g_buffer_size_set = FALSE;
long int g_connect_timeout = DEFAULT_CONNECT_TIMEOUT;
int g_connect_timeout_set = FALSE;
int telnet_log = FALSE;
int g_print_log = FALSE;
int g_print_status = FALSE;
int g_test_mode = FALSE;

FILE *log_fd;

int flag_interrupted = FALSE;
int quitting = FALSE;

#ifdef DEBUG
void dbg_write(const char *fmt, ...) {
  va_list args;
  va_start(args, fmt);
  vprintf(fmt, args);
  va_end(args);
}
#else
#define dbg_write(...)
#endif

const char *TERM_CLEAR_SCREEN = "\033[2J\033[1;1H";

#if defined(_WIN32) || defined(_WIN64)


  // * ******* *
  // * WINDOWS *
  // * ******* *

const char FS_SEPARATOR = '\\';
#define DEFAULT_HTML_DIRECTORY (".")
#define DEFAULT_HTML_FILE (PACKAGE_NAME ".html")

void os_sleep(int seconds) {
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

#define strcasecmp _stricmp
#define strncasecmp _strnicmp

#else


  // * *********** *
  // * NOT WINDOWS *
  // * *********** *

const char FS_SEPARATOR = '/';
#define DEFAULT_HTML_DIRECTORY (".")
#define DEFAULT_HTML_FILE (PACKAGE_NAME ".html")

void os_sleep(int seconds) {
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
}

int os_last_network_op_is_in_progress() {
  return (errno == EINPROGRESS);
}

void os_closesocket(int sock) {
  close(sock);
}

#endif


  // * *** *
  // * ALL *
  // * *** *

char g_html_directory[BIGSTRSIZE] = DEFAULT_HTML_DIRECTORY;
int g_html_directory_set = FALSE;
char g_html_file[SMALLSTRSIZE] = DEFAULT_HTML_FILE;
int g_html_file_set = FALSE;
char g_html_complete_file_name[BIGSTRSIZE];

#define CFGK_COMMENT_CHAR ';'
const char *CFG_S_GENERAL_STR  = "general";
const char *CFG_S_TCPCHECK_STR = "tcp-probe";

enum {CFG_S_NONE, CFG_S_GENERAL, CFG_S_TCPPROBE};
struct readcfg_var_t {
  const char *name;
  int section;
  long int *plint_target;
  char **p_pchar_target;
  char *pchar_target;
  size_t char_target_len;
  int *pint_var_set;
  int allow_null;
};

struct check_t chk00;
const struct readcfg_var_t readcfg_vars[] = {
  {"display_name", CFG_S_TCPPROBE, NULL, &(chk00.display_name), NULL, 0, &(chk00.display_name_set), FALSE},
  {"host_name", CFG_S_TCPPROBE, NULL, &(chk00.host_name), NULL, 0, &(chk00.host_name_set), FALSE},
  {"port", CFG_S_TCPPROBE, &(chk00.port), NULL, NULL, 0, &(chk00.port_set), FALSE},
  {"expect", CFG_S_TCPPROBE, NULL, &(chk00.expect), NULL, 0, &(chk00.expect_set), FALSE},
  {"check_interval", CFG_S_GENERAL, &g_check_interval, NULL, NULL, 0, &g_check_interval_set, TRUE},
  {"buffer_size", CFG_S_GENERAL, &g_buffer_size, NULL, NULL, 0, &g_buffer_size_set, FALSE},
  {"connect_timeout", CFG_S_GENERAL, &g_connect_timeout, NULL, NULL, 0, &g_connect_timeout_set, FALSE},
  {"keep_last_status", CFG_S_GENERAL, &g_nb_keep_last_status, NULL, NULL, 0, &g_nb_keep_last_status_set, TRUE},
  {"display_name_width", CFG_S_GENERAL, &g_display_name_width, NULL, NULL, 0, &g_display_name_width_set, FALSE},
  {"html_refresh_interval", CFG_S_GENERAL, &g_html_refresh_interval, NULL, NULL, 0, &g_html_refresh_interval_set, FALSE},
  {"html_title", CFG_S_GENERAL, NULL, NULL, g_html_title, sizeof(g_html_title), &g_html_title_set, FALSE},
  {"html_directory", CFG_S_GENERAL, NULL, NULL, g_html_directory, sizeof(g_html_directory), &g_html_directory_set, FALSE},
  {"html_file", CFG_S_GENERAL, NULL, NULL, g_html_file, sizeof(g_html_file), &g_html_file_set, FALSE},
  {"html_nb_columns", CFG_S_GENERAL, &g_html_nb_columns, NULL, NULL, 0, &g_html_nb_columns_set, FALSE}
};

//
// My implementation of getline()
//
ssize_t my_getline(char **lineptr, size_t *n, FILE *stream) {

#define MY_GETLINE_INITIAL_ALLOCATE 2
#define MY_GETLINE_MIN_INCREASE   4
#define MY_GETLINE_COEF_INCREASE    1

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

      *lineptr = (char *)realloc(*lineptr, *n);

      if (*lineptr == NULL) {
        errno = ENOMEM;
        return -1;
      }
    }

      // Now read one character from stream
    c = getc(stream);

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
// Destroy a check struct
//
void check_t_destroy(struct check_t *chk) {
  dbg_write("mark a\n");
  if (chk->display_name != NULL)
    free(chk->display_name);
  dbg_write("mark b\n");
  if (chk->host_name != NULL)
    free(chk->host_name);
  dbg_write("mark c\n");
  if (chk->expect != NULL)
    free(chk->expect);
  dbg_write("mark d\n");
  if (chk->str_prev_status != NULL)
    free(chk->str_prev_status);
  dbg_write("mark e\n");
}

//
//
//
void blank_time_last_status_change(struct check_t *chk) {
  strncpy(chk->time_last_status_change, "", sizeof(chk->time_last_status_change));
  chk->h_time_last_status_change = -1;
  chk->m_time_last_status_change = -1;
}

//
// Create a check struct
//
void check_t_create(struct check_t *chk) {
  chk->is_valid = FALSE;

  chk->display_name_set = FALSE;
  chk->display_name = NULL;

  chk->host_name_set = FALSE;
  chk->host_name = NULL;

  chk->expect_set = FALSE;
  chk->expect = NULL;

  chk->port_set = FALSE;

  chk->status = ST_UNDEF;
  chk->prev_status = ST_UNDEF;
  chk->str_prev_status = NULL;

  blank_time_last_status_change(chk);
}

//
// Prepare a check_t (final prep)
//
void check_t_getready(struct check_t *chk) {
  if (!chk->is_valid)
    return;
  if (chk->str_prev_status != NULL)
    return;
  if (g_nb_keep_last_status >= 1) {
    chk->str_prev_status = (char *)malloc(g_nb_keep_last_status + 1);
    memset(chk->str_prev_status, ST_TO_CHAR[ST_UNDEF], g_nb_keep_last_status);
    chk->str_prev_status[g_nb_keep_last_status] = '\0';
  }
}

//
// Free pointers in the checks variables
//
void clean_checks() {
  int i;
  for (i = 0; i < g_nb_checks; ++i) {
    dbg_write("cleaning check #%i\n", i);
    check_t_destroy(&checks[i]);
  }
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

  char *str = (char *)malloc(REGULAR_STR_STRBUFSIZE);
  vsnprintf(str, REGULAR_STR_STRBUFSIZE, format, args);
  strncat(str, "\n", REGULAR_STR_STRBUFSIZE);
  fprintf(stderr, str, NULL);
  free(str);
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
// Get date/time of day
//
void get_datetime_of_day(int *year, int *month, int *day, int *hour, int *minute, int *second, long unsigned int *usec) {
  time_t ltime = time(NULL);
  struct tm ts;
  ts = *localtime(&ltime);

  struct timeval tv;
  struct timezone tz;
  if (gettimeofday(&tv, &tz) == GETTIMEOFDAY_ERROR) {
    char s_err[ERR_STR_BUFSIZE];
    fatal_error("gettimeofday() error, %s", os_last_err_desc(s_err, sizeof(s_err)));
  }

  *year = ts.tm_year;
  *month = ts.tm_mon + 1;
  *day = ts.tm_mday;
  *hour = ts.tm_hour;
  *minute = ts.tm_min;
  *second = ts.tm_sec;
  *usec = tv.tv_usec;
}

//
// Prepare prefix string, used by my_log only
//
void my_log_core_get_dt_str(const logdisp_t log_disp, char *dt, size_t dt_bufsize, size_t *dt_len) {
  int year; int month; int day;
  int hour; int minute; int second; long unsigned int usec;
  get_datetime_of_day(&year, &month, &day, &hour, &minute, &second, &usec);

  snprintf(dt, dt_bufsize, "%02i/%02i/%02i %02i:%02i:%02i.%06lu  ", day, month, year % 100, hour, minute, second, usec);
  *dt_len = strlen(dt);
  if (log_disp == LP_NOTHING) {
    strncpy(dt, "", dt_bufsize);
  } else if (log_disp == LP_INDENT) {
    memset(dt, ' ', *dt_len);
  }
}

//
// Output log string, used by my_log only
//
void my_log_core_output(const char *s, size_t dt_len) {
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
}

//
// Output a string in the program log
//
void my_logs(const loglevel_t log_level, const logdisp_t log_disp, const char *s) {
  if (log_level > current_log_level)
    return;

  char *dt = (char *)malloc(REGULAR_STR_STRBUFSIZE);
  size_t dt_len;

  my_log_core_get_dt_str(log_disp, dt, REGULAR_STR_STRBUFSIZE, &dt_len);
  strncat(dt, s, REGULAR_STR_STRBUFSIZE);
  my_log_core_output(dt, dt_len);

  free(dt);
}

//
// Output a formatted string in the program log
//
void my_logf(const loglevel_t log_level, const logdisp_t log_disp, const char *format, ...) {
  va_list args;
  va_start(args, format);

  if (log_level > current_log_level)
    return;

  char *dt = (char *)malloc(REGULAR_STR_STRBUFSIZE);
  size_t dt_len;
  char *str = (char *)malloc(REGULAR_STR_STRBUFSIZE);

  my_log_core_get_dt_str(log_disp, dt, REGULAR_STR_STRBUFSIZE, &dt_len);

  vsnprintf(str, REGULAR_STR_STRBUFSIZE, format, args);
  strncat(dt, str, REGULAR_STR_STRBUFSIZE);
  my_log_core_output(dt, dt_len);

  free(str);
  free(dt);

  va_end(args);
}

//
// Log a telnet line
//
void my_log_telnet(const int is_received, const char *s) {
  char prefix[50];
  strncpy(prefix, is_received ? PREFIX_RECEIVED : PREFIX_SENT, sizeof(prefix));
  size_t m = strlen(prefix) + strlen(s) + 1;
  char *tmp = (char *)malloc(m);
  strncpy(tmp, prefix, m);
  strncat(tmp, s, m);
  my_logs(LL_NORMAL, LP_DATETIME, tmp);
  free(tmp);
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
int socket_read_line_alloc(int sock, char** out, int trace) {
  const int INITIAL_READLINE_BUFFER_SIZE = 100;

  int size = INITIAL_READLINE_BUFFER_SIZE;
  int i = 0;
  int cr = FALSE;
  char ch;
  int nb;

  *out = (char *)malloc(size);

  for (;;) {
    if ((nb = recv(sock, &ch, 1, 0)) == SOCKET_ERROR) {
      char s_err[ERR_STR_BUFSIZE];
      my_logf(LL_ERROR, LP_DATETIME, "Error reading socket, error %s", os_last_err_desc(s_err, sizeof(s_err)));
      os_closesocket(sock);
      return -1;
    }

    if (i >= size) {
      if (size * 2 <= MAX_READLINE_SIZE) {
        size *= 2;
        *out = (char *)realloc(*out, size);
      } else {
        (*out)[size - 1] = '\0';
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
      my_logf(LL_VERBOSE, LP_DATETIME, "<<< %s", *out);
    }
    return 1;
  }
}


//
// Return true if s begins with prefix, false otherwise
// String comparison is case insensitive
//
int s_begins_with(const char *s, const char *begins_with) {
  return (strncasecmp(s, begins_with, strlen(begins_with)) == 0);
}

//
// Fill a string with current date/time, format is
//    dd/mm hh:mm:ss
//
void get_str_now(char *s, size_t s_len) {
  int year; int month; int day;
  int hour; int minute; int second; long unsigned int usec;
  get_datetime_of_day(&year, &month, &day, &hour, &minute, &second, &usec);

  snprintf(s, s_len, "%02i/%02i %02i:%02i:%02i", day, month, hour, minute, second);
}

//
// Fill a string with current time, format is
//    hh:mm
//
void get_strnow_short_width(char *s, size_t s_len) {
  int year; int month; int day;
  int hour; int minute; int second; long unsigned int usec;
  get_datetime_of_day(&year, &month, &day, &hour, &minute, &second, &usec);

  snprintf(s, s_len, "%02i:%02i", hour, minute);
}

//
//
//
int perform_a_check(struct check_t *chk) {
  char server_desc[SMALLSTRSIZE];

    // String to store error descriptions
  char s_err[ERR_STR_BUFSIZE];

  struct timeval tv;

  snprintf(server_desc, sizeof(server_desc), "%s:%li", chk->host_name, chk->port);

    // Resolving server name
  struct sockaddr_in server;
  struct hostent *hostinfo = NULL;
  my_logf(LL_DEBUG, LP_DATETIME, "Running gethosbyname() on %s", chk->host_name);
  hostinfo = gethostbyname(chk->host_name);
  if (hostinfo == NULL) {
    my_logf(LL_ERROR, LP_DATETIME, "Unknown host %s, %s", chk->host_name, os_last_err_desc(s_err, sizeof(s_err)));
    return ST_UNKNOWN;
  }

  int status = ST_FAIL;

  int connection_sock;
  if ((connection_sock = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP)) == SOCKET_ERROR) {
    fatal_error("socket() error to create connection socket, %s", os_last_err_desc(s_err, sizeof(s_err)));
  }
  server.sin_family = AF_INET;
  server.sin_port = htons((uint16_t)chk->port);
  server.sin_addr = *(struct in_addr *)hostinfo->h_addr;
  my_logf(LL_DEBUG, LP_DATETIME, "Connecting to %s (%s)...", chk->display_name, server_desc);
    // tv value is undefined after call to connect() as per documentation, so
    // it is to be re-set every time.
  tv.tv_sec = g_connect_timeout;
  tv.tv_usec = 0;
  if (connect_with_timeout(&server, &connection_sock, &tv, server_desc) == 0) {
    my_logf(LL_VERBOSE, LP_DATETIME, "Connected to %s", server_desc);

    if (chk->expect_set && strlen(chk->expect) >= 1) {

      char *response;
      int read_res = 0;
      if ((read_res = socket_read_line_alloc(connection_sock, &response, current_log_level == LL_DEBUG)) < 0) {
        os_closesocket(connection_sock);
      } else if (s_begins_with(response, chk->expect)) {
        my_logf(LL_DEBUG, LP_DATETIME, "Expected answer: '%s'", response);
        status = ST_OK;
      } else {
        my_logf(LL_DEBUG, LP_DATETIME, "Unexpected answer: '%s' (expected '%s')", response, chk->expect);
      }
      free(response);

    } else {
      status = ST_OK;
    }

    os_closesocket(connection_sock);
    my_logf(LL_VERBOSE, LP_DATETIME, "Disconnected from %s", server_desc);
  }

  return status;

}

//
// Main loop
//
void almost_neverending_loop() {

  do {
    int II;

    my_logs(LL_NORMAL, LP_NOTHING, "");
    my_logs(LL_NORMAL, LP_DATETIME, "Starting check...");

    int year0; int month0; int day0;
    int hour0; int minute0; int second0; long unsigned int usec0;
    get_datetime_of_day(&year0, &month0, &day0, &hour0, &minute0, &second0, &usec0);

    for (II = 0; II < g_nb_checks; ++II) {
      struct check_t *chk = &checks[II];
      if (!chk->is_valid)
        continue;

      int status = perform_a_check(chk);
      if (status < 0 || status > ST_LAST)
        internal_error("almost_neverending_loop", __FILE__, __LINE__);

      chk->prev_status = chk->status;
      chk->status = status;

      if (chk->prev_status != chk->status && chk->prev_status != ST_UNDEF) {
        get_strnow_short_width(chk->time_last_status_change, sizeof(chk->time_last_status_change));
        chk->h_time_last_status_change = hour0;
        chk->m_time_last_status_change = minute0;
      }

        // The whole code below is used to erase the information "last status change"
        // 23 hours after, so that there is no confusion in the day.
        // Note that the test would work for any duration from 1 hour to 23 hours,
        // by replacing the 23 below.
      if (chk->h_time_last_status_change >= 0) {
          // M1 = Mark 1 = time at which the status last changed, minus 1 hour
          // Expressed in minutes since midnight.
        int M1 = ((chk->h_time_last_status_change + 23) % 24) * 60 + chk->m_time_last_status_change;
          // M2 = Mark 2 = time at which the status last changed
          // Expressed in minutes since midnight.
        int M2 = chk->h_time_last_status_change * 60 + chk->m_time_last_status_change;
          // MN = Mark N = now
          // Expressed in minutes since midnight.
        int MN = hour0 * 60 + minute0;
          // MN must be in the interval [M1, M2[, the test takes into account the case where
          // midnight is in [M1, M2[.
        if ((M1 < M2 && MN >= M1 && MN < M2) ||
          (M1 > M2 && (MN >= M1 || MN < M2))) {
          blank_time_last_status_change(chk);
        }
      }

      my_logf(LL_NORMAL, LP_DATETIME, "%s -> %s", chk->display_name, ST_TO_LONGSTR[chk->status]);

      if (g_nb_keep_last_status >= 1) {
        char *buf = (char *)malloc(g_nb_keep_last_status + 1);
        strncpy(buf, chk->str_prev_status + 1, g_nb_keep_last_status + 1);
        strncpy(chk->str_prev_status, buf, g_nb_keep_last_status + 1);
        char t[] = "A";
        t[0] = ST_TO_CHAR[chk->status];
        strncat(chk->str_prev_status, t, g_nb_keep_last_status + 1);
        free(buf);
      }
    }

    int year1; int month1; int day1;
    int hour1; int minute1; int second1; long unsigned int usec1;
    get_datetime_of_day(&year1, &month1, &day1, &hour1, &minute1, &second1, &usec1);
    if (day1 < day0)
      day1 = day0 + 1;
    float elapsed = (day1 - day0) * 86400 + (hour1 - hour0) * 3600 + (minute1 - minute0) * 60 + (second1 - second0);
    elapsed += ((float)usec1 - (float)usec0) / 1000000;
    if (g_test_mode)
      elapsed = .12345;

    if (g_print_status) {
      const char *LC_PREFIX = "Last check: ";
      char now[20];
      get_str_now(now, sizeof(now));
      char duration[50];
      snprintf(duration, sizeof(duration), ", done in %6.3fs", elapsed);
      char sf[50];
      int l = g_nb_keep_last_status - strlen(now) - strlen(LC_PREFIX) - strlen(duration) + g_display_name_width + 5;
      snprintf(sf, sizeof(sf), "%%s %%s %%s%%%is%%s\n", l < 1 ? 1 : l);

      fputs(TERM_CLEAR_SCREEN, stdout);
      printf(sf, LC_PREFIX, now, duration, "", "LUPDT");
      printf("  LUPDT = Last status UPDated Time\n");
      printf("  Check interval = %li second%s", g_check_interval, g_check_interval >= 2 ? "s" : "");
      if (g_nb_keep_last_status >= 1)
        printf(", range = %li min", (g_check_interval * g_nb_keep_last_status) / 60);
      printf("\n");
      printf("  . = ok, X = fail, ? = unknown, <space> = undefined\n");
    }

    FILE *H = NULL;
    if (!g_test_mode) {
      H = fopen(g_html_complete_file_name, "w+");
      if (H == NULL)
        my_logf(LL_ERROR, LP_DATETIME, "Unable to open HTML output file %s", g_html_complete_file_name);
      else
        my_logf(LL_VERBOSE, LP_DATETIME, "Creating %s", g_html_complete_file_name);
    }
    if (H != NULL) {
      char now[20];
      get_str_now(now, sizeof(now));
      fputs("<!DOCTYPE HTML PUBLIC \"-//W3C//DTD HTML 4.01//EN\" \"http://www.w3.org/TR/html4/strict.dtd\">\n", H);
      fputs("<html>\n", H);
      fputs("<head>\n", H);
      fprintf(H, "<META HTTP-EQUIV=\"Refresh\" CONTENT=\"%li\">\n", g_html_refresh_interval);
      fputs("</head>\n", H);
      fputs("<body>\n", H);
      fprintf(H, "<h1>%s</h1>\n", g_html_title);
      fputs("<hr>\n", H);
      fprintf(H, "<h3>Last check: %s</h3>\n", now);
      fprintf(H, "<p>Last check done in %6.3fs<br>\n", elapsed);
      if (g_nb_keep_last_status >= 1) {
        fprintf(H, "Check interval = %li second%s, range = %li min<br>\n",
          g_check_interval, g_check_interval >= 2 ? "s" : "", (g_check_interval * g_nb_keep_last_status) / 60);
      }
      fputs("</p>", H);
      fputs("<table cellpadding=\"2\" cellspacing=\"1\" border=\"1\">\n", H);
      fputs("<tr>\n", H);
      int i;
      for (i = 0; i < g_html_nb_columns; ++i) {
        fputs("<td>Name</td><td>Status</td><td>Last *</td>", H);
        if (g_nb_keep_last_status >= 1)
          fputs("<td>History</td>", H);
          fputs("\n", H);
      }
      fputs("</tr>\n", H);
    }

    int counter = 0;
    for (II = 0; II < g_nb_checks; ++II) {
      struct check_t *chk = &checks[II];
      if (!chk->is_valid)
        continue;

      if (g_print_status) {
        char short_display_name[g_display_name_width + 1];
        strncpy(short_display_name, chk->display_name, g_display_name_width + 1);
        short_display_name[g_display_name_width] = '\0';

        char f[50];
        if (g_nb_keep_last_status >= 1) {
          snprintf(f, sizeof(f), "%%-%lis %%s |%%s| %%s\n", g_display_name_width);
          printf(f, short_display_name, ST_TO_STR2[chk->status], chk->str_prev_status, chk->time_last_status_change);
        } else {
          snprintf(f, sizeof(f), "%%-%lis %%s %%s\n", g_display_name_width);
          printf(f, short_display_name, ST_TO_STR2[chk->status], chk->time_last_status_change);
        }
      }

      if (H != NULL) {
        if (counter % g_html_nb_columns == 0)
          fputs("<tr>\n", H);
        fprintf(H, "<td>%s</td><td bgcolor=\"%s\">%s</td>\n",
          chk->display_name, ST_TO_BGCOLOR_FORHTML[chk->status], ST_TO_LONGSTR_FORHTML[chk->status]);
        fprintf(H, "<td>%s</td>", chk->time_last_status_change);
        if (g_nb_keep_last_status >= 1) {
          fputs("<td>", H);
          int i;
          for (i = 0; i < g_nb_keep_last_status; ++i) {
            char c = chk->str_prev_status[i];
            int j;
            for (j = 0; j <= ST_LAST; ++j) {
              if (ST_TO_CHAR[j] == c)
                break;
            }
            if (j > ST_LAST)
              j = ST_UNKNOWN;
            fprintf(H, "<img src=\"%s\">\n", img_files[j].file_name);
          }
          fprintf(H, "</td>\n");
        }
        if (counter % g_html_nb_columns == g_html_nb_columns - 1 || counter == nb_valid_checks - 1)
          fputs("</tr>\n", H);
      }
      ++counter;
    }

    if (H != NULL) {
      fputs("</table>\n", H);
      fputs("<p>* Time at which the status last changed\n</p>", H);
      fputs("</body>\n", H);
      fputs("</html>\n", H);
      fclose(H);
    }

    my_logf(LL_NORMAL, LP_DATETIME, "Check done in %fs", elapsed);

    if (g_check_interval && !g_test_mode) {
      my_logf(LL_VERBOSE, LP_DATETIME, "Now sleeping for %li second(s)", g_check_interval);
      os_sleep(g_check_interval);
    } else {
      break;
    }
  } while (1);
}

//
// Manage atexit()
//
void atexit_handler() {
  if (quitting)
    return;
  quitting = TRUE;

  clean_checks();

  my_logs(LL_NORMAL, LP_DATETIME, PACKAGE_NAME " stop");
  my_logs(LL_NORMAL, LP_NOTHING, "");
  my_log_close();
}

//
// Manage signals
//
void sigterm_handler(int sig) {
    // To avoid warning "unused parameter"
  (void)sig;

  flag_interrupted = TRUE;
  my_logs(LL_VERBOSE, LP_DATETIME, "Received TERM signal, quitting...");
  exit(EXIT_FAILURE);
}

void sigabrt_handler(int sig) {
    // To avoid warning "unused parameter"
  (void)sig;

  flag_interrupted = TRUE;
  my_logs(LL_VERBOSE, LP_DATETIME, "Received ABORT signal, quitting...");
  exit(EXIT_FAILURE);
}

void sigint_handler(int sig) {
    // To avoid warning "unused parameter"
  (void)sig;

  flag_interrupted = TRUE;
  my_logs(LL_VERBOSE, LP_DATETIME, "Received INT signal, quitting...");
  exit(EXIT_FAILURE);
}

//
// Manage errors with provided options
//
void option_error(const char *s) {
  fprintf(stderr, s, NULL);
  fprintf(stderr, "\nTry `" PACKAGE_NAME " --help' for more information.\n");
  exit(EXIT_FAILURE);
}

//
// Print a small help screen
//
void printhelp() {
  printf("Usage: " PACKAGE_NAME " [options...]\n");
  printf("Do TCP connection tests periodically and output status information\n\n");
  printf("  -h  --help          Display this help text\n");
  printf("  -V  --version       Display version information and exit\n");
  printf("  -v  --verbose       Be more talkative\n");
  printf("  -q  --quiet         Be less talkative\n");
  printf("  -l  --log-file      Log file (default: %s)\n", DEFAULT_LOGFILE);
  printf("  -c  --config-file   Configuration file (default: %s)\n", DEFAULT_CFGFILE);
  printf("  -p  --print-log     Print the log on the screen\n");
  printf("  -C  --stdout        Print status on stdout\n");
  printf("  -t  --test          Test mode\n");
}

//
// Print version information
//
void printversion() {
#ifdef DEBUG
  printf(PACKAGE_STRING "d\n");
#else
  printf(PACKAGE_STRING "\n");
#endif
  printf("Copyright 2013 Sébastien Millet\n");
	printf("This program is free software; you may redistribute it under the terms of\n");
	printf("the GNU General Public License version 3 or (at your option) any later version.\n");
	printf("This program has absolutely no warranty.\n");
}

void parse_options(int argc, char *argv[]) {

  static struct option long_options[] = {
    {"help", no_argument, NULL, 'h'},
    {"version", no_argument, NULL, 'V'},
    {"verbose", no_argument, NULL, 'v'},
    {"quiet", no_argument, NULL, 'q'},
    {"log-file", required_argument, NULL, 'l'},
    {"config-file", required_argument, NULL, 'c'},
    {"print-log", no_argument, NULL, 'p'},
    {"stdout", no_argument, NULL, 'C'},
    {"test", no_argument, NULL, 't'},
    {0, 0, 0, 0}
  };

  int c;
  int option_index = 0;

  strncpy(g_log_file, DEFAULT_LOGFILE, sizeof(g_log_file));
  strncpy(g_cfg_file, DEFAULT_CFGFILE, sizeof(g_cfg_file));

  while (1) {

    c = getopt_long(argc, argv, "hvCtl:c:pVq", long_options, &option_index);

    if (c == -1) {
      break;
    }

    switch (c) {

      case 'h':
        printhelp();
        exit(EXIT_FAILURE);

      case 'V':
        printversion();
        exit(EXIT_FAILURE);

      case 'c':
        strncpy(g_cfg_file, optarg, sizeof(g_cfg_file));
        break;

      case 'C':
        g_print_status = TRUE;
        break;

      case 't':
        g_test_mode = TRUE;
        break;

      case 'l':
        strncpy(g_log_file, optarg, sizeof(g_log_file));
        break;

      case 'p':
        g_print_log = TRUE;
        break;

      case 'v':
        current_log_level++;
        break;

      case 'q':
        current_log_level--;
        break;

      case '?':
        exit(EXIT_FAILURE);

      default:
        abort();
    }
  }
  if (optind < argc)
    option_error("Trailing options");
  if (current_log_level < LL_ERROR)
    current_log_level = LL_ERROR;
  if (current_log_level > LL_DEBUG)
    current_log_level = LL_DEBUG;
}

//
// Check a check variable
//
void check_t_check(struct check_t *chk, const char *cf, int line_number) {
  g_nb_checks++;

  int is_valid = TRUE;
  if (!chk->host_name_set) {
    my_logf(LL_ERROR, LP_DATETIME, "Configuration file '%s', section of line %i: no host name defined, discarding check", cf, line_number);
    is_valid = FALSE;
  }
  if (!chk->port_set) {
    my_logf(LL_ERROR, LP_DATETIME, "Configuration file '%s', section of line %i: no port defined, discarding check", cf, line_number);
    is_valid = FALSE;
  }

  chk->is_valid = is_valid;
  if (chk->is_valid)
    ++nb_valid_checks;

  if (!chk->display_name_set && chk->is_valid) {
    size_t n = strlen(chk->host_name) + 1;
    chk->display_name = (char *)malloc(n);
    strncpy(chk->display_name, chk->host_name, n);
    chk->display_name_set = TRUE;
  }
}

void read_configuration_file(const char *cf) {
  FILE *FCFG = NULL;
  if ((FCFG = fopen(cf, "r")) == NULL) {
    fatal_error("Configuration file '%s': unable to open", cf);
  }
  my_logf(LL_VERBOSE, LP_DATETIME, "Reading configuration from '%s'", cf);

  ssize_t nb_bytes;
  char *line = NULL;
  size_t len = 0;
  int line_number = 0;
  int check_line_number = -1;
  int read_status = CFG_S_NONE;

  int cur_check = -1;

  while ((nb_bytes = my_getline(&line, &len, FCFG)) != -1) {
    line[nb_bytes - 1] = '\0';
    ++line_number;

    char *b = line;
    while (isspace(*b)) b++;
    size_t blen = strlen(b);
    char *e;
    size_t slen;
    switch(*b) {
      case CFGK_COMMENT_CHAR:
      case '\0':
          // Empty line or starting with a ';' -> just ignore
        break;
      case '[':
        e = b + 1;
        slen = 0;
        while (*e != '\0' && *e != ']') {
          ++e;
          ++slen;
        }
        if (*e == '\0') {
          my_logf(LL_ERROR, LP_DATETIME, "Configuration file '%s', line %i: syntax error", cf, line_number);
        } else {
          char section_name[SMALLSTRSIZE + 1];
          strncpy(section_name, b + 1, SMALLSTRSIZE);
          section_name[slen] = '\0';
          my_logf(LL_DEBUG, LP_DATETIME, "Entering section '%s'", section_name);

          if (strcasecmp(section_name, CFG_S_GENERAL_STR) == 0) {
            read_status = CFG_S_GENERAL;
          } else if (strcasecmp(section_name, CFG_S_TCPCHECK_STR) == 0) {
            if (check_line_number >= 1) {
              if (cur_check < 0) {
                internal_error("read_configuration_file", __FILE__, __LINE__);
              }
              checks[cur_check] = chk00;
              check_t_check(&checks[cur_check], cf, check_line_number);
            } else if (cur_check >= 0) {
              internal_error("read_configuration_file", __FILE__, __LINE__);
            }
            ++cur_check;
            check_line_number = line_number;
            if (cur_check >= MAX_CHECKS) {
              --cur_check;
              my_logf(LL_ERROR, LP_DATETIME, "Configuration file '%s', line %i: reached max number of checks (%i)", cf, line_number, MAX_CHECKS);
              read_status = CFG_S_NONE;
            } else {
              read_status = CFG_S_TCPPROBE;
              check_t_create(&chk00);
            }
          } else {
            my_logf(LL_ERROR, LP_DATETIME, "Configuration file '%s', line %i: unknown section name '%s'", cf, line_number, section_name);
          }
        }
        break;
      default:
        e = b;
        slen = 0;
        while (*e != '\0' && *e != '=') {
          ++e;
          ++slen;
        }
        if (*e == '\0') {
          my_logf(LL_ERROR, LP_DATETIME, "Configuration file '%s', line %i: syntax error", cf, line_number);
        } else {

          size_t n = slen + 1;
          char *orig_key = (char *)malloc(n);
          char *key = orig_key;
          strncpy(key, b, n);
          key[slen] = '\0';
          key = trim(key);

          n = blen - slen + 1;
          char *orig_value = (char *)malloc(n);
          char *value = orig_value;
          strncpy(value, e + 1, n);
          value = trim(value);
          size_t l = strlen(value);
          if (value[0] == '"' && value[l - 1] == '"') {
            value[l - 1] = '\0';
            ++value;
          }
          if (value[0] == '\'' && value[l - 1] == '\'') {
            value[l - 1] = '\0';
            ++value;
          }

          if (strlen(key) == 0) {
            my_logf(LL_ERROR, LP_DATETIME, "Configuration file '%s', line %i: syntax error", cf, line_number);
          } else {
            int i;
            int match = FALSE;
            for (i = 0; (unsigned int)i < sizeof(readcfg_vars) / sizeof(*readcfg_vars); ++i) {
              if (strcasecmp(key, readcfg_vars[i].name) == 0) {
                match = TRUE;
                struct readcfg_var_t cfg = readcfg_vars[i];

/*                if (cfg.section == CFG_S_TCPPROBE) {*/
/*                  struct check_t *chk = &checks[cur_check < 0 ? 0 : cur_check];*/
/*                  if (cfg.plint_target != NULL) cfg.plint_target = (long int *)(((int *)cfg.plint_target - &chk00.is_valid) + &chk->is_valid);*/
/*                  if (cfg.p_pchar_target != NULL) cfg.p_pchar_target = (char **)(((int *)cfg.p_pchar_target - &chk00.is_valid) + &chk->is_valid);*/
/*                  if (cfg.pchar_target != NULL) cfg.pchar_target = (char *)(((int *)cfg.pchar_target - &chk00.is_valid) + &chk->is_valid);*/
/*                  cfg.pint_var_set = (cfg.pint_var_set - &chk00.is_valid) + &chk->is_valid;*/
/*                }*/

                if (read_status != cfg.section) {
                  my_logf(LL_ERROR, LP_DATETIME, "Configuration file '%s', line %i: variable %s not allowed in this section",
                    cf, line_number, key);
                } else {
                  long int n = 0;
                  if (cfg.plint_target != NULL)
                    n = atoi(value);
                  if (cfg.plint_target != NULL && strlen(value) == 0) {
                    my_logf(LL_ERROR, LP_DATETIME, "Configuration file '%s', line %i: empty value not allowed", cf, line_number);
                  } else if (cfg.plint_target != NULL && n == 0 && !cfg.allow_null) {
                    my_logf(LL_ERROR, LP_DATETIME, "Configuration file '%s', line %i: null value not allowed", cf, line_number);
                  } else if ((cfg.p_pchar_target != NULL || cfg.pchar_target != NULL) && strlen(value) == 0 && !cfg.allow_null) {
                    my_logf(LL_ERROR, LP_DATETIME, "Configuration file '%s', line %i: empty value not allowed", cf, line_number);
                  } else if (*cfg.pint_var_set) {
                    my_logf(LL_ERROR, LP_DATETIME, "Configuration file '%s', line %i: variable %s already defined", cf, line_number, key);
                  } else if (cfg.plint_target != NULL) {

                      // Variable of type long int
                    *cfg.plint_target = n;
                    *cfg.pint_var_set = TRUE;

                  } else if (cfg.p_pchar_target != NULL) {
                    
                      // Variable of type string, need to malloc
                    if (*cfg.p_pchar_target != NULL)
                      internal_error("read_configuration_file", __FILE__, __LINE__);
                    size_t nn = strlen(value) + 1;
                    *cfg.p_pchar_target = (char *)malloc(nn);
                    strncpy(*cfg.p_pchar_target, value, nn);
                    *cfg.pint_var_set = TRUE;

                  } else if (cfg.pchar_target != NULL) {

                      // Variable of type string, no need to malloc
                    strncpy(cfg.pchar_target, value, cfg.char_target_len);
                    *cfg.pint_var_set = TRUE;

                  } else {
                    internal_error("read_configuration_file", __FILE__, __LINE__);
                  }
                }
                break;
              }
            }
            if (!match) {
              my_logf(LL_ERROR, LP_DATETIME, "Configuration file '%s', line %i: unknown variable %s", cf, line_number, key);
            }
          }

          free(orig_value);
          free(orig_key);
        }
      break;
    }
  }
  if (cur_check >= 0) {
    if (check_line_number < 0)
      internal_error("read_configuration_file", __FILE__, __LINE__);
    checks[cur_check] = chk00;
    check_t_check(&checks[cur_check], cf, check_line_number);
  }

  if (g_nb_checks != cur_check + 1) {
    internal_error("read_configuration_file", __FILE__, __LINE__);
  }

  if (line != NULL)
    free(line);

  fclose(FCFG);

  if (!g_check_interval_set) {
    g_check_interval = DEFAULT_CHECK_INTERVAL;
    my_logf(LL_WARNING, LP_DATETIME, "check_interval not defined, taking default = %li", g_check_interval);
  }
  if (g_nb_keep_last_status < 0) {
    g_nb_keep_last_status = DEFAULT_NB_KEEP_LAST_STATUS;
    my_logf(LL_WARNING, LP_DATETIME, "keep_last_status not defined, taking default = %li", g_nb_keep_last_status);
  }

  strncpy(g_html_complete_file_name, g_html_directory, sizeof(g_html_complete_file_name));
  fs_concatene(g_html_complete_file_name, g_html_file, sizeof(g_html_complete_file_name));

/*  dbg_write("Output HTML file = %s\n", g_html_complete_file_name);*/

}

//
// Create files used for HTML display
//
void create_img_files() {
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
  for (i = 0; i <= ST_LAST; ++i) {
    strncpy(buf, g_html_directory, sizeof(buf));
    fs_concatene(buf, img_files[i].file_name, sizeof(buf));
    FILE *IMG = fopen(buf, "w+");

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
  }

}

int main(int argc, char *argv[]) {

  parse_options(argc, argv);

  atexit(atexit_handler);
  signal(SIGTERM, sigterm_handler);
  signal(SIGABRT, sigabrt_handler);
  signal(SIGINT, sigint_handler);

  my_log_open();
  my_logs(LL_NORMAL, LP_DATETIME, PACKAGE_STRING " start");

  read_configuration_file(g_cfg_file);

  int ii;
  for (ii = 0; ii < g_nb_checks; ++ii) {
    struct check_t *chk = &checks[ii];
    check_t_getready(chk);
  }

  my_logf(LL_VERBOSE, LP_DATETIME, "check_interval = %li", g_check_interval);
  my_logf(LL_VERBOSE, LP_DATETIME, "keep_last_status = %li", g_nb_keep_last_status);
  my_logf(LL_VERBOSE, LP_DATETIME, "display_name_width = %li", g_display_name_width);
  my_logf(LL_VERBOSE, LP_DATETIME, "html_directory = %s", g_html_directory);
  my_logf(LL_VERBOSE, LP_DATETIME, "html_file = %s", g_html_file);
  my_logf(LL_VERBOSE, LP_DATETIME, "html_title = %s", g_html_title);
  my_logf(LL_VERBOSE, LP_DATETIME, "html_refresh_interval = %li", g_html_refresh_interval);
  my_logf(LL_NORMAL, LP_DATETIME, "Valid check(s) defined: %i", nb_valid_checks);
  int i;
  int c = 0;
  for (i = 0; i < g_nb_checks; ++i) {
    struct check_t *chk = &checks[i];
    if (chk->is_valid) {
      ++c;
      my_logf(LL_DEBUG, LP_DATETIME, "== CHECK #%i", i);
    } else {
      my_logf(LL_DEBUG, LP_DATETIME, "!! check #%i (will be ignored)", i);
    }
    my_logf(LL_DEBUG, LP_DATETIME, "   is_valid     = %s", chk->is_valid ? "Yes" : "No");
    my_logf(LL_DEBUG, LP_DATETIME, "   display_name = %s", chk->display_name_set ? chk->display_name : "<unset>");
    my_logf(LL_DEBUG, LP_DATETIME, "   host_name    = %s", chk->host_name_set ? chk->host_name : "<unset>");

    if (chk->port_set)
      my_logf(LL_DEBUG, LP_DATETIME, "   port         = %i", chk->port);
    else
      my_logf(LL_DEBUG, LP_DATETIME, "   port         = <unset>");

    my_logf(LL_DEBUG, LP_DATETIME, "   expect       = %s", chk->expect_set ? chk->expect : "<unset>");
    my_logf(LL_DEBUG, LP_DATETIME, "   alert        = %s", chk->alert ? "Yes" : "No");
  }
  if (c != nb_valid_checks)
    internal_error("main", __FILE__, __LINE__);

  create_img_files();

    // Just to call WSAStartup, yes!
  os_init_network();

  almost_neverending_loop();

  if (quitting)
    return EXIT_FAILURE;
  quitting = TRUE;

  my_logs(LL_VERBOSE, LP_DATETIME, PACKAGE_NAME " end");
  my_logs(LL_NORMAL, LP_NOTHING, "");
  my_log_close();

  return EXIT_SUCCESS;
}

