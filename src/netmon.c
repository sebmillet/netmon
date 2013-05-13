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
#include <sys/time.h>
#include <sys/stat.h>
#include <pthread.h>

/*#define DEBUG*/

loglevel_t g_current_log_level = LL_NORMAL;

#define DEFAULT_CHECK_INTERVAL 180
#define DEFAULT_NB_KEEP_LAST_STATUS 20
#define DEFAULT_DISPLAY_NAME_WIDTH 20
#define DEFAULT_SENDER  (PACKAGE_TARNAME "@localhost")
#define DEFAULT_ALERT_THRESHOLD 3
#define DEFAULT_CONNECT_TIMEOUT 5

const char *DEFAULT_LOGFILE = PACKAGE_TARNAME ".log";
const char *DEFAULT_CFGFILE = PACKAGE_TARNAME ".ini";

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

#define PREFIX_RECEIVED "<<< "
#define PREFIX_SENT ">>> "

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

#include <signal.h>
#include <ctype.h>
#include <getopt.h>
#include <time.h>

#define SMALLSTRSIZE  200
#define BIGSTRSIZE    1000

#define REGULAR_STR_STRBUFSIZE 2000
#define ERR_STR_BUFSIZE 200
#define DEFAULT_BUFFER_SIZE 10000
  // Maximum size of an input line in the TCP connection
#define MAX_READLINE_SIZE 10000


//
// Status
//

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

const char *ST_TO_BGCOLOR_FORHTML[] = {
  "#FFFFFF", // ST_UNDEF
  "#B0B0B0", // ST_UNKNOWN
  "#00FF00", // ST_OK
  "#FF0000"  // ST_FAIL
};


//
// HTML & WEB SERVER
//

char g_html_title[SMALLSTRSIZE] = PACKAGE_STRING;
int g_html_title_set = FALSE;

long int g_html_refresh_interval = DEFAULT_HTML_REFRESH_PERIOD;
int g_html_refresh_interval_set = FALSE;

long int g_html_nb_columns = DEFAULT_HTML_NB_COLUMNS;
int g_html_nb_columns_set = FALSE;

long int g_webserver_on = DEFAULT_WEBSERVER_ON;
int g_webserver_on_set = FALSE;
long int g_webserver_port = DEFAULT_WEBSERVER_PORT;
int g_webserver_port_set = FALSE;

extern char const st_undef[];
extern size_t const st_undef_len;
extern char const st_unknown[];
extern size_t const st_unknown_len;
extern char const st_ok[];
extern size_t const st_ok_len;
extern char const st_fail[];
extern size_t const st_fail_len;


//
// CONFIG
//

struct check_t checks[2000];
int g_nb_checks = 0;
int g_nb_valid_checks = 0;

struct alert_t alerts[100];
int g_nb_alerts = 0;
int g_nb_valid_alerts = 0;

char g_log_file[SMALLSTRSIZE];
char g_cfg_file[SMALLSTRSIZE];

long int g_check_interval;
int g_check_interval_set = FALSE;
long int g_nb_keep_last_status = -1;
int g_nb_keep_last_status_set = FALSE;

long int g_display_name_width = DEFAULT_DISPLAY_NAME_WIDTH;
int g_display_name_width_set = FALSE;

long int g_buffer_size = DEFAULT_BUFFER_SIZE;
int g_buffer_size_set = FALSE;
long int g_connect_timeout = DEFAULT_CONNECT_TIMEOUT;
int g_connect_timeout_set = FALSE;
int telnet_log = FALSE;
int g_print_log = FALSE;
int g_print_status = FALSE;
int g_test_mode = FALSE;

char g_html_directory[BIGSTRSIZE] = DEFAULT_HTML_DIRECTORY;
int g_html_directory_set = FALSE;
char g_html_file[SMALLSTRSIZE] = DEFAULT_HTML_FILE;
int g_html_file_set = FALSE;
char g_html_complete_file_name[BIGSTRSIZE];

#define CFGK_LIST_SEPARATOR ','
#define CFGK_COMMENT_CHAR ';'
const char *CS_GENERAL_STR  = "general";
const char *CS_TCPCHECK_STR = "tcp-probe";
const char *CS_ALERT_STR = "alert";

const char *AM_SMTP_STR = "smtp";

const char *V_YESNO_STRYES = "yes";
const char *V_YESNO_STRNO = "no";

