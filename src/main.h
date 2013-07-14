// main.h

// Copyright Sébastien Millet, 2013

#ifdef HAVE_CONFIG_H
#include "../config.h"
#else
#include "../extracfg.h"
#endif

#include "util.h"

#include <sys/types.h>
#include <time.h>

#define LOOP_REF_SIZE 70

#define MAN_EN      "man-en"
#define FILE_MAN_EN "netmon.html"

void os_set_sock_nonblocking_mode(int sock);

enum {ST_UNDEF = 0, ST_UNKNOWN = 1, ST_OK = 2, ST_FAIL = 3, _ST_LAST = 3, _ST_NBELEMS = 4};
enum {ERR_SMTP_OK = 0, ERR_SMTP_RESOLVE_ERROR, ERR_SMTP_NETIO, ERR_SMTP_BAD_ANSWER_TO_EHLO, ERR_SMTP_SENDER_REJECTED,
  ERR_SMTP_NO_RECIPIENT_ACCEPTED, ERR_SMTP_DATA_COMMAND_REJECTED, ERR_SMTP_EMAIL_RECEPTION_NOT_CONFIRMED,
  ERR_SMTP_INVALID_PORT_NUMBER};
enum {ERR_POP3_OK = 0, ERR_POP3_INVALID_PORT_NUMBER, ERR_POP3_USER_REJECTED, ERR_POP3_PASSWORD_REJECTED, ERR_POP3_NETIO,
  ERR_POP3_STAT_ERROR, ERR_POP3_RESOLVE_ERROR, ERR_POP3_EMAIL_NOT_RETRIEVABLE};

  // Used for HTML page image files (small icons giving the status
  // of an entry)
struct img_file_t {
  const char *file_name;
  const char *var;
  size_t var_len;
};

struct alert_ctrl_t {
  int idx;
  int alert_status;
  int trigger_sequence;
  int nb_failures;
};

struct rfc821_enveloppe_t {
  conn_def_t srv;
  char *self;
  char *sender;
  char *recipients;
  int self_set;
  int sender_set;
  int recipients_set;

  int nb_recipients_wanted;
  int nb_recipients_ok;
  char *from_orig;
  char *from;
};

struct pop3_account_t {
  conn_def_t srv;
  char *user;
  char *password;
  int user_set;
  int password_set;
};

enum {LE_NONE = 0, LE_SENT = 1, LE_RECEIVED = 2};
struct loop_t {
  int status;
  char loop_ref[LOOP_REF_SIZE];
  time_t sent_time;
  time_t received_time;
};

struct check_t {

// 1. Defined at build time

  int is_valid;

  long int method;
  int guess_method;
  char *display_name;
  conn_def_t srv;
  int method_set;
  int display_name_set;

    // CM_TCP method
  char *tcp_expect;
  int tcp_expect_set;

    // CM_PROGRAM method
  char *prg_command;
  int prg_command_set;

    // CM_LOOP method
  char *loop_id;
  int loop_id_set;
  struct rfc821_enveloppe_t loop_smtp;
  struct pop3_account_t loop_pop3;
  long int loop_fail_delay;
  int loop_fail_delay_set;
  long int loop_fail_timeout;
  int loop_fail_timeout_set;
  long int loop_send_every;
  int loop_send_every_set;
  int loop_send_countdown;

    // Common to all methods
  char *alerts;
  long int alert_threshold;
  long int alert_repeat_every;
  long int alert_repeat_max;
  long int alert_recovery;
  int nb_consecutive_notok;
  int nb_alerts;
  struct alert_ctrl_t *alert_ctrl;

  int alerts_set;
  int alert_threshold_set;
  int alert_repeat_every_set;
  int alert_repeat_max_set;
  int alert_recovery_set;

// 2. Updatable

  int status;
  int prev_status;
  int last_status_change_flag;
  struct tm last_status_change;
  struct tm alert_info;

  char *str_prev_status;

  int trigger_sequence;
};

struct alert_t {
  int is_valid;

  char *name;
  long int method;
  int guess_method;

  int name_set;
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
  struct rfc821_enveloppe_t smtp_env;

    // "program" method
  char *prg_command;
  int prg_command_set;

    // "log" method
  char *log_file;
  char *log_string;
  int log_file_set;
  int log_string_set;
};

  // Used to read ini file
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
  int method;
};
  // Used in confunction with readcfg_var_t to read ini file
struct section_method_mgmt_t {
  const char **names;
  int *guess_method;
  long int *method;
  int *method_set;
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

int perform_check_tcp(struct check_t *chk, const struct subst_t *subst, int subst_len);
int perform_check_program(struct check_t *chk, const struct subst_t *subst, int subst_len);
int perform_check_loop(struct check_t *chk, const struct subst_t *subst, int subst_len);

  // From webserver.c
void *webserver(void *p);

