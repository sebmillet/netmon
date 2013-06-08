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
#include <sys/wait.h>
#include <pthread.h>

/*#define DEBUG*/

loglevel_t g_current_log_level = LL_NORMAL;

#define DEFAULT_DATE_FORMAT DF_FRENCH
#define DEFAULT_LOG_USEC TRUE
#define DEFAULT_CHECK_INTERVAL 120
#define DEFAULT_NB_KEEP_LAST_STATUS 15
#define DEFAULT_DISPLAY_NAME_WIDTH 20
#define DEFAULT_ALERT_THRESHOLD 3
#define DEFAULT_ALERT_SMTP_SENDER  (PACKAGE_TARNAME "@localhost")
#define DEFAULT_ALERT_REPEAT_EVERY 30
#define DEFAULT_ALERT_REPEAT_MAX 5
#define DEFAULT_ALERT_RECOVERY TRUE
#define DEFAULT_ALERT_RETRIES 2
#define DEFAULT_ALERT_SMTP_PORT 25
#define DEFAULT_ALERT_SMTP_SELF PACKAGE_TARNAME
#define DEFAULT_ALERT_LOG_STRING "${NOW_TIMESTAMP}  ${DESCRIPTION}"
#define DEFAULT_CONNECT_TIMEOUT 5
#define DEFAULT_PRINT_SUBST_ERROR FALSE
#define SUBST_ERROR_PREFIX  "?"
#define SUBST_ERROR_POSTFIX "?"
#define LAST_STATUS_CHANGE_DISPLAY_SECONDS  (60 * 60 * 23)

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

#define LOG_AFTER_TIMESTAMP "  "
#define PREFIX_RECEIVED "<<< "
#define PREFIX_SENT ">>> "

  // Don't update the one below unless you know what you're doing!
  // Linked to test_linux directory scripts
#define TEST2_NB_LOOPS  199

  // As writtn here:
  //   http://nagiosplug.sourceforge.net/developer-guidelines.html#AEN76
enum {_NAGIOS_FIRST = 0, NAGIOS_OK = 0, NAGIOS_WARNING = 1, NAGIOS_CRITICAL = 2, NAGIOS_UNKNOWN = 3, _NAGIOS_LAST = 3};

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

enum {AS_NOTHING, AS_FAIL, AS_RECOVERY};
const char *alert_status_names[] = {
  "Nothing",  // AS_NOTHING
  "Fail",     // AS_FAIL
  "Recovery"  // AS_RECOVERY
};

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
const char *ST_TO_LONGSTR_FANCY[] = {
  "<undef>",  // ST_UNDEF
  "** ?? **", // ST_UNKNOWN
  "ok",       // ST_OK
  "** KO **"  // ST_FAIL
};
const char *ST_TO_LONGSTR_SIMPLE[] = {
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

char g_test_alert[SMALLSTRSIZE];

long int g_check_interval;
int g_check_interval_set = FALSE;
long int g_nb_keep_last_status = -1;
int g_nb_keep_last_status_set = FALSE;

long int g_print_subst_error = DEFAULT_PRINT_SUBST_ERROR;
int g_print_subst_error_set = FALSE;

long int g_display_name_width = DEFAULT_DISPLAY_NAME_WIDTH;
int g_display_name_width_set = FALSE;

long int g_buffer_size = DEFAULT_BUFFER_SIZE;
int g_buffer_size_set = FALSE;
long int g_connect_timeout = DEFAULT_CONNECT_TIMEOUT;
int g_connect_timeout_set = FALSE;
int telnet_log = FALSE;
int g_print_log = FALSE;
int g_print_status = FALSE;
int g_test_mode = 0;

char g_html_directory[BIGSTRSIZE] = DEFAULT_HTML_DIRECTORY;
int g_html_directory_set = FALSE;
char g_html_file[SMALLSTRSIZE] = DEFAULT_HTML_FILE;
int g_html_file_set = FALSE;
char g_html_complete_file_name[BIGSTRSIZE];

#define CFGK_LIST_SEPARATOR ','
#define CFGK_COMMENT_CHAR ';'
const char *CS_GENERAL_STR  = "general";
const char *CS_CHECK_STR = "check";
const char *CS_ALERT_STR = "alert";

#define FIND_STRING_NOT_FOUND -1

enum {AM_UNDEF = FIND_STRING_NOT_FOUND, AM_SMTP = 0, AM_PROGRAM = 1, AM_LOG = 2};
const char *l_alert_methods[] = {
  "smtp",     // AM_SMTP
  "program",  // AM_PROGRAM
  "log"       // AM_LOG
};
int (*alert_func[]) (const struct exec_alert_t *) = {
  execute_alert_smtp,     // AM_SMTP
  execute_alert_program,  // AM_PROGRAM
  execute_alert_log       // AM_LOG
};

enum {CM_UNDEF = FIND_STRING_NOT_FOUND, CM_TCP = 0, CM_PROGRAM = 1};
const char *l_check_methods[] = {
  "tcp",      // CM_TCP
  "program"   // CM_PROGRAM
};
int (*check_func[]) (const struct check_t *, const struct subst_t *, int) = {
  perform_check_tcp,    // CM_TCP
  perform_check_program // CM_PROGRAM
};

enum {ID_YES = 0, ID_NO = 1};
const char *l_yesno[] = {
  "yes",  // ID_YES
  "no"    // ID_NO
};

long int g_log_usec = DEFAULT_LOG_USEC;
int g_log_usec_set = FALSE;
long int g_date_format;
int g_date_format_set = FALSE;
enum {DF_FRENCH = 0, DF_ENGLISH = 1};
int g_date_df = (DEFAULT_DATE_FORMAT == DF_FRENCH);
const char *l_date_formats[] = {
  "french", // DF_FENCH
  "english" // DF_ENGLISH
};

struct check_t chk00;
struct alert_t alrt00;
const struct readcfg_var_t readcfg_vars[] = {
  {"method", V_STRKEY, CS_PROBE, &chk00.method, NULL, NULL, 0, &chk00.method_set, FALSE, l_check_methods,
    sizeof(l_check_methods) / sizeof(*l_check_methods)},
  {"display_name", V_STR, CS_PROBE, NULL, &(chk00.display_name), NULL, 0, &(chk00.display_name_set), FALSE, NULL, 0},
  {"host_name", V_STR, CS_PROBE, NULL, &(chk00.host_name), NULL, 0, &(chk00.host_name_set), FALSE, NULL, 0},
  {"tcp_port", V_INT, CS_PROBE, &(chk00.tcp_port), NULL, NULL, 0, &(chk00.tcp_port_set), FALSE, NULL, 0},
  {"tcp_connect_timeout", V_INT, CS_PROBE, &(chk00.tcp_connect_timeout), NULL, NULL, 0, &(chk00.tcp_connect_timeout_set), FALSE, NULL, 0},
  {"tcp_expect", V_STR, CS_PROBE, NULL, &(chk00.tcp_expect), NULL, 0, &(chk00.tcp_expect_set), FALSE, NULL, 0},
  {"program_command", V_STR, CS_PROBE, NULL, &(chk00.prg_command), NULL, 0, &(chk00.prg_command_set), FALSE, NULL, 0},
  {"alerts", V_STR, CS_PROBE, NULL, &(chk00.alerts), NULL, 0, &(chk00.alerts_set), FALSE, NULL, 0},
  {"alert_threshold", V_INT, CS_PROBE, &(chk00.alert_threshold), NULL, NULL, 0, &(chk00.alert_threshold_set), FALSE, NULL, 0},
  {"alert_repeat_every", V_INT, CS_PROBE, &(chk00.alert_repeat_every), NULL, NULL, 0, &(chk00.alert_repeat_every_set), FALSE, NULL, 0},
  {"alert_repeat_max", V_INT, CS_PROBE, &(chk00.alert_repeat_max), NULL, NULL, 0, &(chk00.alert_repeat_max_set), TRUE, NULL, 0},
  {"alert_recovery", V_YESNO, CS_PROBE, &(chk00.alert_recovery), NULL, NULL, 0, &(chk00.alert_recovery_set), FALSE, NULL, 0},
  {"date_format", V_STRKEY, CS_GENERAL, &g_date_format, NULL, NULL, 0, &g_date_format_set, FALSE, l_date_formats,
    sizeof(l_date_formats) / sizeof(*l_date_formats)},
  {"print_subst_error", V_YESNO, CS_GENERAL, &g_print_subst_error, NULL, NULL, 0, &g_print_subst_error_set, FALSE, NULL, 0},
  {"log_usec", V_YESNO, CS_GENERAL, &g_log_usec, NULL, NULL, 0, &g_log_usec_set, FALSE, NULL, 0},
  {"check_interval", V_INT, CS_GENERAL, &g_check_interval, NULL, NULL, 0, &g_check_interval_set, TRUE, NULL, 0},
  {"buffer_size", V_INT, CS_GENERAL, &g_buffer_size, NULL, NULL, 0, &g_buffer_size_set, FALSE, NULL, 0},
  {"connect_timeout", V_INT, CS_GENERAL, &g_connect_timeout, NULL, NULL, 0, &g_connect_timeout_set, FALSE, NULL, 0},
  {"keep_last_status", V_INT, CS_GENERAL, &g_nb_keep_last_status, NULL, NULL, 0, &g_nb_keep_last_status_set, TRUE, NULL, 0},
  {"display_name_width", V_INT, CS_GENERAL, &g_display_name_width, NULL, NULL, 0, &g_display_name_width_set, FALSE, NULL, 0},
  {"html_refresh_interval", V_INT, CS_GENERAL, &g_html_refresh_interval, NULL, NULL, 0, &g_html_refresh_interval_set, FALSE, NULL, 0},
  {"html_title", V_STR, CS_GENERAL, NULL, NULL, g_html_title, sizeof(g_html_title), &g_html_title_set, FALSE, NULL, 0},
  {"html_directory", V_STR, CS_GENERAL, NULL, NULL, g_html_directory, sizeof(g_html_directory), &g_html_directory_set, FALSE, NULL, 0},
  {"html_file", V_STR, CS_GENERAL, NULL, NULL, g_html_file, sizeof(g_html_file), &g_html_file_set, FALSE, NULL, 0},
  {"html_nb_columns", V_INT, CS_GENERAL, &g_html_nb_columns, NULL, NULL, 0, &g_html_nb_columns_set, FALSE, NULL, 0},
  {"webserver_on", V_YESNO, CS_GENERAL, &g_webserver_on, NULL, NULL, 0, &g_webserver_on_set, FALSE, NULL, 0},
  {"webserver_port", V_INT, CS_GENERAL, &g_webserver_port, NULL, NULL, 0, &g_webserver_port_set, FALSE, NULL, 0},
  {"name", V_STR, CS_ALERT, NULL, &alrt00.name, NULL, 0, &alrt00.name_set, FALSE, NULL, 0},
  {"method", V_STRKEY, CS_ALERT, &alrt00.method, NULL, NULL, 0, &alrt00.method_set, FALSE, l_alert_methods,
    sizeof(l_alert_methods) / sizeof(*l_alert_methods)},
  {"threshold", V_INT, CS_ALERT, &(alrt00.threshold), NULL, NULL, 0, &(alrt00.threshold_set), FALSE, NULL, 0},
  {"repeat_every", V_INT, CS_ALERT, &(alrt00.repeat_every), NULL, NULL, 0, &(alrt00.repeat_every_set), FALSE, NULL, 0},
  {"repeat_max", V_INT, CS_ALERT, &(alrt00.repeat_max), NULL, NULL, 0, &(alrt00.repeat_max_set), TRUE, NULL, 0},
  {"recovery", V_YESNO, CS_ALERT, &(alrt00.recovery), NULL, NULL, 0, &(alrt00.recovery_set), FALSE, NULL, 0},
  {"retries", V_INT, CS_ALERT, &(alrt00.retries), NULL, NULL, 0, &(alrt00.retries_set), TRUE, NULL, 0},
  {"smtp_smart_host", V_STR, CS_ALERT, NULL, &alrt00.smtp_smarthost, NULL, 0, &alrt00.smtp_smarthost_set, FALSE, NULL, 0},
  {"smtp_port", V_INT, CS_ALERT, &alrt00.smtp_port, NULL, NULL, 0, &alrt00.smtp_port_set, FALSE, NULL, 0},
  {"smtp_self", V_STR, CS_ALERT, NULL, &alrt00.smtp_self, NULL, 0, &alrt00.smtp_self_set, FALSE, NULL, 0},
  {"smtp_sender", V_STR, CS_ALERT, NULL, &alrt00.smtp_sender, NULL, 0, &alrt00.smtp_sender_set, TRUE, NULL, 0},
  {"smtp_recipients", V_STR, CS_ALERT, NULL, &alrt00.smtp_recipients, NULL, 0, &alrt00.smtp_recipients_set, FALSE, NULL, 0},
  {"smtp_connect_timeout", V_INT, CS_ALERT, &alrt00.smtp_connect_timeout, NULL, NULL, 0, &alrt00.smtp_connect_timeout_set, FALSE, NULL, 0},
  {"program_command", V_STR, CS_ALERT, NULL, &alrt00.prg_command, NULL, 0, &alrt00.prg_command_set, FALSE, NULL, 0},
  {"log_file", V_STR, CS_ALERT, NULL, &alrt00.log_file, NULL, 0, &alrt00.log_file_set, FALSE, NULL, 0},
  {"log_string", V_STR, CS_ALERT, NULL, &alrt00.log_string, NULL, 0, &alrt00.log_string_set, FALSE, NULL, 0}
};


//
// GENERAL
//

FILE *log_fd;
int g_trace_network_traffic;

int flag_interrupted = FALSE;
int quitting = FALSE;

long int loop_count = 0;

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
  signal(SIGPIPE, SIG_IGN);
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
  if (chk->tcp_expect != NULL)
    free(chk->tcp_expect);

  if (chk->prg_command != NULL)
    free(chk->prg_command);

  if (chk->alerts != NULL)
    free(chk->alerts);
  if (chk->str_prev_status != NULL)
    free(chk->str_prev_status);
  if (chk->alert_ctrl != NULL)
    free(chk->alert_ctrl);
}