struct check_t chk00;
struct alert_t alrt00;
const struct readcfg_var_t readcfg_vars[] = {
  {"display_name", V_STR, CS_TCPPROBE, NULL, &(chk00.display_name), NULL, 0, &(chk00.display_name_set), FALSE},
  {"host_name", V_STR, CS_TCPPROBE, NULL, &(chk00.host_name), NULL, 0, &(chk00.host_name_set), FALSE},
  {"port", V_INT, CS_TCPPROBE, &(chk00.port), NULL, NULL, 0, &(chk00.port_set), FALSE},
  {"expect", V_STR, CS_TCPPROBE, NULL, &(chk00.expect), NULL, 0, &(chk00.expect_set), FALSE},
  {"alerts", V_STR, CS_TCPPROBE, NULL, &(chk00.alerts), NULL, 0, &(chk00.alerts_set), FALSE},
  {"alert_threshold", V_INT, CS_TCPPROBE, &(chk00.alert_threshold), NULL, NULL, 0, &(chk00.alert_threshold_set), FALSE},
  {"check_interval", V_INT, CS_GENERAL, &g_check_interval, NULL, NULL, 0, &g_check_interval_set, TRUE},
  {"buffer_size", V_INT, CS_GENERAL, &g_buffer_size, NULL, NULL, 0, &g_buffer_size_set, FALSE},
  {"connect_timeout", V_INT, CS_GENERAL, &g_connect_timeout, NULL, NULL, 0, &g_connect_timeout_set, FALSE},
  {"keep_last_status", V_INT, CS_GENERAL, &g_nb_keep_last_status, NULL, NULL, 0, &g_nb_keep_last_status_set, TRUE},
  {"display_name_width", V_INT, CS_GENERAL, &g_display_name_width, NULL, NULL, 0, &g_display_name_width_set, FALSE},
  {"html_refresh_interval", V_INT, CS_GENERAL, &g_html_refresh_interval, NULL, NULL, 0, &g_html_refresh_interval_set, FALSE},
  {"html_title", V_STR, CS_GENERAL, NULL, NULL, g_html_title, sizeof(g_html_title), &g_html_title_set, FALSE},
  {"html_directory", V_STR, CS_GENERAL, NULL, NULL, g_html_directory, sizeof(g_html_directory), &g_html_directory_set, FALSE},
  {"html_file", V_STR, CS_GENERAL, NULL, NULL, g_html_file, sizeof(g_html_file), &g_html_file_set, FALSE},
  {"html_nb_columns", V_INT, CS_GENERAL, &g_html_nb_columns, NULL, NULL, 0, &g_html_nb_columns_set, FALSE},
  {"webserver_on", V_YESNO, CS_GENERAL, &g_webserver_on, NULL, NULL, 0, &g_webserver_on_set, FALSE},
  {"webserver_port", V_INT, CS_GENERAL, &g_webserver_port, NULL, NULL, 0, &g_webserver_port_set, FALSE},
  {"name", V_STR, CS_ALERT, NULL, &alrt00.name, NULL, 0, &alrt00.name_set, FALSE},
  {"method", V_STR, CS_ALERT, NULL, &alrt00.method_name, NULL, 0, &alrt00.method_name_set, FALSE},
  {"smtp_smart_host", V_STR, CS_ALERT, NULL, &alrt00.smtp_smarthost, NULL, 0, &alrt00.smtp_smarthost_set, FALSE},
  {"smtp_port", V_INT, CS_ALERT, &alrt00.smtp_port, NULL, NULL, 0, &alrt00.smtp_port_set, FALSE},
  {"smtp_self", V_STR, CS_ALERT, NULL, &alrt00.smtp_self, NULL, 0, &alrt00.smtp_self_set, FALSE},
  {"smtp_sender", V_STR, CS_ALERT, NULL, &alrt00.smtp_sender, NULL, 0, &alrt00.smtp_sender_set, TRUE},
  {"smtp_recipients", V_STR, CS_ALERT, NULL, &alrt00.smtp_recipients, NULL, 0, &alrt00.smtp_recipients_set, FALSE}
};


//
// GENERAL
//

FILE *log_fd;
int g_trace_network_traffic;

int flag_interrupted = FALSE;
int quitting = FALSE;

pthread_mutex_t mutex;

#if defined(_WIN32) || defined(_WIN64)


  // * ******* *
  // * WINDOWS *
  // * ******* *

const char FS_SEPARATOR = '\\';

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

int add_reader_access_right(const char *f) {
  return 0;
}

#define strcasecmp _stricmp
#define strncasecmp _strnicmp

#else


  // * *********** *
  // * NOT WINDOWS *
  // * *********** *

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
}

int os_last_network_op_is_in_progress() {
  return (errno == EINPROGRESS);
}

void os_closesocket(int sock) {
  close(sock);
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
#else
#define dbg_write(...)
#endif

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

#define MY_GETLINE_INITIAL_ALLOCATE 30
#define MY_GETLINE_MIN_INCREASE     30
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
// Destroy a check struct
//
void check_t_destroy(struct check_t *chk) {
  if (chk->display_name != NULL)
    free(chk->display_name);
  if (chk->host_name != NULL)
    free(chk->host_name);
  if (chk->expect != NULL)
    free(chk->expect);
  if (chk->alerts != NULL)
    free(chk->alerts);
  if (chk->str_prev_status != NULL)
    free(chk->str_prev_status);
  if (chk->alerts_idx != NULL)
    free(chk->alerts_idx);
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
  dbg_write("Creating check...\n");

  chk->is_valid = FALSE;

  chk->display_name_set = FALSE;
  chk->display_name = NULL;

  chk->host_name_set = FALSE;
  chk->host_name = NULL;

  chk->expect_set = FALSE;
  chk->expect = NULL;

  chk->port_set = FALSE;

  chk->alerts = NULL;
  chk->alerts_set = FALSE;
  chk->nb_alerts = 0;
  chk->alerts_idx = NULL;
  chk->alert_threshold = 0;
  chk->alert_threshold_set = FALSE;

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
    check_t_destroy(&checks[i]);
  }
}

