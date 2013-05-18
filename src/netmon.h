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
char *errno_error(char *s, size_t s_len);

enum {EC_OK, EC_RESOLVE_ERROR, EC_CONNECTION_ERROR, EC_UNEXPECTED_ANSWER};
enum {SRT_SUCCESS, SRT_SOCKET_ERROR, SRT_UNEXPECTED_ANSWER};

struct check_t {
  int is_valid;

  char *display_name;
  char *host_name;
  char *expect;
  long int port;
  long int connect_timeout;

  char *alerts;
  long int alert_threshold;
  long int alert_resend_every;
  int nb_consecutive_notok;
  int nb_alerts;
  int *alerts_idx;

  int display_name_set;
  int host_name_set;
  int expect_set;
  int port_set;
  int connect_timeout_set;
  int alerts_set;
  int alert_threshold_set;
  int alert_resend_every_set;

  int status;
  int prev_status;
    // Format is hh:mm
  char time_last_status_change[6];
    // Format is dd/mm hh:mm
  char datetime_alert_info[12];
  int h_time_last_status_change;
  int m_time_last_status_change;
  char *str_prev_status;
};

enum {AM_UNDEF, AM_SMTP};
struct alert_t {
  int is_valid;

  char *name;
  char *method_name;
  int method;

  int name_set;
  int method_name_set;

  long int alert_threshold;
  long int alert_resend_every;
  int alert_threshold_set;
  int alert_resend_every_set;

    // SMTP method
  char *smtp_smarthost;
  long int smtp_port;
  char *smtp_self;
  char *smtp_sender;
  char *smtp_recipients;
  long int smtp_connect_timeout;
  int smtp_smarthost_set;
  int smtp_port_set;
  int smtp_self_set;
  int smtp_sender_set;
  int smtp_recipients_set;
  int smtp_connect_timeout_set;
};

enum {CS_NONE, CS_GENERAL, CS_TCPPROBE, CS_ALERT};
enum {V_STR, V_INT, V_YESNO};
struct readcfg_var_t {
  const char *name;
  int var_type;
  int section;
  long int *plint_target;
  char **p_pchar_target;
  char *pchar_target;
  size_t char_target_len;
  int *pint_var_set;
  int allow_null;
};

