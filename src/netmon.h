// netmon.h

// Copyright Sébastien Millet, 2013

#ifdef HAVE_CONFIG_H
#include "../config.h"
#else
#include "../extracfg.h"
#endif

#include <sys/types.h>

#ifndef SOCKET_ERROR
#define SOCKET_ERROR -1
#endif

#if defined(_WIN32) || defined(_WIN64)
typedef int socklen_t;
#endif

#define FALSE 0
#define TRUE  1

#define DEFAULT_CONNECT_TIMEOUT 5

#define REGULAR_STR_STRBUFSIZE 2000
#define ERR_STR_BUFSIZE 200
#define DEFAULT_BUFFER_SIZE 10000

  // Maximum size of an input line in the TCP connection
#define MAX_READLINE_SIZE 10000

#define BIND_ERROR -1
#define LISTEN_ERROR -1
#define ACCEPT_ERROR -1
#define CONNECT_ERROR -1
#define SELECT_ERROR -1
#define RECV_ERROR -1
#define SEND_ERROR -1
#define SETSOCKOPT_ERROR -1
#define GETTIMEOFDAY_ERROR -1

  // Level of log
typedef enum {LL_ERROR = -1, LL_WARNING = 0, LL_NORMAL = 1, LL_VERBOSE = 2, LL_DEBUG = 3} loglevel_t;
  // Type of prefix output in the log
typedef enum {LP_DATETIME, LP_NOTHING, LP_INDENT} logdisp_t;

void os_set_sock_nonblocking_mode(int sock);
void os_set_sock_blocking_mode(int sock);
int os_last_err();
char *os_last_err_desc(char *s, size_t s_bufsize);
void os_init_network();
int os_last_network_op_is_in_progress();
void os_closesocket(int sock);

void fatal_error(const char *format, ...);
void my_logf(const loglevel_t log_level, const logdisp_t log_disp, const char *format, ...);

#define STRNOW_SHORT_WIDTH 5
struct check_t {
  int is_valid;

  char *display_name;
  char *host_name;
  char *expect;
  long int port;
  int alert;

  int display_name_set;
  int host_name_set;
  int expect_set;
  int port_set;

  int status;
  int prev_status;
  char time_last_status_change[STRNOW_SHORT_WIDTH + 1];
  int h_time_last_status_change;
  int m_time_last_status_change;
  char *str_prev_status;
};