//
// Create an alert struct
//
void alert_t_create(struct alert_t *alrt) {
  dbg_write("Creating alert...\n");

  alrt->is_valid = FALSE;

  alrt->name_set = FALSE;
  alrt->name = NULL;

  alrt->method_name_set = FALSE;
  alrt->method_name = NULL;

  alrt->method = AM_UNDEF;

  alrt->smtp_smarthost_set = FALSE;
  alrt->smtp_smarthost = NULL;

  alrt->smtp_port_set = FALSE;

  alrt->smtp_self_set = FALSE;
  alrt->smtp_self = NULL;

  alrt->smtp_sender_set = FALSE;
  alrt->smtp_sender = NULL;

  alrt->smtp_recipients_set = FALSE;
  alrt->smtp_recipients = NULL;
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
  my_pthread_mutex_lock(&mutex);

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

  my_pthread_mutex_unlock(&mutex);
}

//
// Output a string in the program log
//
void my_logs(const loglevel_t log_level, const logdisp_t log_disp, const char *s) {
  if (log_level > g_current_log_level)
    return;

  char dt[REGULAR_STR_STRBUFSIZE];
  size_t dt_len;

  my_log_core_get_dt_str(log_disp, dt, sizeof(dt), &dt_len);
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
  size_t dt_len;
  char str[REGULAR_STR_STRBUFSIZE];
  my_log_core_get_dt_str(log_disp, dt, sizeof(dt), &dt_len);
  va_list args;
  va_start(args, format);
  vsnprintf(str, sizeof(str), format, args);
  va_end(args);
  strncat(dt, str, sizeof(dt));
  my_log_core_output(dt, dt_len);
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

    // Newline (\015\012) + null terminating character
  int l = strlen(fmt) + 100;
  char *tmp;
  tmp = (char *)malloc(l + 1);

  va_list args;
  va_start(args, fmt);
  vsnprintf(tmp, l, fmt, args);
  va_end(args);

  if (trace)
    my_logf(LL_VERBOSE, LP_DATETIME, PREFIX_SENT "%s", tmp);

  strncat(tmp, "\015\012", l);

  int e = send(*s, tmp, strlen(tmp), 0);

  free(tmp);
  if (e == SOCKET_ERROR) {
    char s_err[ERR_STR_BUFSIZE];
    my_logf(LL_ERROR, LP_DATETIME, "Error sending to socket, error %s", os_last_err_desc(s_err, sizeof(s_err)));
    os_closesocket(*s);
    *s = -1;
    return -1;
  }
  return 0;
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

      char *response = NULL;
      int response_size;
      int read_res = 0;
      if ((read_res = socket_read_line_alloc(connection_sock, &response, g_trace_network_traffic, &response_size)) < 0) {
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
const char *TERM_CLEAR_SCREEN = "\033[2J\033[1;1H";
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
        if (counter % g_html_nb_columns == g_html_nb_columns - 1 || counter == g_nb_valid_checks - 1) {
          int k;
          for (k = counter; k % g_html_nb_columns != g_html_nb_columns - 1; ++k) {
            fputs("<td></td><td></td><td></td>", H);
            if (g_nb_keep_last_status >= 1)
              fputs("<td></td>", H);
          }
          fputs("</tr>\n", H);
        }
      }
      ++counter;
    }

    if (H != NULL) {
      fputs("</table>\n", H);
      fputs("<p>* Time at which the status last changed\n</p>", H);
      fputs("</body>\n", H);
      fputs("</html>\n", H);
      fclose(H);
      add_reader_access_right(g_html_complete_file_name);
    }

    my_logf(LL_NORMAL, LP_DATETIME, "Check done in %fs", elapsed);

    if (g_check_interval && !g_test_mode) {
      long int delay = g_check_interval - (long int)elapsed;
      if (delay < 1)
        delay = 1;
      if (delay > g_check_interval)
        delay = g_check_interval;
      my_logf(LL_VERBOSE, LP_DATETIME, "Now sleeping for %li second(s) (interval = %li)", delay, g_check_interval);
      os_sleep(delay);
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

  // Works under Linux but not under Windows, I don't know why...
/*  clean_checks();*/

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
        g_current_log_level++;
        break;

      case 'q':
        g_current_log_level--;
        break;

      case '?':
        exit(EXIT_FAILURE);

      default:
        abort();
    }
  }
  if (optind < argc)
    option_error("Trailing options");
  if (g_current_log_level < LL_ERROR)
    g_current_log_level = LL_ERROR;
  if (g_current_log_level > LL_DEBUG)
    g_current_log_level = LL_DEBUG;

  g_trace_network_traffic = (g_current_log_level == LL_DEBUG);
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
  if (!chk->is_valid)
    return;

  ++g_nb_valid_checks;

  if (!chk->display_name_set) {
    size_t n = strlen(chk->host_name) + 1;
    chk->display_name = (char *)malloc(n);
    strncpy(chk->display_name, chk->host_name, n);
    chk->display_name_set = TRUE;
  }

  if (!chk->alert_threshold_set) {
    chk->alert_threshold = DEFAULT_ALERT_THRESHOLD;
    chk->alert_threshold_set = TRUE;
  }
}