//
// Create a check struct
//
void check_t_create(struct check_t *chk) {
/*  dbg_write("Creating check...\n");*/

  chk->is_valid = FALSE;

  chk->method_set = FALSE;

  chk->display_name_set = FALSE;
  chk->display_name = NULL;

  chk->host_name_set = FALSE;
  chk->host_name = NULL;

  chk->tcp_expect_set = FALSE;
  chk->tcp_expect = NULL;

  chk->tcp_port_set = FALSE;
  chk->tcp_connect_timeout_set = FALSE;

  chk->prg_command = NULL;
  chk->prg_command_set = FALSE;

  chk->alerts = NULL;
  chk->alerts_set = FALSE;
  chk->nb_alerts = 0;
  chk->alert_ctrl = NULL;
  chk->alert_threshold = 0;
  chk->alert_threshold_set = FALSE;
  chk->alert_repeat_every = 0;
  chk->alert_repeat_every_set = FALSE;
  chk->alert_repeat_max = 0;
  chk->alert_repeat_max_set = FALSE;
  chk->alert_recovery_set = FALSE;

  chk->status = ST_UNDEF;
  chk->prev_status = ST_UNDEF;
  chk->str_prev_status = NULL;
}

//
// Prepare a check_t (final prep)
//
void check_t_getready(struct check_t *chk) {
  if (!chk->is_valid)
    return;
  chk->nb_consecutive_notok = 0;
  if (chk->str_prev_status != NULL)
    return;
  if (g_nb_keep_last_status >= 1) {
    chk->str_prev_status = (char *)malloc(g_nb_keep_last_status + 1);
    memset(chk->str_prev_status, ST_TO_CHAR[ST_UNDEF], g_nb_keep_last_status);
    chk->str_prev_status[g_nb_keep_last_status] = '\0';
  }
  chk->last_status_change_flag = FALSE;
  chk->trigger_sequence = 0;
  int i;
  for (i = 0; i < chk->nb_alerts; ++i) {
    chk->alert_ctrl[i].alert_status = AS_NOTHING;
    chk->alert_ctrl[i].trigger_sequence = 0;
    chk->alert_ctrl[i].nb_failures = 0;
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
/*  dbg_write("Creating alert...\n");*/

  alrt->is_valid = FALSE;

  alrt->name_set = FALSE;
  alrt->name = NULL;

  alrt->method = AM_UNDEF;
  alrt->method_set = FALSE;

  alrt->threshold = 0;
  alrt->threshold_set = FALSE;
  alrt->repeat_every = 0;
  alrt->repeat_every_set = FALSE;
  alrt->repeat_max = 0;
  alrt->repeat_max_set = FALSE;
  alrt->recovery_set = FALSE;
  alrt->retries = 0;
  alrt->retries_set = FALSE;

    // SMTP

  alrt->smtp_smarthost_set = FALSE;
  alrt->smtp_smarthost = NULL;

  alrt->smtp_port_set = FALSE;

  alrt->smtp_self_set = FALSE;
  alrt->smtp_self = NULL;

  alrt->smtp_sender_set = FALSE;
  alrt->smtp_sender = NULL;

  alrt->smtp_recipients_set = FALSE;
  alrt->smtp_recipients = NULL;

  alrt->smtp_connect_timeout_set = FALSE;

    // PROGRAM

  alrt->prg_command = NULL;
  alrt->prg_command_set = FALSE;

    // LOG
  alrt->log_file = NULL;
  alrt->log_file_set = FALSE;
  alrt->log_string = NULL;
  alrt->log_string_set = FALSE;
}

//
// Finds a word in a table, return index found or -1 if not found.
// Case insensitive search
//
int find_word(const char **table, int n, const char *elem) {
  int i;
  for (i = 0; i < n; ++i) {
    if (strcasecmp(table[i], elem) == 0)
      return i;
  }
  return -1;
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
  *gmtoff = ts.tm_gmtoff;
}

//
// Remplit la structure avec les date/heure actuelles
void set_current_tm(struct tm *ts) {
  time_t ltime = time(NULL);
  *ts = *localtime(&ltime);
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
//
//
#define STR_LOG_TIMESTAMP 25
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

  dbg_write("Mark 1168a\n");
  int e = send(*s, tmp, strlen(tmp), 0);
  dbg_write("Mark 1168b\n");

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
//
//
int socket_round_trip(int sock, const char *expect, const char *fmt, ...) {

  int l = strlen(fmt) + 100;
  char *tmp;
  tmp = (char *)malloc(l + 1);

  va_list args;
  va_start(args, fmt);
  vsnprintf(tmp, l, fmt, args);
  va_end(args);

  int e = socket_line_sendf(&sock, g_trace_network_traffic, tmp);
  free(tmp);
  if (e)
    return SRT_SOCKET_ERROR;

  char *response = NULL;
  int response_size;
  if (socket_read_line_alloc(sock, &response, g_trace_network_traffic, &response_size) < 0) {
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
#define STR_NOW 15
void get_str_now(char *s, size_t s_len, struct tm *ts) {
  snprintf(s, s_len, "%02i/%02i %02i:%02i:%02i",
    g_date_df ? ts->tm_mday : ts->tm_mon + 1, g_date_df ? ts->tm_mon + 1 : ts->tm_mday, ts->tm_hour, ts->tm_min, ts->tm_sec);
}

//
// Fill a string with current time, format is
//    hh:mm
//
#define STR_LASTSTATUS_CHANGE  6
void get_str_last_status_change(char *s, size_t s_len, const struct tm *ts) {
  snprintf(s, s_len, "%02i:%02i", ts->tm_hour, ts->tm_min);
  s[s_len - 1] = '\0';
}

//
// Fill a string with current date and time, format is
//    dd/mm hh:mm
//
#define STR_ALERT_INFO 12
void get_str_alert_info(char *s, size_t s_len, const struct tm *ts) {
  snprintf(s, s_len, "%02i/%02i %02i:%02i",
    g_date_df ? ts->tm_mday : ts->tm_mon + 1, g_date_df ? ts->tm_mon + 1 : ts->tm_mday, ts->tm_hour, ts->tm_min);
}

//
//
//
int establish_connection(const char *host_name, int port, const char *expect, int timeout, int *sock) {
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
    return EC_RESOLVE_ERROR;
  }

  int ret = EC_CONNECTION_ERROR;

  if ((*sock = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP)) == SOCKET_ERROR) {
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

  if (connect_with_timeout(&server, sock, &tv, server_desc) == 0) {
    my_logf(LL_DEBUG, LP_DATETIME, "Connected to %s", server_desc);

    if (expect != NULL && strlen(expect) >= 1) {
      char *response = NULL;
      int response_size;
      if (socket_read_line_alloc(*sock, &response, g_trace_network_traffic, &response_size) < 0) {
        ;
      } else if (s_begins_with(response, expect)) {
        my_logf(LL_DEBUG, LP_DATETIME, "Expected answer: '%s'", response);
        ret = EC_OK;
      } else {
        my_logf(LL_DEBUG, LP_DATETIME, "Unexpected answer: '%s' (expected '%s')", response, expect);
        ret = EC_UNEXPECTED_ANSWER;
      }
      free(response);
    } else {
      ret = EC_OK;
    }

  }

  return ret;
}

//
//
//
int perform_check_tcp(const struct check_t *chk, const struct subst_t *subst, int subst_len) {
  char prefix[SMALLSTRSIZE];
  snprintf(prefix, sizeof(prefix), "TCP check(%s):", chk->display_name);

  int sock;
  my_logf(LL_DEBUG, LP_DATETIME, "%s connecting to %s:%i...", prefix, chk->host_name, chk->tcp_port);
  int ec = establish_connection(chk->host_name, chk->tcp_port, chk->tcp_expect_set ? chk->tcp_expect : NULL,
    chk->tcp_connect_timeout_set ? chk->tcp_connect_timeout : g_connect_timeout, &sock);
  os_closesocket(sock);
  my_logf(LL_VERBOSE, LP_DATETIME, "%s disconnected from %s:%i", prefix, chk->host_name, chk->tcp_port);
  if (ec == EC_OK)
    return ST_OK;
  if (ec == EC_RESOLVE_ERROR)
    return ST_UNKNOWN;
  return ST_FAIL;
}

//
//
//
int perform_check_program(const struct check_t *chk, const struct subst_t *subst, int subst_len) {
  char prefix[SMALLSTRSIZE];
  snprintf(prefix, sizeof(prefix), "Program check(%s):", chk->display_name);

  char *s_substitued = dollar_subst_alloc(chk->prg_command, subst, subst_len);
  my_logf(LL_VERBOSE, LP_DATETIME, "%s will execute the command:", prefix);
  my_logs(LL_VERBOSE, LP_INDENT, s_substitued);

  int r1 = system(s_substitued);
  int r2 = WEXITSTATUS(r1);
  my_logf(LL_VERBOSE, LP_DATETIME, "%s return code: %i", prefix, r2);
  free(s_substitued);
  if (r2 < _NAGIOS_FIRST)
    r2 = NAGIOS_UNKNOWN;
  else if (r2 > _NAGIOS_LAST)
    r2 = NAGIOS_UNKNOWN;
  if (r2 == NAGIOS_OK)
    return ST_OK;
  if (r2 == NAGIOS_WARNING)
    return ST_FAIL;
  if (r2 == NAGIOS_CRITICAL)
    return ST_FAIL;
  if (r2 == NAGIOS_UNKNOWN)
    return ST_UNKNOWN;
  return ST_UNKNOWN;
}

//
//
//
void loop_count_to_str(char *lcstr, size_t lcstr_len) {
  my_pthread_mutex_lock(&mutex);
  long int lc = loop_count;
  my_pthread_mutex_unlock(&mutex);
  snprintf(lcstr, lcstr_len, "%li", lc);
}

//
//
//
int perform_check(const struct check_t *chk) {
  my_logf(LL_VERBOSE, LP_DATETIME, "Performing check %s(%s)", l_check_methods[chk->method], chk->display_name);

  struct tm my_now;
  set_current_tm(&my_now);

    // NOW substitutions
  char now_ts[STR_LOG_TIMESTAMP]; char now_date[9]; char now_y[5]; char now_m[3]; char now_d[3];
  char now_h[3]; char now_mi[3]; char now_s[3];
  set_log_timestamp(now_ts, sizeof(now_ts), my_now.tm_year + 1900, my_now.tm_mon + 1, my_now.tm_mday,
    my_now.tm_hour, my_now.tm_min, my_now.tm_sec, -1);
  snprintf(now_date, sizeof(now_date), "%04d%02d%02d", my_now.tm_year + 1900, my_now.tm_mon + 1, my_now.tm_mday);
  snprintf(now_y, sizeof(now_y), "%04d", my_now.tm_year + 1900);
  snprintf(now_m, sizeof(now_m), "%02d", my_now.tm_mon + 1);
  snprintf(now_d, sizeof(now_d), "%02d", my_now.tm_mday);
  snprintf(now_h, sizeof(now_h), "%02d", my_now.tm_hour);
  snprintf(now_mi, sizeof(now_mi), "%02d", my_now.tm_min);
  snprintf(now_s, sizeof(now_s), "%02d", my_now.tm_sec);

  char lcstr[12];
  loop_count_to_str(lcstr, sizeof(lcstr));

  struct subst_t subst[] = {
    {"DISPLAY_NAME", chk->display_name},
    {"HOST_NAME", chk->host_name},
    {"NOW_TIMESTAMP", now_ts},
    {"NOW_YMD", now_date},
    {"NOW_YEAR", now_y},
    {"NOW_MONTH", now_m},
    {"NOW_DAY", now_d},
    {"NOW_HOUR", now_h},
    {"NOW_MINUTE", now_mi},
    {"NOW_SECOND", now_s},
    {"LOOP_COUNT", lcstr},
    {"TAB", "\t"}
  };
  return check_func[chk->method](chk, subst, sizeof(subst) / sizeof(*subst));
}

//
// Extract SMTP address from string, example ->
// smtp_address("abc <x@c> def") will return "x@c"
// smtp_address("  dd  x@c o@t d,d") will return "x@c o@t"
// smtp_address("  dd  < a x@c d> zz ") will return "a x@c d"
//
char *smtp_address(char *a) {
  char *p1 = strchr(a, '<');
  char *p2 = strrchr(a, '>');
  if (p1 != NULL && p2 != NULL && p2 > p1) {
    *p2 = '\0';
    return trim(p1 + 1);
  }
  char *p = strchr(a, '@');
  if (p != NULL) {
    char *f = strrchr(a, '@');;
    for (; p > a; --p) {
      if (isspace(*p)) {
        ++p;
        break;
      }
    }
    for (; *f != '\0'; ++f) {
      if (isspace(*f)) {
        *f = '\0';
        break;
      }
    }
    return trim(p);
  }
  return trim(a);
}

//
// Fill the string with a boundary string, garanteed unique
//
void get_unique_mime_boundary(char *boundary, size_t boundary_len) {
  int wday; int year; int month; int day;
  int hour; int minute; int second; long unsigned int usec;
  long int gmtoff;
  get_datetime_of_day(&wday, &year, &month, &day, &hour, &minute, &second, &usec, &gmtoff);
  snprintf(boundary, boundary_len, "nm1_%04d%02d%02d%02d%02d%02d%06lu_%d_%d",
    year, month, day, hour, minute, second, usec, rand(), rand());
}

//
// Fill the string with a standard date
//
void get_rfc822_header_format_current_date(char *date, size_t date_len) {
  const char *wdays[] = {"Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat"};
  const char *months[] = {"Jan", "Feb", "Mar", "Apr", "May", "Jun", "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"};

  int wday; int year; int month; int day;
  int hour; int minute; int second; long unsigned int usec;
  long int gmtoff;
  get_datetime_of_day(&wday, &year, &month, &day, &hour, &minute, &second, &usec, &gmtoff);
  int gmtoff_h = abs(gmtoff) / 3600;
  int gmtoff_m = abs(gmtoff) % 3600;
  snprintf(date, date_len, "%s, %d %s %d %02d:%02d:%02d %s%02d%02d", wdays[wday], day, months[month - 1], year,
    hour, minute, second, gmtoff < 0 ? "-" : "+", gmtoff_h, gmtoff_m);
}

//
// Used by execute_alert_smtp
//
int core_execute_alert_smtp_one_host(const struct exec_alert_t *exec_alert, const char *smart_host, int port) {
  struct alert_t *alrt = exec_alert->alrt;

  char prefix[SMALLSTRSIZE];
  snprintf(prefix, sizeof(prefix), "SMTP alert(%s):", alrt->name);

  int sock;
  my_logf(LL_DEBUG, LP_DATETIME, "%s connecting to %s:%i...", prefix, smart_host, port);
  int ec = establish_connection(smart_host, port, "220 ",
    alrt->smtp_connect_timeout_set ? alrt->smtp_connect_timeout : g_connect_timeout, &sock);
  if (ec != EC_OK)
    return (ec == EC_RESOLVE_ERROR ? ERR_SMTP_RESOLVE_ERROR : ERR_SMTP_NETIO);

  if (socket_line_sendf(&sock, g_trace_network_traffic, "EHLO %s",
      alrt->smtp_self_set ? alrt->smtp_self : DEFAULT_ALERT_SMTP_SELF)) {
    return ERR_SMTP_NETIO;
  }

  char *response = NULL;
  int response_size;
  do {
    if (socket_read_line_alloc(sock, &response, g_trace_network_traffic, &response_size) < 0)
      return ERR_SMTP_NETIO;
  } while (s_begins_with(response, "250-"));
  if (!s_begins_with(response, "250 ")) {
    my_logf(LL_ERROR, LP_DATETIME, "%s unexpected answer from server '%s'", prefix, response);
    return ERR_SMTP_BAD_ANSWER_TO_EHLO;
  }
  char from_buf[SMALLSTRSIZE];
  char *from_orig = alrt->smtp_sender_set ? alrt->smtp_sender : DEFAULT_ALERT_SMTP_SENDER;
  strncpy(from_buf, from_orig, sizeof(from_buf));
  char *from = from_buf;
  from = smtp_address(from);
  if (socket_round_trip(sock, "250 ", "MAIL FROM: <%s>", from) != SRT_SUCCESS) {
    my_logf(LL_ERROR, LP_DATETIME, "%s sender not accepted, closing connection", prefix);
    return ERR_SMTP_SENDER_REJECTED;
  }

  int nb_ok_recipients = 0;
  size_t l = strlen(alrt->smtp_recipients) + 1;
  char *recipients = (char *)malloc(l);
  strncpy(recipients, alrt->smtp_recipients, l);
  char *r = recipients;
  char *next = NULL;
  while (*r != '\0') {
    if ((next = strchr(r, CFGK_LIST_SEPARATOR)) != NULL) {
      *next = '\0';
      ++next;
    }
    r = smtp_address(r);
    if (strlen(r) >= 1) {
      int res = socket_round_trip(sock, "250 ", "RCPT TO: <%s>", r);
      if (res == SRT_SUCCESS)
        ++nb_ok_recipients;
      else if (res != SRT_SUCCESS && res != SRT_UNEXPECTED_ANSWER) {
        free(recipients);
        return ERR_SMTP_NETIO;
      }
    }

    r = (next == NULL ? &r[strlen(r)] : next);
  }
  free(recipients);

  if (nb_ok_recipients == 0) {
    my_logf(LL_ERROR, LP_DATETIME, "%s no recipient accepted, closing connection", prefix);
    socket_line_sendf(&sock, g_trace_network_traffic, "QUIT");
    os_closesocket(sock);
    return ERR_SMTP_NO_RECIPIENT_ACCEPTED;
  }

  int res;
  if ((res = socket_round_trip(sock, "354 ", "DATA")) != SRT_SUCCESS) {
    my_logf(LL_ERROR, LP_DATETIME, "%s DATA command not accepted, closing connection", prefix);
    return (res == SRT_SOCKET_ERROR ? ERR_SMTP_NETIO : ERR_SMTP_DATA_COMMAND_REJECTED);
  }
  my_logf(LL_DEBUG, LP_DATETIME, "%s will now send email content", prefix);

  char boundary[SMALLSTRSIZE];
  get_unique_mime_boundary(boundary, sizeof(boundary));
  char date[SMALLSTRSIZE];
  get_rfc822_header_format_current_date(date, sizeof(date));

// Email headers

  if (strlen(from_orig) >= 1) {
    socket_line_sendf(&sock, g_trace_network_traffic, "return-path: %s", from);
    socket_line_sendf(&sock, g_trace_network_traffic, "sender: %s", from);
    socket_line_sendf(&sock, g_trace_network_traffic, "from: %s", from_orig);
  }
  socket_line_sendf(&sock, g_trace_network_traffic, "to: %s", alrt->smtp_recipients);
  socket_line_sendf(&sock, g_trace_network_traffic, "x-mailer: %s", PACKAGE_STRING);
  socket_line_sendf(&sock, g_trace_network_traffic, "subject: %s", exec_alert->desc);
  socket_line_sendf(&sock, g_trace_network_traffic, "date: %s", date);
  socket_line_sendf(&sock, g_trace_network_traffic, "MIME-Version: 1.0");
  socket_line_sendf(&sock, g_trace_network_traffic, "Content-Type: multipart/alternative; boundary=%s", boundary);
  socket_line_sendf(&sock, g_trace_network_traffic, "");

// Email body

    // Alternative 1: plain text
  socket_line_sendf(&sock, g_trace_network_traffic, "--%s", boundary);
  socket_line_sendf(&sock, g_trace_network_traffic, "Content-Type: text/plain; charset=\"us-ascii\"");
  socket_line_sendf(&sock, g_trace_network_traffic, "Content-Transfer-Encoding: 7bit");
  socket_line_sendf(&sock, g_trace_network_traffic, "");
  socket_line_sendf(&sock, g_trace_network_traffic, "%s", exec_alert->desc);
  socket_line_sendf(&sock, g_trace_network_traffic, "");

    // Alternative 2: html
  socket_line_sendf(&sock, g_trace_network_traffic, "--%s", boundary);
  socket_line_sendf(&sock, g_trace_network_traffic, "Content-Type: text/html; charset=\"UTF-8\"");
  socket_line_sendf(&sock, g_trace_network_traffic, "Content-Transfer-Encoding: 7bit");
  socket_line_sendf(&sock, g_trace_network_traffic, "");
/*  socket_line_sendf(&sock, g_trace_network_traffic, "<!--");*/
/*  socket_line_sendf(&sock, g_trace_network_traffic, "-->");*/
  socket_line_sendf(&sock, g_trace_network_traffic, "<html>");
  socket_line_sendf(&sock, g_trace_network_traffic, "<body>");
  socket_line_sendf(&sock, g_trace_network_traffic, "<table cellpadding=\"2\" cellspacing=\"1\" border=\"1\">");
 
  socket_line_sendf(&sock, g_trace_network_traffic, "<tr><td bgcolor=\"%s\">", ST_TO_BGCOLOR_FORHTML[exec_alert->status]);
  socket_line_sendf(&sock, g_trace_network_traffic, "%s", exec_alert->desc);
  socket_line_sendf(&sock, g_trace_network_traffic, "</td></tr></table>");
  socket_line_sendf(&sock, g_trace_network_traffic, "</body>");
  socket_line_sendf(&sock, g_trace_network_traffic, "</html>");
  socket_line_sendf(&sock, g_trace_network_traffic, "");

    // End of alternatives
  socket_line_sendf(&sock, g_trace_network_traffic, "--%s--", boundary);
  socket_line_sendf(&sock, g_trace_network_traffic, "");

// End

  socket_line_sendf(&sock, g_trace_network_traffic, ".", alrt->smtp_recipients);
  socket_line_sendf(&sock, g_trace_network_traffic, "QUIT");

  os_closesocket(sock);
  my_logf(LL_DEBUG, LP_DATETIME, "Disconnected from %s:%i", smart_host, port);

  return ERR_SMTP_OK;
}

//
// Execute alert when method == AM_SMTP
//
int execute_alert_smtp(const struct exec_alert_t *exec_alert) {
  const char *h = exec_alert->alrt->smtp_smarthost;
  int port = exec_alert->alrt->smtp_port_set ? exec_alert->alrt->smtp_port : DEFAULT_ALERT_SMTP_PORT;

  int err_smtp = core_execute_alert_smtp_one_host(exec_alert, h, port);

  return err_smtp;
}

//
// Execute alert when method == AM_PROGRAM
//
int execute_alert_program(const struct exec_alert_t *exec_alert) {
  struct alert_t *alrt = exec_alert->alrt;

  char prefix[SMALLSTRSIZE];
  snprintf(prefix, sizeof(prefix), "Program alert(%s):", alrt->name);

  char *s_substitued = dollar_subst_alloc(alrt->prg_command, exec_alert->subst, exec_alert->subst_len);
  my_logf(LL_VERBOSE, LP_DATETIME, "%s will execute the command:", prefix);
  my_logs(LL_VERBOSE, LP_INDENT, s_substitued);
  int r1 = system(s_substitued);
  int r2 = WEXITSTATUS(r1);
  my_logf(LL_VERBOSE, LP_DATETIME, "%s return code: %i", prefix, r2);
  free(s_substitued);
  return r2;
}

//
// Execute alert when method == AM_LOG
//
int execute_alert_log(const struct exec_alert_t *exec_alert) {
  struct alert_t *alrt = exec_alert->alrt;
  struct tm *now = exec_alert->my_now;

  char prefix[SMALLSTRSIZE];
  snprintf(prefix, sizeof(prefix), "Log alert(%s):", alrt->name);

  char *f_substitued = dollar_subst_alloc(alrt->log_file, exec_alert->subst, exec_alert->subst_len);

  FILE *H = fopen(f_substitued, "a+");

  int ret = 0;

  if (H == NULL) {
    my_logf(LL_ERROR, LP_DATETIME, "%s unable to open log file '%s'", prefix);
    ret = -1;
  } else {
    char *s_substitued = dollar_subst_alloc(alrt->log_string, exec_alert->subst, exec_alert->subst_len);
    fputs(s_substitued, H);
    fputs("\n", H);

    my_logf(LL_VERBOSE, LP_DATETIME, "%s wrote in log '%s':", prefix, f_substitued);
    my_logs(LL_VERBOSE, LP_INDENT, s_substitued);
    free(s_substitued);

    fclose(H);
  }

  free(f_substitued);
  return ret;
}

//
// Execute alert for all methods
//
int execute_alert(struct exec_alert_t *exec_alert) {
  my_logf(LL_VERBOSE, LP_DATETIME, "%s(%s) -> display_name = '%s', host_name = '%s', status = '%i'",
    l_alert_methods[exec_alert->alrt->method], exec_alert->alrt->name,
    exec_alert->display_name, exec_alert->host_name, exec_alert->status);

  char desc[SMALLSTRSIZE];
  char alert_info[STR_ALERT_INFO];
  get_str_alert_info(alert_info, sizeof(alert_info), exec_alert->alert_info);
  snprintf(desc, sizeof(desc), "%s [%s] in status %s since %s",
    exec_alert->display_name, exec_alert->host_name, ST_TO_LONGSTR_SIMPLE[exec_alert->status], alert_info);
  exec_alert->desc = desc;

    // NOW substitutions
  struct tm *mn = exec_alert->my_now;
  char now_ts[STR_LOG_TIMESTAMP]; char now_date[9]; char now_y[5]; char now_m[3]; char now_d[3];
  char now_h[3]; char now_mi[3]; char now_s[3];
  set_log_timestamp(now_ts, sizeof(now_ts), mn->tm_year + 1900, mn->tm_mon + 1, mn->tm_mday,
    mn->tm_hour, mn->tm_min, mn->tm_sec, -1);
  snprintf(now_date, sizeof(now_date), "%04d%02d%02d", mn->tm_year + 1900, mn->tm_mon + 1, mn->tm_mday);
  snprintf(now_y, sizeof(now_y), "%04d", mn->tm_year + 1900);
  snprintf(now_m, sizeof(now_m), "%02d", mn->tm_mon + 1);
  snprintf(now_d, sizeof(now_d), "%02d", mn->tm_mday);
  snprintf(now_h, sizeof(now_h), "%02d", mn->tm_hour);
  snprintf(now_mi, sizeof(now_mi), "%02d", mn->tm_min);
  snprintf(now_s, sizeof(now_s), "%02d", mn->tm_sec);
    // ALERT_INFO substitutions
  struct tm *ai = exec_alert->alert_info;
  char ai_ts[STR_LOG_TIMESTAMP]; char ai_date[9]; char ai_y[5]; char ai_m[3]; char ai_d[3];
  char ai_h[3]; char ai_mi[3]; char ai_s[3];
  set_log_timestamp(ai_ts, sizeof(ai_ts), ai->tm_year + 1900, ai->tm_mon + 1, ai->tm_mday,
    ai->tm_hour, ai->tm_min, ai->tm_sec, -1);
  snprintf(ai_date, sizeof(ai_date), "%04d%02d%02d", ai->tm_year + 1900, ai->tm_mon + 1, ai->tm_mday);
  snprintf(ai_y, sizeof(ai_y), "%04d", ai->tm_year + 1900);
  snprintf(ai_m, sizeof(ai_m), "%02d", ai->tm_mon + 1);
  snprintf(ai_d, sizeof(ai_d), "%02d", ai->tm_mday);
  snprintf(ai_h, sizeof(ai_h), "%02d", ai->tm_hour);
  snprintf(ai_mi, sizeof(ai_mi), "%02d", ai->tm_min);
  snprintf(ai_s, sizeof(ai_s), "%02d", ai->tm_sec);

  char nstatus[12]; char nconsec[12]; char nalertst[12]; char seq[12]; char nb_failures[12];
  snprintf(nstatus, sizeof(nstatus), "%d", exec_alert->status);
  snprintf(nconsec, sizeof(nconsec), "%d", exec_alert->nb_consecutive_notok);
  snprintf(nalertst, sizeof(nalertst), "%d", exec_alert->alert_status);
  snprintf(seq, sizeof(nalertst), "%d", exec_alert->alert_ctrl->trigger_sequence);
  snprintf(nb_failures, sizeof(nb_failures), "%d", exec_alert->alert_ctrl->nb_failures);

  char lcstr[12];
  loop_count_to_str(lcstr, sizeof(lcstr));

  struct subst_t subst[] = {
    {"DESCRIPTION", desc},
    {"STATUS", ST_TO_LONGSTR_SIMPLE[exec_alert->status]},
    {"STATUS_NUM", nstatus},
    {"DISPLAY_NAME", exec_alert->display_name},
    {"HOST_NAME", exec_alert->host_name},
    {"CONSECUTIVE_NOTOK", nconsec},
    {"ALERT_NAME", exec_alert->alrt->name},
    {"ALERT_METHOD", l_alert_methods[exec_alert->alrt->method]},
    {"ALERT_STATUS", alert_status_names[exec_alert->alert_status]},
    {"ALERT_STATUS_NUM", nalertst},
    {"ALERT_SEQ", seq},
    {"ALERT_NB_FAILURES", nb_failures},
    {"ALERT_TIMESTAMP", ai_ts},
    {"ALERT_YMD", ai_date},
    {"ALERT_YEAR", ai_y},
    {"ALERT_MONTH", ai_m},
    {"ALERT_DAY", ai_d},
    {"ALERT_HOUR", ai_h},
    {"ALERT_MINUTE", ai_mi},
    {"ALERT_SECOND", ai_s},
    {"NOW_TIMESTAMP", now_ts},
    {"NOW_YMD", now_date},
    {"NOW_YEAR", now_y},
    {"NOW_MONTH", now_m},
    {"NOW_DAY", now_d},
    {"NOW_HOUR", now_h},
    {"NOW_MINUTE", now_mi},
    {"NOW_SECOND", now_s},
    {"LOOP_COUNT", lcstr},
    {"TAB", "\t"}
  };
  exec_alert->subst = subst;
  exec_alert->subst_len = sizeof(subst) / sizeof(*subst);

  return alert_func[exec_alert->alrt->method](exec_alert);
}

//
// Main loop
//
const char *TERM_CLEAR_SCREEN = "\033[2J\033[1;1H";
void almost_neverending_loop() {

  while (1) {

    my_pthread_mutex_lock(&mutex);
    ++loop_count;
    int lc = loop_count;
    my_pthread_mutex_unlock(&mutex);

    int II;

    my_logs(LL_NORMAL, LP_DATETIME, "Starting check...");

    struct timeval tv0;
    if (gettimeofday(&tv0, NULL) == GETTIMEOFDAY_ERROR)
      fatal_error("File %s, line %i, gettimeofday() error", __FILE__, __LINE__);

    for (II = 0; II < g_nb_checks; ++II) {
      struct check_t *chk = &checks[II];
      if (!chk->is_valid)
        continue;

      int status = perform_check(chk);
      if (status < 0 || status > ST_LAST)
        internal_error("almost_neverending_loop", __FILE__, __LINE__);

      struct tm my_now;
      set_current_tm(&my_now);

      chk->prev_status = chk->status;
      chk->status = status;

      int reset_nb_failures = FALSE;
      if (chk->prev_status != chk->status && chk->prev_status != ST_UNDEF) {
        chk->last_status_change = my_now;
        chk->last_status_change_flag = TRUE;
        reset_nb_failures = TRUE;
      }
      if ((chk->status != ST_OK || chk->prev_status != ST_OK) && chk->status != chk->prev_status) {
        set_current_tm(&chk->alert_info);
        reset_nb_failures = TRUE;
      }

      int as;
      if (chk->status != ST_OK) {
        as = AS_FAIL;
        chk->nb_consecutive_notok++;
      } else {
        as = (chk->prev_status != ST_OK && chk->prev_status != ST_UNDEF ? AS_RECOVERY : AS_NOTHING);
        chk->nb_consecutive_notok = 0;
      }

      if (chk->last_status_change_flag) {
        time_t lsc = mktime(&chk->last_status_change);
        if ((long signed int)tv0.tv_sec - (long signed int)lsc >= LAST_STATUS_CHANGE_DISPLAY_SECONDS) {
          chk->last_status_change_flag = FALSE;
        }
      }

#ifdef DEBUG
      my_logf(LL_NORMAL, LP_DATETIME, "%s -> %s (%i)", chk->display_name, ST_TO_LONGSTR_FANCY[chk->status], chk->nb_consecutive_notok);
#else
      my_logf(LL_NORMAL, LP_DATETIME, "%s -> %s", chk->display_name, ST_TO_LONGSTR_FANCY[chk->status]);
#endif

        // Update status history
      if (g_nb_keep_last_status >= 1) {
        char *buf = (char *)malloc(g_nb_keep_last_status + 1);
        strncpy(buf, chk->str_prev_status + 1, g_nb_keep_last_status + 1);
        strncpy(chk->str_prev_status, buf, g_nb_keep_last_status + 1);
        char t[] = "A";
        t[0] = ST_TO_CHAR[chk->status];
        strncat(chk->str_prev_status, t, g_nb_keep_last_status + 1);
        free(buf);
      }

// Manage alert

      int trigger_alert = FALSE;
      if (as == AS_NOTHING)
        chk->trigger_sequence = 0;

      int threshold = chk->alert_threshold_set ? chk->alert_threshold : DEFAULT_ALERT_THRESHOLD;
      int repeat_max = chk->alert_repeat_max_set ? chk->alert_repeat_max : DEFAULT_ALERT_REPEAT_MAX;
      if (chk->alert_threshold_set && chk->alert_threshold == chk->nb_consecutive_notok) {
        trigger_alert = TRUE;
        chk->trigger_sequence++;
      } else if (chk->alert_repeat_every_set) {
        if (chk->nb_consecutive_notok - threshold >= chk->alert_repeat_every &&
              (chk->nb_consecutive_notok - threshold + chk->alert_repeat_every) % chk->alert_repeat_every == 0) {
          my_logf(LL_DEBUG, LP_DATETIME, "repeat_max = %d", repeat_max);
          trigger_alert = (repeat_max < 0 ? TRUE : (chk->trigger_sequence <= repeat_max));
          chk->trigger_sequence++;
        }
      }

      my_logf(LL_DEBUG, LP_DATETIME, "as = %d, chk->trigger_sequence = %d", as, chk->trigger_sequence);
      my_logf(LL_DEBUG, LP_DATETIME, "chk->nb_consecutive_notok = %d", chk->nb_consecutive_notok);
      my_logf(LL_DEBUG, LP_DATETIME, "trigger_alert = %d", trigger_alert);

      int i;
      for (i = 0; i < chk->nb_alerts; ++i) {
        int trigger_alert_by_alert = trigger_alert;
        struct alert_t *alrt = &alerts[chk->alert_ctrl[i].idx];

        if (reset_nb_failures)
          chk->alert_ctrl[i].nb_failures = 0;

        if (!chk->alert_threshold_set) {
          threshold = alrt->threshold_set ? alrt->threshold : DEFAULT_ALERT_THRESHOLD;
          if (threshold == chk->nb_consecutive_notok)
            trigger_alert_by_alert = TRUE;
        }

        if (!chk->alert_repeat_every_set) {
          int resend_every = alrt->repeat_every_set ? alrt->repeat_every : DEFAULT_ALERT_REPEAT_EVERY;
          if (chk->nb_consecutive_notok - threshold >= resend_every &&
                (chk->nb_consecutive_notok - threshold + resend_every) % resend_every == 0) {
            int repm = alrt->repeat_max_set ? alrt->repeat_max : repeat_max;
            trigger_alert_by_alert = (repm < 0 ? TRUE : (chk->alert_ctrl[i].trigger_sequence <= repm));
          }
        }

        int retries = (alrt->retries_set ? alrt->retries : DEFAULT_ALERT_RETRIES);
        if (chk->alert_ctrl[i].alert_status == AS_FAIL && as == AS_RECOVERY) {
          if (chk->alert_recovery_set && chk->alert_recovery) {
            trigger_alert_by_alert = TRUE;
          } else if (!chk->alert_recovery_set) {
            if (alrt->recovery_set && alrt->recovery) {
              trigger_alert_by_alert = TRUE;
            } else if (!alrt->recovery_set) {
              if (!trigger_alert_by_alert)
                trigger_alert_by_alert = DEFAULT_ALERT_RECOVERY;
            }
          }
        } else if (chk->alert_ctrl[i].alert_status == AS_RECOVERY && chk->alert_ctrl[i].nb_failures <= retries) {
          trigger_alert_by_alert = TRUE;
        }

        int increase_seq = TRUE;

        if (chk->alert_ctrl[i].nb_failures >= 1 && chk->alert_ctrl[i].nb_failures <= retries) {
          trigger_alert_by_alert = TRUE;
          increase_seq = FALSE;
        }

          // Here we go! We have to trigger the alert, whatever the reason is (check
          // config or alert config or default config or any combination)
        if (trigger_alert_by_alert) {
          if (increase_seq)
            chk->alert_ctrl[i].trigger_sequence++;

          my_logf(LL_DEBUG, LP_DATETIME, "chk trigger sequence = %d, alert trigger sequence = %d", chk->trigger_sequence, chk->alert_ctrl[i].trigger_sequence);

          struct exec_alert_t exec_alert = {
            chk->status,
            as,
            alrt,
            &chk->alert_ctrl[i],
            lc,
            &my_now,
            &chk->alert_info,
            &chk->last_status_change,
            chk->nb_consecutive_notok,
            chk->display_name,
            chk->host_name,
            NULL,
            0,
            NULL
          };

          int r = execute_alert(&exec_alert);

          my_logf(LL_DEBUG, LP_DATETIME, "Excuted alert, result = %d", r);

          if (r != 0) {
            chk->alert_ctrl[i].nb_failures++;
            if (as != AS_NOTHING)
              chk->alert_ctrl[i].alert_status = as;

            if (chk->alert_ctrl[i].nb_failures > retries) {
              chk->alert_ctrl[i].nb_failures = 0;
              if (chk->alert_ctrl[i].alert_status == AS_RECOVERY)
                chk->alert_ctrl[i].alert_status = AS_NOTHING;
            }
          } else {
            if (as == AS_NOTHING) {
              chk->alert_ctrl[i].trigger_sequence = 0;
            }
            chk->alert_ctrl[i].nb_failures = 0;
            chk->alert_ctrl[i].alert_status = (as == AS_RECOVERY ? AS_NOTHING : as);
          }
        } else if (as == AS_NOTHING) {
          chk->alert_ctrl[i].alert_status = AS_NOTHING;
          chk->alert_ctrl[i].trigger_sequence = 0;
          chk->alert_ctrl[i].nb_failures = 0;
        }
      }
    }

    struct tm now_done;
    set_current_tm(&now_done);

    struct timeval tv1;
    if (gettimeofday(&tv1, NULL) == GETTIMEOFDAY_ERROR)
      fatal_error("File %s, line %i, gettimeofday() error", __FILE__, __LINE__);

    float elapsed = (long signed int)tv1.tv_sec - (long signed int)tv0.tv_sec;
    elapsed += ((float)tv1.tv_usec - (float)tv0.tv_usec) / 1000000;
    if (g_test_mode >= 1)
      elapsed = .12345;

    if (g_print_status) {
      const char *LC_PREFIX = "Last check: ";
      char now[STR_NOW];
      get_str_now(now, sizeof(now), &now_done);
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
    if (g_test_mode == 0) {
      H = fopen(g_html_complete_file_name, "w+");
      if (H == NULL)
        my_logf(LL_ERROR, LP_DATETIME, "Unable to open HTML output file %s", g_html_complete_file_name);
      else
        my_logf(LL_VERBOSE, LP_DATETIME, "Creating %s", g_html_complete_file_name);
    }
    if (H != NULL) {
      char now[STR_NOW];
      get_str_now(now, sizeof(now), &now_done);
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

      char lsc[STR_LASTSTATUS_CHANGE];
      strncpy(lsc, "", sizeof(lsc));
      if (chk->last_status_change_flag)
        get_str_last_status_change(lsc, sizeof(lsc), &chk->last_status_change);

      if (g_print_status) {
        char short_display_name[g_display_name_width + 1];
        strncpy(short_display_name, chk->display_name, g_display_name_width + 1);
        short_display_name[g_display_name_width] = '\0';

        char f[50];
        if (g_nb_keep_last_status >= 1) {
          snprintf(f, sizeof(f), "%%-%lis %%s |%%s| %%s\n", g_display_name_width);
          printf(f, short_display_name, ST_TO_STR2[chk->status], chk->str_prev_status, lsc);
        } else {
          snprintf(f, sizeof(f), "%%-%lis %%s %%s\n", g_display_name_width);
          printf(f, short_display_name, ST_TO_STR2[chk->status], lsc);
        }
      }

      if (H != NULL) {
        if (counter % g_html_nb_columns == 0)
          fputs("<tr>\n", H);
        fprintf(H, "<td>%s</td><td bgcolor=\"%s\">%s</td>\n",
          chk->display_name, ST_TO_BGCOLOR_FORHTML[chk->status], ST_TO_LONGSTR_SIMPLE[chk->status]);
        fprintf(H, "<td>%s</td>", lsc);
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

    if (g_check_interval && g_test_mode == 0) {
      long int delay = g_check_interval - (long int)elapsed;
      if (delay < 1)
        delay = 1;
      if (delay > g_check_interval)
        delay = g_check_interval;
      my_logf(LL_NORMAL, LP_DATETIME, "Now sleeping for %li second(s) (interval = %li)", delay, g_check_interval);
      os_sleep(delay);
    } else if (g_test_mode >=1) {
      if (g_test_mode == 1)
        break;
      else if (g_test_mode == 2 && lc == TEST2_NB_LOOPS)
        break;
    }
  };
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
  printf("  -h --help          Display this help text\n");
  printf("  -V --version       Display version information and exit\n");
  printf("  -v --verbose       Be more talkative\n");
  printf("  -q --quiet         Be less talkative\n");
  printf("  -l --log-file      Log file (default: %s)\n", DEFAULT_LOGFILE);
  printf("  -c --config-file   Configuration file (default: %s)\n", DEFAULT_CFGFILE);
  printf("  -p --print-log     Print the log on the screen\n");
  printf("  -C --stdout        Print status on stdout\n");
  printf("  -t --test N        Set test mode to N, 0 (default) = no test mode\n");
  printf("  -a --alert         Test the alert name written after the option and quit\n");
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

//
// Analyz program options ->
//   * The log (my_logf and my_logs functions) is not yet open *
//
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
    {"test", required_argument, NULL, 't'},
    {"alert", no_argument, NULL, 'a'},
    {0, 0, 0, 0}
  };

  int c;
  int option_index = 0;

  int n;

  strncpy(g_log_file, DEFAULT_LOGFILE, sizeof(g_log_file));
  strncpy(g_cfg_file, DEFAULT_CFGFILE, sizeof(g_cfg_file));
  strncpy(g_test_alert, "", sizeof(g_test_alert));

  while (1) {

    c = getopt_long(argc, argv, "hvCt:l:c:a:pVq", long_options, &option_index);

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

      case 'a':
        strncpy(g_test_alert, optarg, sizeof(g_test_alert));
        break;

      case 'C':
        g_print_status = TRUE;
        break;

      case 't':
        n = atoi(optarg);
        g_test_mode = n;
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

  if (!chk->display_name_set) {
    my_logf(LL_ERROR, LP_DATETIME, "Configuration file '%s', section of line %i: no display_name defined, discarding check",
      cf, line_number);
    is_valid = FALSE;
  }
  if (!chk->method_set) {
    my_logf(LL_ERROR, LP_DATETIME, "Configuration file '%s', section of line %i: no method defined, discarding check",
      cf, line_number);
    is_valid = FALSE;
  } else if (chk->method == FIND_STRING_NOT_FOUND) {
    my_logf(LL_ERROR, LP_DATETIME, "Configuration file '%s', section of line %i: unknown method, discarding check",
      cf, line_number);
    is_valid = FALSE;
  }

  if (chk->method_set && chk->method == CM_TCP) {
    if (!chk->host_name_set) {
      my_logf(LL_ERROR, LP_DATETIME, "Configuration file '%s', section of line %i: no host name defined, discarding check",
        cf, line_number);
      is_valid = FALSE;
    }
    if (!chk->tcp_port_set) {
      my_logf(LL_ERROR, LP_DATETIME, "Configuration file '%s', section of line %i: no port defined, discarding check",
        cf, line_number);
      is_valid = FALSE;
    }
  } else if (chk->method_set && chk->method == CM_TCP) {
    if (!chk->prg_command_set) {
      my_logf(LL_ERROR, LP_DATETIME, "Configuration file '%s', section of line %i: no command defined, discarding check",
        cf, line_number);
      is_valid = FALSE;
    }
  }

  chk->is_valid = is_valid;
  if (!chk->is_valid)
    return;

  ++g_nb_valid_checks;

  if (!chk->host_name_set) {
    chk->host_name = (char *)malloc(1);
    strncpy(chk->host_name, "", 1);
    chk->host_name_set = TRUE;
  }
}

//
// Check an alert variable
//
void alert_t_check(struct alert_t *alrt, const char *cf, int line_number) {
  g_nb_alerts++;

  int is_valid = TRUE;

  if (!alrt->name_set) {
    my_logf(LL_ERROR, LP_DATETIME, "Configuration file '%s', section of line %i: no name defined, discarding alert",
      cf, line_number);
    is_valid = FALSE;
  }
  if (!alrt->method_set) {
    my_logf(LL_ERROR, LP_DATETIME, "Configuration file '%s', section of line %i: no method defined, discarding alert",
      cf, line_number);
    is_valid = FALSE;
  } else if (alrt->method == FIND_STRING_NOT_FOUND) {
    my_logf(LL_ERROR, LP_DATETIME, "Configuration file '%s', section of line %i: unknown method, discarding alert",
      cf, line_number);
    is_valid = FALSE;
  }

  if (alrt->method == AM_SMTP) {
    if (!alrt->smtp_smarthost_set) {
      my_logf(LL_ERROR, LP_DATETIME, "Configuration file '%s', section of line %i: no smart host defined, discarding alert",
        cf, line_number);
      is_valid = FALSE;
    }
    if (!alrt->smtp_recipients_set) {
      my_logf(LL_ERROR, LP_DATETIME, "Configuration file '%s', section of line %i: no recipients defined, discarding alert",
        cf, line_number);
      is_valid = FALSE;
    }
  } else if (alrt->method == AM_PROGRAM) {
    if (!alrt->prg_command_set) {
      my_logf(LL_ERROR, LP_DATETIME, "Configuration file '%s', section of line %i: no command defined, discarding alert",
        cf, line_number);
      is_valid = FALSE;
    }
  } else if (alrt->method == AM_LOG) {
    if (!alrt->log_file_set) {
      my_logf(LL_ERROR, LP_DATETIME, "Configuration file '%s', section of line %i: no log file defined, discarding alert",
        cf, line_number);
      is_valid = FALSE;
    }
    if (!alrt->log_string_set) {
      size_t n = strlen(DEFAULT_ALERT_LOG_STRING) + 1;
      alrt->log_string = (char *)malloc(n);
      strncpy(alrt->log_string, DEFAULT_ALERT_LOG_STRING, n);
      alrt->log_string_set = TRUE;
    }
  }

  alrt->is_valid = is_valid;

  if (alrt->is_valid)
    ++g_nb_valid_alerts;
}

//
// Parse the ini file
//
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

          if (read_status == CS_PROBE) {
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

            if (strcasecmp(section_name, CS_CHECK_STR) == 0) {
              ++cur_check;
/*              dbg_write("New check: %i\n", cur_check);*/
              if (cur_check >= sizeof(checks) / sizeof(*checks)) {
                --cur_check;
                my_logf(LL_ERROR, LP_DATETIME,
                  "Configuration file '%s', line %i: reached max number of checks (%i)",
                  cf, line_number, sizeof(checks) / sizeof(*checks));
              } else {
                read_status = CS_PROBE;
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
              if (strcasecmp(key, readcfg_vars[i].name) == 0 && read_status == readcfg_vars[i].section) {
                match = TRUE;
                struct readcfg_var_t cfg = readcfg_vars[i];

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
                  } else if (cfg.var_type == V_YESNO) {

                      // Variable of type long int with yes/no input
                    int yn = find_word(l_yesno, sizeof(l_yesno) / sizeof(*l_yesno), value);
                    if (yn == FIND_STRING_NOT_FOUND) {
                      my_logf(LL_ERROR, LP_DATETIME,
                        "Configuration file '%s', line %i: variable %s must be set to yes or no",
                        cf, line_number, key);
                    }
                    *cfg.plint_target = (yn == ID_YES ? TRUE : FALSE);
                    *cfg.pint_var_set = TRUE;

                  } else if (cfg.var_type == V_STRKEY) {

                      // Variable of type long int with a string table key list
                    int idx = find_word(cfg.table, cfg.table_nb_elems, value);
                    if (idx == FIND_STRING_NOT_FOUND) {
                      my_logf(LL_ERROR, LP_DATETIME,
                        "Configuration file '%s', line %i: unknown value '%s' for variable %s",
                        cf, line_number, value, key);
                    }
                    *cfg.plint_target = idx;
                    *cfg.pint_var_set = TRUE;

                  } else {
                    internal_error("read_configuration_file", __FILE__, __LINE__);
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
                break;
              }
            }
            if (!match) {
              for (i = 0; (unsigned int)i < sizeof(readcfg_vars) / sizeof(*readcfg_vars); ++i) {
                if (strcasecmp(key, readcfg_vars[i].name) == 0) {
                  match = TRUE;
                  break;
                }
              }
              if (match) {
                my_logf(LL_ERROR, LP_DATETIME,
                  "Configuration file '%s', line %i: variable %s not allowed in this section",
                  cf, line_number, key);
              } else {
                my_logf(LL_ERROR, LP_DATETIME, "Configuration file '%s', line %i: unknown variable %s",
                  cf, line_number, key);
              }
            }
          }
        }
      break;
    }
  }
  if (cur_check >= 0 && read_status == CS_PROBE) {
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
  if (!g_date_format_set)
    g_date_format = (g_date_format == FIND_STRING_NOT_FOUND ? DEFAULT_DATE_FORMAT : g_date_format);
  g_date_df = (g_date_format == DF_FRENCH);

  strncpy(g_html_complete_file_name, g_html_directory, sizeof(g_html_complete_file_name));
  fs_concatene(g_html_complete_file_name, g_html_file, sizeof(g_html_complete_file_name));

/*  dbg_write("Output HTML file = %s\n", g_html_complete_file_name);*/

  g_print_log = save_g_print_log;
}

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
  while (1) {
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
// Identify an alert by its name
// Returns -1 if the alert is not found
//
int find_alert(const char *alert_name) {
  int i;
  for (i = 0; i < g_nb_alerts; ++i) {
    if (alerts[i].is_valid) {
      if (strcasecmp(alerts[i].name, alert_name) == 0)
        break;
    }
  }
  return (i < g_nb_alerts ? i : -1);
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
    chk->alert_ctrl = (struct alert_ctrl_t *)malloc(sizeof(struct alert_ctrl_t) * n);
    size_t b = strlen(chk->alerts) + 2;

    char *t = (char *)malloc(b);
    strncpy(t, chk->alerts, b);

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
      if ((k = find_alert(curs)) < 0) {
        my_logf(LL_ERROR, LP_DATETIME, "Check '%s': unknown alert '%s'", chk->display_name, curs);
      } else {
        chk->alert_ctrl[index].idx = k;
        ++index;
      }
    }
    chk->nb_alerts = index;
    free(t);
  }

  g_print_log = save_g_print_log;

}

//
// Display list of checks in the log
//
void checks_display() {
  int c = 0;
  int i;
  for (i = 0; i < g_nb_checks; ++i) {
    struct check_t *chk = &checks[i];
    if (chk->is_valid) {
      ++c;
      my_logf(LL_DEBUG, LP_INDENT, "== CHECK #%i", i);
    } else {
      my_logf(LL_DEBUG, LP_INDENT, "!! check #%i (will be ignored)", i);
    }
    my_logf(LL_DEBUG, LP_INDENT, "   is_valid       = %s", chk->is_valid ? "Yes" : "No");
    my_logf(LL_DEBUG, LP_INDENT, "   display_name   = %s", chk->display_name_set ? chk->display_name : "<unset>");
    my_logf(LL_DEBUG, LP_INDENT, "   host_name      = %s", chk->host_name_set ? chk->host_name : "<unset>");

    if (chk->tcp_port_set)
      my_logf(LL_DEBUG, LP_INDENT, "   port           = %i", chk->tcp_port);
    else
      my_logf(LL_DEBUG, LP_INDENT, "   port           = <unset>");

    my_logf(LL_DEBUG, LP_INDENT, "   expect         = %s", chk->tcp_expect_set ? chk->tcp_expect : "<unset>");
    my_logf(LL_DEBUG, LP_INDENT, "   alerts         = %s", chk->alerts_set ? chk->alerts : "<unset>");
    my_logf(LL_DEBUG, LP_INDENT, "   nb alerts      = %i", chk->nb_alerts);
    int j;
    for (j = 0; j < chk->nb_alerts; ++j) {
      my_logf(LL_DEBUG, LP_INDENT, "     alert:     = #%i -> %s",
        j, alerts[chk->alert_ctrl[j].idx].name);
    }
    if (chk->alert_threshold_set)
      my_logf(LL_DEBUG, LP_INDENT, "   alert_threshold    = %d", chk->alert_threshold);
    else
      my_logf(LL_DEBUG, LP_INDENT, "   alert_threshold    = <unset>");
    if (chk->alert_repeat_every_set)
      my_logf(LL_DEBUG, LP_INDENT, "   alert_repeat_every = %d", chk->alert_repeat_every);
    else
      my_logf(LL_DEBUG, LP_INDENT, "   alert_repeat_every = <unset>");
    if (chk->alert_repeat_max_set)
      my_logf(LL_DEBUG, LP_INDENT, "   alert_repeat_max   = %d", chk->alert_repeat_max);
    else
      my_logf(LL_DEBUG, LP_INDENT, "   alert_repeat_max   = <unset>");
  }
  if (c != g_nb_valid_checks)
    internal_error("main", __FILE__, __LINE__);
}

//
// Display list of alerts in the log
//
void alerts_display() {
  int c = 0;
  int i;
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
    my_logf(LL_DEBUG, LP_INDENT, "   method            = %s",
      (alrt->method_set && alrt->method != FIND_STRING_NOT_FOUND) ? l_alert_methods[alrt->method] : "<unset>");
    if (alrt->threshold_set)
      my_logf(LL_DEBUG, LP_INDENT, "   threshold         = %li", alrt->threshold);
    else
      my_logf(LL_DEBUG, LP_INDENT, "   threshold         = <unset>");
    if (alrt->repeat_every_set)
      my_logf(LL_DEBUG, LP_INDENT, "   repeat_every      = %li", alrt->repeat_every);
    else
      my_logf(LL_DEBUG, LP_INDENT, "   repeat_every      = <unset>");
    if (alrt->repeat_max_set)
      my_logf(LL_DEBUG, LP_INDENT, "   repeat_max        = %li", alrt->repeat_max);
    else
      my_logf(LL_DEBUG, LP_INDENT, "   repeat_max        = <unset>");
    if (alrt->retries_set)
      my_logf(LL_DEBUG, LP_INDENT, "   retries           = %li", alrt->retries);
    else
      my_logf(LL_DEBUG, LP_INDENT, "   retries           = <unset>");
    if (alrt->method == AM_SMTP) {
      my_logf(LL_DEBUG, LP_INDENT, "   SMTP/smart host = %s", alrt->smtp_smarthost_set ? alrt->smtp_smarthost : "<unset>");
      if (alrt->smtp_port_set)
        my_logf(LL_DEBUG, LP_INDENT, "   SMTP/port       = %li", alrt->smtp_port);
      else
        my_logf(LL_DEBUG, LP_INDENT, "   SMTP/port       = <unset>");
      my_logf(LL_DEBUG, LP_INDENT, "   SMTP/self       = %s", alrt->smtp_self_set ? alrt->smtp_self : "<unset>");
      my_logf(LL_DEBUG, LP_INDENT, "   SMTP/sender     = %s", alrt->smtp_sender_set ? alrt->smtp_sender : "<unset>");
      my_logf(LL_DEBUG, LP_INDENT, "   SMTP/recipients = %s", alrt->smtp_recipients_set ? alrt->smtp_recipients : "<unset>");
    } else if (alrt->method == AM_PROGRAM) {
      my_logf(LL_DEBUG, LP_INDENT, "   program/command   = %s", alrt->prg_command_set ? alrt->prg_command : "<unset>");
    } else if (alrt->method == AM_LOG) {
      my_logf(LL_DEBUG, LP_INDENT, "   log/log_file      = %s", alrt->log_file_set ? alrt->log_file : "<unset>");
    }
  }
  if (c != g_nb_valid_alerts)
    internal_error("main", __FILE__, __LINE__);
}

//
// Display config elements in the log
void config_display() {
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

  int i;
  for (i = 0; i < g_nb_checks; ++i) {
    struct check_t *chk = &checks[i];
    if (!chk->is_valid)
      continue;

    char t[BIGSTRSIZE];
    strncpy(t, "", sizeof(t));
    int II;
    for (II = 0; II < chk->nb_alerts; ++II) {
      if (strlen(t) >= 1)
        strncat(t, ", ", sizeof(t));
      strncat(t, alerts[chk->alert_ctrl[II].idx].name, sizeof(t));
    }
    t[sizeof(t) - 1] = '\0';

    my_logf(LL_NORMAL, LP_DATETIME, "To check: '%s' [%s:%i], %s%s%s, %s%s", chk->display_name, chk->host_name, chk->tcp_port,
      chk->tcp_expect_set ? "expect \"" : "no expect", chk->tcp_expect_set ? chk->tcp_expect : "", chk->tcp_expect_set ? "\"" : "",
      chk->alerts_set ? "alerts: " : "no alert", chk->alerts_set ? t : "");
  }
}

int main(int argc, char *argv[]) {

    // rand() function used by get_unique_mime_boundary()
  srand(time(NULL));

  if ((errno = pthread_mutex_init(&mutex, NULL)) != 0) {
    char s_err[SMALLSTRSIZE];
    fatal_error("pthread_mutex_init(): %s", errno_error(s_err, sizeof(s_err)));
  }

  parse_options(argc, argv);

  my_log_open();
  my_logs(LL_NORMAL, LP_NOTHING, "");
  my_logs(LL_NORMAL, LP_DATETIME, PACKAGE_STRING " start");

  read_configuration_file(g_cfg_file);
    // Match alerts as written in the alerts option of checks (checks[]) with
    // defined alerts (alerts[])
  identify_alerts();

  int ii;
  for (ii = 0; ii < g_nb_checks; ++ii) {
    struct check_t *chk = &checks[ii];
    check_t_getready(chk);
  }

  if (strlen(g_test_alert) >= 1) {
    int a = find_alert(g_test_alert);
    if (a < 0) {
      printf("Unknown alert '%s'\n", g_test_alert);
      exit(EXIT_FAILURE);
    }
    char desc[SMALLSTRSIZE];
    snprintf(desc, sizeof(desc), "[TEST] alert for alert %s", alerts[a].name);



    struct alert_ctrl_t alert_ctrl = {a, AS_NOTHING, 0, 0};
    struct tm my_now;
    set_current_tm(&my_now);
    struct tm alert_info;
    time_t ltime = time(NULL) - 30 * 60;
    alert_info = *localtime(&ltime);
    struct exec_alert_t exec_alert = {ST_UNDEF, AS_NOTHING, &alerts[a], &alert_ctrl, 0, &my_now, &alert_info, &alert_info,
      1, "Test alert display name", "Test alert host name", NULL, 0, desc};
    int r = execute_alert(&exec_alert);
    my_logf(LL_NORMAL, LP_DATETIME, "Alert returned code %d", r);

    my_log_close();
    exit(EXIT_SUCCESS);
  }

  checks_display();
  alerts_display();
  config_display();

  web_create_img_files();

    // Just to call WSAStartup, yes!
  os_init_network();

  if (g_webserver_on) {
    pthread_t id;
    if (pthread_create(&id, NULL, webserver, NULL) != 0)
      fatal_error("main(): pthread_create() error");
  }

  atexit(atexit_handler);
  signal(SIGTERM, sigterm_handler);
  signal(SIGABRT, sigabrt_handler);
  signal(SIGINT, sigint_handler);

  almost_neverending_loop();

  if (quitting) {
    return EXIT_FAILURE;
  }
  quitting = TRUE;

  my_logs(LL_VERBOSE, LP_DATETIME, PACKAGE_NAME " end");
  my_log_close();

  return EXIT_SUCCESS;
}

