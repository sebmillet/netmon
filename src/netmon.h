// netmon.h

// Copyright Sébastien Millet, 2013

#ifdef HAVE_CONFIG_H
#include "../config.h"
#else
#include "../extracfg.h"
#endif

#include <sys/types.h>
#include <time.h>

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

struct subst_t {
  const char *find;
  const char *replace;
};

enum {EC_OK, EC_RESOLVE_ERROR, EC_CONNECTION_ERROR, EC_UNEXPECTED_ANSWER};
enum {SRT_SUCCESS, SRT_SOCKET_ERROR, SRT_UNEXPECTED_ANSWER};
enum {ERR_SMTP_OK = 0, ERR_SMTP_RESOLVE_ERROR, ERR_SMTP_NETIO, ERR_SMTP_BAD_ANSWER_TO_EHLO, ERR_SMTP_SENDER_REJECTED, ERR_SMTP_NO_RECIPIENT_ACCEPTED,
  ERR_SMTP_DATA_COMMAND_REJECTED};

struct alert_ctrl_t {
  int idx;
  int alert_status;
  int trigger_sequence;
  int nb_failures;
};

struct check_t {
  int is_valid;

  char *display_name;
  char *host_name;
  char *expect;
  long int port;
  long int connect_timeout;

  char *alerts;
  long int alert_threshold;
  long int alert_repeat_every;
  long int alert_repeat_max;
  long int alert_recovery;
  int nb_consecutive_notok;
  int nb_alerts;
  struct alert_ctrl_t *alert_ctrl;

  int display_name_set;
  int host_name_set;
  int expect_set;
  int port_set;
  int connect_timeout_set;
  int alerts_set;
  int alert_threshold_set;
  int alert_repeat_every_set;
  int alert_repeat_max_set;
  int alert_recovery_set;

  int status;
  int prev_status;
    // Format is hh:mm
//  char time_last_status_change[6];
    // Format is dd/mm hh:mm
//  char datetime_alert_info[12];
  int last_status_change_flag;
  struct tm last_status_change;
  struct tm alert_info;
//  int h_time_last_status_change;
//  int m_time_last_status_change;

  char *str_prev_status;

  int trigger_sequence;
};

struct alert_t {
  int is_valid;

  char *name;
//  char *method_name;
  long int method;

  int name_set;
//  int method_name_set;
  int method_set;

  long int threshold;
  long int repeat_every;
  long int repeat_max;
  long int recovery;
  long int retries;
  int threshold_set;
  int repeat_every_set;
  int repeat_max_set;
  int recovery_set;
  int retries_set;

    // "smtp" method
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

    // "program" method
  char *prg_command;
  int prg_command_set;

    // "log" method
  char *log_file;
  char *log_string;
  int log_file_set;
  int log_string_set;
};

enum {CS_NONE, CS_GENERAL, CS_TCPPROBE, CS_ALERT};
enum {V_STR, V_INT, V_YESNO, V_STRKEY};
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
  const char **table;
  int table_nb_elems;
};

struct exec_alert_t {
  int status;
  int alert_status;
  struct alert_t *alrt;
  struct alert_ctrl_t *alert_ctrl;
  int loop_count;
  struct tm *my_now;

    // Most often, taken from the check_t that raised the alert.
    // I do not put a struct check_t * here as sometimes
    // there is associated check_t with alert.
  struct tm *alert_info;
  struct tm *last_status_change;
  int nb_consecutive_notok;
  char *display_name;
  char *host_name;

  struct subst_t *subst;
  int subst_len;
  char *desc;
};

int execute_alert_smtp(const struct exec_alert_t *exec_alert);
int execute_alert_program(const struct exec_alert_t *exec_alert);
int execute_alert_log(const struct exec_alert_t *exec_alert);