//
// Check an alert variable
//
void alert_t_check(struct alert_t *alrt, const char *cf, int line_number) {
  g_nb_alerts++;

  int is_valid = TRUE;

  if (!alrt->name_set) {
    my_logf(LL_ERROR, LP_DATETIME, "Configuration file '%s', section of line %i: no name defined, discarding alert", cf, line_number);
    is_valid = FALSE;
  }
  if (!alrt->method_name_set) {
    my_logf(LL_ERROR, LP_DATETIME, "Configuration file '%s', section of line %i: no method defined, discarding alert", cf, line_number);
    is_valid = FALSE;
  } else if (strcasecmp(alrt->method_name, AM_SMTP_STR) == 0)
    alrt->method = AM_SMTP;
  else {
    my_logf(LL_ERROR, LP_DATETIME, "Configuration file '%s', section of line %i: unknown method '%s', discarding alert", cf, line_number, alrt->method_name);
    is_valid = FALSE;
  }

  if (alrt->method == AM_SMTP) {
    if (!alrt->smtp_smarthost_set) {
      my_logf(LL_ERROR, LP_DATETIME, "Configuration file '%s', section of line %i: no smart host defined, discarding alert", cf, line_number);
      is_valid = FALSE;
    }
    if (!alrt->smtp_recipients_set) {
      my_logf(LL_ERROR, LP_DATETIME, "Configuration file '%s', section of line %i: no recipients defined, discarding alert", cf, line_number);
      is_valid = FALSE;
    }
  }

  alrt->is_valid = is_valid;

  if (alrt->is_valid) {
    if (!alrt->smtp_sender_set) {
      size_t n = strlen(DEFAULT_SENDER) + 1;
      alrt->smtp_sender = (char *)malloc(n);
      strncpy(alrt->smtp_sender, DEFAULT_SENDER, n);
      alrt->smtp_sender_set = TRUE;
    }
    ++g_nb_valid_alerts;
  }
}

void read_configuration_file(const char *cf) {
  FILE *FCFG = NULL;
  if ((FCFG = fopen(cf, "r")) == NULL) {
    fatal_error("Configuration file '%s': unable to open", cf);
  }
  my_logf(LL_VERBOSE, LP_DATETIME, "Reading configuration from '%s'", cf);

  int save_g_print_log = g_print_log;
  g_print_log = TRUE;

  ssize_t nb_bytes;
  char *line = NULL;
  size_t len = 0;
  int line_number = 0;
  int section_start_line_number = -1;
  int read_status = CS_NONE;

  int cur_check = -1;
  int cur_alert = -1;

  while ((nb_bytes = my_getline(&line, &len, FCFG)) != -1) {
    line[nb_bytes - 1] = '\0';
    ++line_number;

/*    dbg_write("Line %i: '%s'\n", line_number, line);*/

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

          if (read_status == CS_TCPPROBE) {
            if (section_start_line_number < 1 || cur_check < 0)
              internal_error("read_configuration_file", __FILE__, __LINE__);
            checks[cur_check] = chk00;
            check_t_check(&checks[cur_check], cf, section_start_line_number);
          } else if (read_status == CS_ALERT) {
            if (section_start_line_number < 1 || cur_alert < 0)
              internal_error("read_configuration_file", __FILE__, __LINE__);
            alerts[cur_alert] = alrt00;
            alert_t_check(&alerts[cur_alert], cf, section_start_line_number);
          }

          if (strcasecmp(section_name, CS_GENERAL_STR) == 0) {
            read_status = CS_GENERAL;
          } else {
            section_start_line_number = line_number;
            read_status = CS_NONE;

            if (strcasecmp(section_name, CS_TCPCHECK_STR) == 0) {
              ++cur_check;
/*              dbg_write("New check: %i\n", cur_check);*/
              if (cur_check >= sizeof(checks) / sizeof(*checks)) {
                --cur_check;
                my_logf(LL_ERROR, LP_DATETIME,
                  "Configuration file '%s', line %i: reached max number of checks (%i)",
                  cf, line_number, sizeof(checks) / sizeof(*checks));
              } else {
                read_status = CS_TCPPROBE;
                check_t_create(&chk00);
              }
            } else if (strcasecmp(section_name, CS_ALERT_STR) == 0) {
              ++cur_alert;
/*              dbg_write("New alert: %i\n", cur_alert);*/
              if (cur_alert >= sizeof(alerts) / sizeof(*alerts)) {
                --cur_alert;
                my_logf(LL_ERROR, LP_DATETIME,
                  "Configuration file '%s', line %i: reached max number of alerts (%i)",
                  cf, line_number, sizeof(alerts) / sizeof(*alerts));
              } else {
                read_status = CS_ALERT;
                alert_t_create(&alrt00);
              }
            } else {
              my_logf(LL_ERROR, LP_DATETIME,
                "Configuration file '%s', line %i: unknown section name '%s'",
                cf, line_number, section_name);
            }
          }
          section_start_line_number = line_number;
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
          if (slen >= SMALLSTRSIZE)
            slen = SMALLSTRSIZE;

          char orig_key[SMALLSTRSIZE];
          char *key = orig_key;
          size_t n = slen + 1;
          if (n > sizeof(orig_key))
            n = sizeof(orig_key);
          strncpy(key, b, n);
          key[n - 1] = '\0';
          key = trim(key);

          n = blen - slen + 1;
          char orig_value[BIGSTRSIZE];
          char *value = orig_value;
          if (n > sizeof(orig_value))
            n = sizeof(orig_value);
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

                if (read_status != cfg.section) {
                  my_logf(LL_ERROR, LP_DATETIME,
                    "Configuration file '%s', line %i: variable %s not allowed in this section",
                    cf, line_number, key);
                } else {
                  long int n = 0;
                  if (cfg.plint_target != NULL && cfg.var_type == V_INT)
                    n = atoi(value);
                  if (cfg.plint_target != NULL && strlen(value) == 0) {
                    my_logf(LL_ERROR, LP_DATETIME, "Configuration file '%s', line %i: empty value not allowed",
                      cf, line_number);
                  } else if (cfg.plint_target != NULL && cfg.var_type == V_INT && n == 0 && !cfg.allow_null) {
                    my_logf(LL_ERROR, LP_DATETIME, "Configuration file '%s', line %i: null value not allowed",
                      cf, line_number);
                  } else if ((cfg.p_pchar_target != NULL || cfg.pchar_target != NULL) &&
                                strlen(value) == 0 && !cfg.allow_null) {
                    my_logf(LL_ERROR, LP_DATETIME, "Configuration file '%s', line %i: empty value not allowed",
                      cf, line_number);
                  } else if (*cfg.pint_var_set) {
                    my_logf(LL_ERROR, LP_DATETIME, "Configuration file '%s', line %i: variable %s already defined",
                      cf, line_number, key);
                  } else if (cfg.plint_target != NULL) {

                    if (cfg.var_type == V_INT) {
                        // Variable of type long int
                      *cfg.plint_target = n;
                      *cfg.pint_var_set = TRUE;
                    } else {

                        // Variable of type long int with yes/no input
                      if (strcasecmp(value, V_YESNO_STRYES) == 0) {
                        *cfg.plint_target = TRUE;
                      } else if (strcasecmp(value, V_YESNO_STRNO) == 0) {
                        *cfg.plint_target = FALSE;
                      } else {
                        my_logf(LL_ERROR, LP_DATETIME,
                          "Configuration file '%s', line %i: variable %s must be set to yes or no",
                          cf, line_number, key);
                      }

                    }

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
              my_logf(LL_ERROR, LP_DATETIME, "Configuration file '%s', line %i: unknown variable %s",
                cf, line_number, key);
            }
          }
        }
      break;
    }
  }
  if (cur_check >= 0 && read_status == CS_TCPPROBE) {
    if (section_start_line_number < 0)
      internal_error("read_configuration_file", __FILE__, __LINE__);
    checks[cur_check] = chk00;
    check_t_check(&checks[cur_check], cf, section_start_line_number);
  }

  if (cur_alert >= 0 && read_status == CS_ALERT) {
    if (section_start_line_number < 0)
      internal_error("read_configuration_file", __FILE__, __LINE__);
    alerts[cur_alert] = alrt00;
    alert_t_check(&alerts[cur_alert], cf, section_start_line_number);
  }

  if (g_nb_checks != cur_check + 1)
    internal_error("read_configuration_file", __FILE__, __LINE__);

  if (g_nb_alerts != cur_alert + 1)
    internal_error("read_configuration_file", __FILE__, __LINE__);

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

  g_print_log = save_g_print_log;
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
//
int server_listen(int listen_port, const char *prefix) {
  struct sockaddr_in listen_sa;

  char s_err[ERR_STR_BUFSIZE];

  int sock = socket(PF_INET, SOCK_STREAM, IPPROTO_TCP);
  if (sock == -1) {
    my_logf(LL_ERROR, LP_DATETIME, "%s: error creating socket, error %s", prefix, os_last_err_desc(s_err, sizeof(s_err)));
    return 0;
  }

  int bOptVal = TRUE;
  socklen_t bOptLen = sizeof(int);
  if (setsockopt(sock, SOL_SOCKET, SO_REUSEADDR, (char *)&bOptVal, bOptLen) == SOCKET_ERROR) {
    my_logf(LL_ERROR, LP_DATETIME, "%s: cannot set socket option, error %s", prefix, os_last_err_desc(s_err, sizeof(s_err)));
    os_closesocket(sock);
    return 0;
  }

  listen_sa.sin_addr.s_addr = htonl(INADDR_ANY);
  listen_sa.sin_port = htons((u_short)listen_port);
  listen_sa.sin_family = AF_INET;

  if (bind(sock, (struct sockaddr*)&listen_sa, sizeof(listen_sa)) == SOCKET_ERROR) {
    my_logf(LL_ERROR, LP_DATETIME, "%s: cannot bind, error %s", prefix, os_last_err_desc(s_err, sizeof(s_err)));
    os_closesocket(sock);
    return 0;
  }
  if (listen(sock, SOMAXCONN) == SOCKET_ERROR) {
    my_logf(LL_ERROR, LP_DATETIME, "%s: cannot listen, error %s", prefix, os_last_err_desc(s_err, sizeof(s_err)));
    os_closesocket(sock);
    return 0;
  }
  return sock;
}

//
// Have server accept incoming connections
//
int server_accept(int listen_sock, struct sockaddr_in* remote_sin, int listen_port, const char* prefix) {
  socklen_t remote_sin_len;

  char s_err[SMALLSTRSIZE];

  my_logf(LL_VERBOSE, LP_DATETIME, "%s: listening on port %u...", prefix, listen_port);
  remote_sin_len = sizeof(*remote_sin);
  int sock = accept(listen_sock, (struct sockaddr *)remote_sin, &remote_sin_len);
  if (sock == -1) {
    my_logf(LL_ERROR, LP_DATETIME, "%s: cannot accept, error %s", prefix, os_last_err_desc(s_err, sizeof(s_err)));
    os_closesocket(listen_sock);
    return 0;
  }
  my_logf(LL_VERBOSE, LP_DATETIME, "%s: connection accepted from %s", prefix, inet_ntoa(remote_sin->sin_addr));
  return sock;
}

//
// Send error page over HTTP connection
//
void http_send_error_page(int sock, const char *e, const char *t) {
  my_logf(LL_DEBUG, LP_DATETIME, "Sending HTTP error %s / %s", e, t);

  socket_line_sendf(&sock, g_trace_network_traffic, "HTTP/1.1 %s", e);
  socket_line_sendf(&sock, g_trace_network_traffic, "Connection: close");
  socket_line_sendf(&sock, g_trace_network_traffic, "Content-type: text/html");
  socket_line_sendf(&sock, g_trace_network_traffic, "");
  socket_line_sendf(&sock, g_trace_network_traffic, "<html><head><title>Not Found</title></head>");
  socket_line_sendf(&sock, g_trace_network_traffic, "<body><p><b>%s</b></p></body></html>", t);

  my_logf(LL_DEBUG, LP_DATETIME, "Finished sending HTTP error %s / %s", e, t);
}

//
// Answers web connections
//
int manage_web_transaction(int sock, const struct sockaddr_in *remote_sin) {

  my_logf(LL_DEBUG, LP_DATETIME, "Entering web transaction...");

  char *received = NULL;
  int size;
  int read_res = 0;
  if ((read_res = socket_read_line_alloc(sock, &received, g_trace_network_traffic, &size)) < 0) {
    free(received);
    return -1;
  }

  my_logf(LL_DEBUG, LP_DATETIME, "Received request '%s'", received);

  char *p = received;
  if (strncmp(p, "GET", 3)) {
    free(received);
    http_send_error_page(sock, "400 Bad Request", "Could not understand request");
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
    http_send_error_page(sock, "400 Bad Request", "Could not understand request");
    return -1;
  }

  char *t = strrchr(tmpurl, '/');
  if (t != NULL)
    tmpurl = t + 1;
  if (strstr(received, "..") != 0) {
    free(received);
    http_send_error_page(sock, "401 Unauthorized", "Not allowed to go up in directory tree");
    return -1;
  }

  char url[BIGSTRSIZE];
  strncpy(url, tmpurl, sizeof(url));

  int keep_alive = TRUE;
  while (strlen(received) != 0) {
    if ((read_res = socket_read_line_alloc(sock, &received, g_trace_network_traffic, &size)) < 0) {
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
    http_send_error_page(sock, "404 Not found", s_err);
    return -1;
  }

  char dt_fileupdate[50];
  char dt_now[50];
  struct timeval tv;
  if (my_ctime_r(&s.st_mtime, dt_fileupdate, sizeof(dt_fileupdate)) == 0 ||
      gettimeofday(&tv, NULL) != 0 ||
      my_ctime_r(&tv.tv_sec, dt_now, sizeof(dt_now)) == 0) {
    http_send_error_page(sock, "500 Server error", "Internal server error");
    return -1;
  }

  FILE *F = fopen(path, "rb");
  if (F == NULL) {
    http_send_error_page(sock, "404 Not found", "File not found");
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

  socket_line_sendf(&sock, g_trace_network_traffic, "HTTP/1.1 200 OK");
  socket_line_sendf(&sock, g_trace_network_traffic, "Connection: %s", keep_alive ? "keep-alive" : "close");
  socket_line_sendf(&sock, g_trace_network_traffic, "Content-length: %li", (long int)s.st_size);
  socket_line_sendf(&sock, g_trace_network_traffic, "Content-type: %s", content_type);
  socket_line_sendf(&sock, g_trace_network_traffic, "Date: %s", dt_now);
  socket_line_sendf(&sock, g_trace_network_traffic, "Last-modified: %s", dt_fileupdate);
  socket_line_sendf(&sock, g_trace_network_traffic, "");

  char *buffer = (char *)malloc(g_buffer_size);
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

    e = send(sock, buffer, n, 0);
    if (e == SOCKET_ERROR) {
      my_logf(LL_ERROR, LP_DATETIME, "Socket error");
        keep_alive = FALSE;
      break;
    }
  }

  socket_line_sendf(&sock, g_trace_network_traffic, "");

  free(buffer);
  fclose(F);

  return keep_alive ? 0 : -1;
}

//
// Manages web server
//
void *webserver(void *p) {
  int listen_sock = server_listen(g_webserver_port, WEBSERVER_LOG_PREFIX);
  if (listen_sock == 0) {
    return NULL;
  }

  my_logf(LL_NORMAL, LP_DATETIME, WEBSERVER_LOG_PREFIX ": start");

  struct sockaddr_in remote_sin;
  int sock;
  while (TRUE) {
    sock = server_accept(listen_sock, &remote_sin, g_webserver_port, WEBSERVER_LOG_PREFIX);
    while (sock != -1) {
      if (manage_web_transaction(sock, &remote_sin) != 0) {
        os_closesocket(sock);
        sock = -1;
        my_logf(LL_VERBOSE, LP_DATETIME, WEBSERVER_LOG_PREFIX ": terminated connection with client");
      } else {
        my_logf(LL_VERBOSE, LP_DATETIME, WEBSERVER_LOG_PREFIX ": continuing connection with client");
      }
    }
  }
  return NULL;
}

//
// Identify alerts configured in checks
//
void identify_alerts() {
  int i; int j; int k;

  int save_g_print_log = g_print_log;
  g_print_log = TRUE;

  for (i = 0; i < g_nb_checks; ++i) {
    struct check_t *chk = &checks[i];
    if (!chk->is_valid)
      continue;
    if (!chk->alerts_set)
      continue;
    char *p = chk->alerts;
    int n = 1;
    for (j = 0; p[j] != '\0'; ++j) {
      if (p[j] == CFGK_LIST_SEPARATOR)
        ++n;
    }
    chk->nb_alerts = n;
    chk->alerts_idx = (int *)malloc(sizeof(int) * n);
    size_t b = strlen(chk->alerts) + 2;

    char *t = (char *)malloc(b);
    strncpy(t, chk->alerts, b);

    dbg_write("Will analyze '%s' (nb = %i)\n", t, chk->nb_alerts);

    char *next_curs = t;
    int index = 0;
    while (next_curs != NULL) {
      char *curs = next_curs;
      char *fc = curs;
      for (; (*fc) != CFGK_LIST_SEPARATOR && (*fc) != '\0'; ++fc)
        ;
      if ((*fc) == CFGK_LIST_SEPARATOR) {
        (*fc) = '\0';
        next_curs = fc + 1;
      } else {
        next_curs = NULL;
      }
      curs = trim(curs);
      for (k = 0; k < g_nb_alerts; ++k) {
        if (alerts[k].is_valid) {
          if (strcasecmp(alerts[k].name, curs) == 0) {
            break;
          }
        }
      }
      chk->alerts_idx[index] = (k >= g_nb_alerts ? -1 : k);
      if (chk->alerts_idx[index] < 0)
        my_logf(LL_ERROR, LP_DATETIME, "Check '%s': unknown alert '%s'", chk->display_name, curs);
      ++index;
    }
    free(t);
  }

  g_print_log = save_g_print_log;

}

int main(int argc, char *argv[]) {

  parse_options(argc, argv);

  atexit(atexit_handler);
  signal(SIGTERM, sigterm_handler);
  signal(SIGABRT, sigabrt_handler);
  signal(SIGINT, sigint_handler);

  if ((errno = pthread_mutex_init(&mutex, NULL)) != 0) {
    char s_err[SMALLSTRSIZE];
    fatal_error("pthread_mutex_init(): %s", errno_error(s_err, sizeof(s_err)));
  }

  my_log_open();
  my_logs(LL_NORMAL, LP_DATETIME, PACKAGE_STRING " start");

  read_configuration_file(g_cfg_file);
  identify_alerts();

  int ii;
  for (ii = 0; ii < g_nb_checks; ++ii) {
    struct check_t *chk = &checks[ii];
    check_t_getready(chk);
  }


//
// CHECKS
//

  int i;
  int c = 0;
  for (i = 0; i < g_nb_checks; ++i) {
    struct check_t *chk = &checks[i];
    if (chk->is_valid) {
      ++c;
      my_logf(LL_DEBUG, LP_INDENT, "== CHECK #%i", i);
    } else {
      my_logf(LL_DEBUG, LP_INDENT, "!! check #%i (will be ignored)", i);
    }
    my_logf(LL_DEBUG, LP_INDENT, "   is_valid     = %s", chk->is_valid ? "Yes" : "No");
    my_logf(LL_DEBUG, LP_INDENT, "   display_name = %s", chk->display_name_set ? chk->display_name : "<unset>");
    my_logf(LL_DEBUG, LP_INDENT, "   host_name    = %s", chk->host_name_set ? chk->host_name : "<unset>");

    if (chk->port_set)
      my_logf(LL_DEBUG, LP_INDENT, "   port         = %i", chk->port);
    else
      my_logf(LL_DEBUG, LP_INDENT, "   port         = <unset>");

    my_logf(LL_DEBUG, LP_INDENT, "   expect       = %s", chk->expect_set ? chk->expect : "<unset>");
    my_logf(LL_DEBUG, LP_INDENT, "   alerts       = %s", chk->alerts_set ? chk->alerts : "<unset>");
    my_logf(LL_DEBUG, LP_INDENT, "   nb alerts    = %i", chk->nb_alerts);
    int j;
    for (j = 0; j < chk->nb_alerts; ++j) {
      my_logf(LL_DEBUG, LP_INDENT, "     alert:     = #%i -> %s",
        j, chk->alerts_idx[j] >= 0 ? alerts[chk->alerts_idx[j]].name : "<unknown>");
    }
  }
  if (c != g_nb_valid_checks)
    internal_error("main", __FILE__, __LINE__);


//
// ALERTS
//

  c = 0;
  for (i = 0; i < g_nb_alerts; ++i) {
    struct alert_t *alrt = &alerts[i];
    if (alrt->is_valid) {
      ++c;
      my_logf(LL_DEBUG, LP_INDENT, "== ALERT #%i", i);
    } else {
      my_logf(LL_DEBUG, LP_INDENT, "!! alert #%i (will be ignored)", i);
    }
    my_logf(LL_DEBUG, LP_INDENT, "   is_valid          = %s", alrt->is_valid ? "Yes" : "No");
    my_logf(LL_DEBUG, LP_INDENT, "   name              = %s", alrt->name_set ? alrt->name : "<unset>");
    my_logf(LL_DEBUG, LP_INDENT, "   method            = %s", alrt->method_name_set ? alrt->method_name : "<unset>");
    if (alrt->method == AM_SMTP) {
      my_logf(LL_DEBUG, LP_INDENT, "   SMTP/smart host = %s", alrt->smtp_smarthost_set ? alrt->smtp_smarthost : "<unset>");
      if (alrt->smtp_port_set)
        my_logf(LL_DEBUG, LP_INDENT, "   SMTP/port       = %i", alrt->smtp_port);
      else
        my_logf(LL_DEBUG, LP_INDENT, "   SMTP/port       = <unset>");
      my_logf(LL_DEBUG, LP_INDENT, "   SMTP/self       = %s", alrt->smtp_self_set ? alrt->smtp_self : "<unset>");
      my_logf(LL_DEBUG, LP_INDENT, "   SMTP/sender     = %s", alrt->smtp_sender_set ? alrt->smtp_sender : "<unset>");
      my_logf(LL_DEBUG, LP_INDENT, "   SMTP/recipients = %s", alrt->smtp_recipients_set ? alrt->smtp_recipients : "<unset>");
    }
  }
  if (c != g_nb_valid_alerts)
    internal_error("main", __FILE__, __LINE__);


//
// CONFIG
//

  my_logf(LL_VERBOSE, LP_DATETIME, "check_interval = %li", g_check_interval);
  my_logf(LL_VERBOSE, LP_DATETIME, "keep_last_status = %li", g_nb_keep_last_status);
  my_logf(LL_VERBOSE, LP_DATETIME, "display_name_width = %li", g_display_name_width);
  my_logf(LL_VERBOSE, LP_DATETIME, "html_directory = %s", g_html_directory);
  my_logf(LL_VERBOSE, LP_DATETIME, "html_file = %s", g_html_file);
  my_logf(LL_VERBOSE, LP_DATETIME, "html_title = %s", g_html_title);
  my_logf(LL_VERBOSE, LP_DATETIME, "html_refresh_interval = %li", g_html_refresh_interval);
  my_logf(LL_NORMAL, LP_DATETIME, "Valid check(s) defined: %i", g_nb_valid_checks);

  my_logf(LL_VERBOSE, LP_DATETIME, "Run web server: %s", g_webserver_on ? "yes" : "no");
  if (g_webserver_on)
    my_logf(LL_VERBOSE, LP_DATETIME, "Web server listen port: %lu", g_webserver_port);

  for (i = 0; i < g_nb_checks; ++i) {
    struct check_t *chk = &checks[i];
    if (!chk->is_valid)
      continue;

    char t[BIGSTRSIZE];
    strncpy(t, "", sizeof(t));
    int II;
    for (II = 0; II < chk->nb_alerts; ++II) {
      if (chk->alerts_idx[II] >= 0) {
        if (strlen(t) >= 1)
          strncat(t, ", ", sizeof(t));
        strncat(t, alerts[chk->alerts_idx[II]].name, sizeof(t));
      }
    }
    t[sizeof(t) - 1] = '\0';

    my_logf(LL_NORMAL, LP_DATETIME, "To check: '%s' [%s:%i], %s%s%s, %s%s", chk->display_name, chk->host_name, chk->port,
      chk->expect_set ? "expect \"" : "no expect", chk->expect_set ? chk->expect : "", chk->expect_set ? "\"" : "",
      chk->alerts_set ? "alerts: " : "no alert", chk->alerts_set ? t : "");
  }

  create_img_files();

    // Just to call WSAStartup, yes!
  os_init_network();

  if (g_webserver_on) {
    pthread_t id;
    if (pthread_create(&id, NULL, webserver, NULL) != 0)
      fatal_error("main(): pthread_create() error");
  }

  almost_neverending_loop();

  if (quitting) {
    return EXIT_FAILURE;
  }
  quitting = TRUE;

  my_logs(LL_VERBOSE, LP_DATETIME, PACKAGE_NAME " end");
  my_logs(LL_NORMAL, LP_NOTHING, "");
  my_log_close();

  return EXIT_SUCCESS;
}

