// main.c

// Copyright Sébastien Millet, 2013

#include "main.h"
#include "util.h"

#include <stdio.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>
#include <sys/time.h>
#include <signal.h>
#include <getopt.h>
#include <stdint.h>
#include <ctype.h>

/*#define DEBUG_LOOP*/

#define DEFAULT_CHECK_INTERVAL 120
#define DEFAULT_NB_KEEP_LAST_STATUS 15
#define DEFAULT_DISPLAY_NAME_WIDTH 20
#define DEFAULT_ALERT_THRESHOLD 3
#define DEFAULT_ALERT_SMTP_SENDER  (PACKAGE_TARNAME "@localhost")
#define DEFAULT_ALERT_REPEAT_EVERY 30
#define DEFAULT_ALERT_REPEAT_MAX 5
#define DEFAULT_ALERT_RECOVERY TRUE
#define DEFAULT_ALERT_RETRIES 2
#define DEFAULT_ALERT_SMTP_SELF PACKAGE_TARNAME
#define DEFAULT_SMTP_PORT 25
#define DEFAULT_POP3_PORT 110
#define DEFAULT_ALERT_LOG_STRING "${NOW_TIMESTAMP}  ${DESCRIPTION}"
#define DEFAULT_CONNECT_TIMEOUT 5
#define DEFAULT_LOOP_SMTP_SELF  PACKAGE_TARNAME
  // 4 hours during which the status will be "fail" when an email gets lost
#define DEFAULT_LOOP_FAIL_TIMEOUT (60 * 60 * 4)
  // If an email has not "come back" after 10 minutes the loop enters the status "fail"
#define DEFAULT_LOOP_FAIL_DELAY (60 * 10)
#define DEFAULT_LOOP_ID "NMNM"
#define DEFAULT_LOOP_SEND_EVERY  2
#define LOOP_STATUS_WHEN_SENDING_FAILS  ST_UNKNOWN
#define LOOP_TRIGGER_OPTIMIZE_ARRAY 10
#define LOOP_PREFIX     PACKAGE_NAME
#define LOOP_POSTFIX    PACKAGE_NAME
#define LOOP_ARRAY_REALLOC_STEP 60
  // "subject" is the simplest! It could be
  //    "x-" PACKAGE_NAME
  // but x- headers are not guaranteed to be kept along the way...
  // Also message-ID could be used but you then would need to
  // determine the domain name.
#define LOOP_HEADER_REF "subject:"
#define LAST_STATUS_CHANGE_DISPLAY_SECONDS  (60 * 60 * 23)

const char *DEFAULT_LOGFILE = PACKAGE_TARNAME ".log";
const char *DEFAULT_CFGFILE = PACKAGE_TARNAME ".ini";

const char *TERM_CLEAR_SCREEN = "\033[2J\033[1;1H";

#define PREFIX_RECEIVED "<<< "
#define PREFIX_SENT ">>> "

  // Don't update the one below unless you know what you're doing!
  // Linked to test_linux directory scripts
#define TEST2_NB_LOOPS  199
#define TEST3_NB_LOOPS  20

  // As writtn here:
  //   http://nagiosplug.sourceforge.net/developer-guidelines.html#AEN76
enum {_NAGIOS_FIRST = 0, NAGIOS_OK = 0, NAGIOS_WARNING = 1, NAGIOS_CRITICAL = 2, NAGIOS_UNKNOWN = 3, _NAGIOS_LAST = 3};

void web_create_img_files();

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


//
// Web Server
//
extern long int g_html_refresh_interval;
int g_html_refresh_interval_set = FALSE;

extern char g_html_title[SMALLSTRSIZE];
int g_html_title_set = FALSE;

extern long int g_html_nb_columns;
int g_html_nb_columns_set = FALSE;

extern long int g_webserver_on;
int g_webserver_on_set = FALSE;

extern long int g_webserver_port;
int g_webserver_port_set = FALSE;

extern const char *ST_TO_BGCOLOR_FORHTML[];
struct img_file_t img_files[_ST_NBELEMS];

//
// CONFIG
//

int g_laxist = FALSE;

extern loglevel_t g_current_log_level;

extern char g_log_file[SMALLSTRSIZE];

struct check_t checks[2000];
int g_nb_checks = 0;
int g_nb_valid_checks = 0;

struct alert_t alerts[100];
int g_nb_alerts = 0;
int g_nb_valid_alerts = 0;

char g_cfg_file[SMALLSTRSIZE];

char g_test_alert[SMALLSTRSIZE];

long int g_check_interval;
int g_check_interval_set = FALSE;
long int g_nb_keep_last_status = -1;
int g_nb_keep_last_status_set = FALSE;

extern long int g_print_subst_error;
int g_print_subst_error_set = FALSE;

long int g_display_name_width = DEFAULT_DISPLAY_NAME_WIDTH;
int g_display_name_width_set = FALSE;

long int g_buffer_size = DEFAULT_BUFFER_SIZE;
int g_buffer_size_set = FALSE;
long int g_connect_timeout = DEFAULT_CONNECT_TIMEOUT;
int g_connect_timeout_set = FALSE;
int telnet_log = FALSE;

extern int g_print_log;
int g_print_status = FALSE;
int g_test_mode = 0;

extern char g_html_directory[BIGSTRSIZE];
int g_html_directory_set = FALSE;
extern char g_html_file[SMALLSTRSIZE];
int g_html_file_set = FALSE;
char g_html_complete_file_name[BIGSTRSIZE];

#define CFGK_LIST_SEPARATOR ','
#define CFGK_PORT_SEPARATOR ':'
#define CFGK_COMMENT_CHAR ';'

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

enum {CM_UNDEF = FIND_STRING_NOT_FOUND, CM_TCP = 0, CM_PROGRAM = 1, CM_LOOP = 2};
const char *l_check_methods[] = {
  "tcp",      // CM_TCP
  "program",  // CM_PROGRAM
  "loop"      // CM_LOOP
};
int (*check_func[]) (struct check_t *, const struct subst_t *, int) = {
  perform_check_tcp,      // CM_TCP
  perform_check_program,  // CM_PROGRAM
  perform_check_loop      // CM_LOOP
};

enum {ID_YES = 0, ID_NO = 1};
const char *l_yesno[] = {
  "yes",  // ID_YES
  "no"    // ID_NO
};

extern long int g_log_usec;
int g_log_usec_set = FALSE;
long int g_date_format;
int g_date_format_set = FALSE;
extern int g_date_df;
const char *l_date_formats[] = {
  "french", // DF_FENCH
  "english" // DF_ENGLISH
};


//
// All variables found in the ini file
//
struct check_t chk00;
struct alert_t alrt00;

enum {CS_NONE = -1, CS_GENERAL = 0, CS_CHECK = 1, CS_ALERT = 2};
  // Indexed with CS_ constants
const char *l_sections_names[] = {
  "general",  // CS_GENERAL
  "check",    // CS_CHECK
  "alert"     // CS_ALERT
};
struct section_method_mgmt_t l_sections_methods[] = {
  {NULL, NULL, NULL, NULL},                                                                    // CS_GENERAL
  {(const char **)l_check_methods, &chk00.guess_method, &chk00.method, &chk00.method_set},     // CS_CHECK
  {(const char **)l_alert_methods, &alrt00.guess_method, &alrt00.method, &alrt00.method_set}   // CS_ALERT
};

const struct readcfg_var_t readcfg_vars[] = {

// CHECKS

  {"method", V_STRKEY, CS_CHECK, &chk00.method, NULL, NULL, 0, &chk00.method_set, FALSE, l_check_methods,
    sizeof(l_check_methods) / sizeof(*l_check_methods), -1},
  {"display_name", V_STR, CS_CHECK, NULL, &(chk00.display_name), NULL, 0, &(chk00.display_name_set), FALSE, NULL, 0, -1},
  {"host_name", V_STR, CS_CHECK, NULL, &(chk00.host_name), NULL, 0, &(chk00.host_name_set), FALSE, NULL, 0, -1},

// CHCKS -> TCP method

  {"tcp_port", V_INT, CS_CHECK, &(chk00.tcp_port), NULL, NULL, 0, &(chk00.tcp_port_set), FALSE, NULL, 0, CM_TCP},
  {"tcp_connect_timeout", V_INT, CS_CHECK, &(chk00.tcp_connect_timeout), NULL, NULL, 0,
    &(chk00.tcp_connect_timeout_set), FALSE, NULL, 0, CM_TCP},
  {"tcp_expect", V_STR, CS_CHECK, NULL, &(chk00.tcp_expect), NULL, 0, &(chk00.tcp_expect_set), FALSE, NULL, 0, CM_TCP},

// CHECKS -> PROGRAM method

  {"program_command", V_STR, CS_CHECK, NULL, &(chk00.prg_command), NULL, 0, &(chk00.prg_command_set), FALSE, NULL, 0, CM_PROGRAM},

// CHECKS -> LOOP method

  {"loop_id", V_STR, CS_CHECK, NULL, &chk00.loop_id, NULL, 0, &chk00.loop_id_set, TRUE, NULL, 0, CM_LOOP},
  {"loop_fail_delay", V_INT, CS_CHECK, &chk00.loop_fail_delay, NULL, NULL, 0, &chk00.loop_fail_delay_set, FALSE, NULL, 0, CM_LOOP},
  {"loop_fail_timeout", V_INT, CS_CHECK, &chk00.loop_fail_timeout, NULL, NULL, 0,
    &chk00.loop_fail_timeout_set, FALSE, NULL, 0, CM_LOOP},
  {"loop_send_every", V_INT, CS_CHECK, &chk00.loop_send_every, NULL, NULL, 0, &chk00.loop_send_every_set, FALSE, NULL, 0, CM_LOOP},
  {"loop_smtp_smart_host", V_STR, CS_CHECK, NULL, &chk00.loop_smtp.smarthost, NULL, 0,
    &chk00.loop_smtp.smarthost_set, FALSE, NULL, 0, CM_LOOP},
  {"loop_smtp_port", V_INT, CS_CHECK, &chk00.loop_smtp.port, NULL, NULL, 0, &chk00.loop_smtp.port_set, FALSE, NULL, 0, CM_LOOP},
  {"loop_smtp_self", V_STR, CS_CHECK, NULL, &chk00.loop_smtp.self, NULL, 0, &chk00.loop_smtp.self_set, FALSE, NULL, 0, CM_LOOP},
  {"loop_smtp_sender", V_STR, CS_CHECK, NULL, &chk00.loop_smtp.sender, NULL, 0, &chk00.loop_smtp.sender_set, TRUE, NULL, 0, CM_LOOP},
  {"loop_smtp_recipients", V_STR, CS_CHECK, NULL, &chk00.loop_smtp.recipients, NULL, 0,
    &chk00.loop_smtp.recipients_set, FALSE, NULL, 0, CM_LOOP},
  {"loop_smtp_connect_timeout", V_INT, CS_CHECK, &chk00.loop_smtp.connect_timeout, NULL, NULL, 0,
    &chk00.loop_smtp.connect_timeout_set, FALSE, NULL, 0, CM_LOOP},
  {"loop_pop3_server", V_STR, CS_CHECK, NULL, &chk00.loop_pop3.server, NULL, 0, &chk00.loop_pop3.server_set, FALSE, NULL, 0, CM_LOOP},
  {"loop_pop3_port", V_INT, CS_CHECK, &chk00.loop_pop3.port, NULL, NULL, 0, &chk00.loop_pop3.port_set, FALSE, NULL, 0, CM_LOOP},
  {"loop_pop3_user", V_STR, CS_CHECK, NULL, &chk00.loop_pop3.user, NULL, 0, &chk00.loop_pop3.user_set, FALSE, NULL, 0, CM_LOOP},
  {"loop_pop3_password", V_STR, CS_CHECK, NULL, &chk00.loop_pop3.password, NULL, 0,
    &chk00.loop_pop3.password_set, FALSE, NULL, 0, CM_LOOP},
  {"loop_pop3_connect_timeout", V_INT, CS_CHECK, &chk00.loop_pop3.connect_timeout, NULL, NULL, 0,
    &chk00.loop_pop3.connect_timeout_set, FALSE, NULL, 0, CM_LOOP},

// CHECKS -> alerts

  {"alerts", V_STR, CS_CHECK, NULL, &(chk00.alerts), NULL, 0, &(chk00.alerts_set), FALSE, NULL, 0, -1},
  {"alert_threshold", V_INT, CS_CHECK, &(chk00.alert_threshold), NULL, NULL, 0, &(chk00.alert_threshold_set), FALSE, NULL, 0, -1},
  {"alert_repeat_every", V_INT, CS_CHECK, &(chk00.alert_repeat_every), NULL, NULL, 0,
    &(chk00.alert_repeat_every_set), FALSE, NULL, 0, -1},
  {"alert_repeat_max", V_INT, CS_CHECK, &(chk00.alert_repeat_max), NULL, NULL, 0, &(chk00.alert_repeat_max_set), TRUE, NULL, 0, -1},
  {"alert_recovery", V_YESNO, CS_CHECK, &(chk00.alert_recovery), NULL, NULL, 0, &(chk00.alert_recovery_set), FALSE, NULL, 0, -1},

// GENERAL

  {"date_format", V_STRKEY, CS_GENERAL, &g_date_format, NULL, NULL, 0, &g_date_format_set, FALSE, l_date_formats,
    sizeof(l_date_formats) / sizeof(*l_date_formats), -1},
  {"print_subst_error", V_YESNO, CS_GENERAL, &g_print_subst_error, NULL, NULL, 0, &g_print_subst_error_set, FALSE, NULL, 0, -1},
  {"log_usec", V_YESNO, CS_GENERAL, &g_log_usec, NULL, NULL, 0, &g_log_usec_set, FALSE, NULL, 0, -1},
  {"check_interval", V_INT, CS_GENERAL, &g_check_interval, NULL, NULL, 0, &g_check_interval_set, TRUE, NULL, 0, -1},
  {"buffer_size", V_INT, CS_GENERAL, &g_buffer_size, NULL, NULL, 0, &g_buffer_size_set, FALSE, NULL, 0, -1},
  {"connect_timeout", V_INT, CS_GENERAL, &g_connect_timeout, NULL, NULL, 0, &g_connect_timeout_set, FALSE, NULL, 0, -1},
  {"keep_last_status", V_INT, CS_GENERAL, &g_nb_keep_last_status, NULL, NULL, 0, &g_nb_keep_last_status_set, TRUE, NULL, 0, -1},
  {"display_name_width", V_INT, CS_GENERAL, &g_display_name_width, NULL, NULL, 0, &g_display_name_width_set, FALSE, NULL, 0, -1},
  {"html_refresh_interval", V_INT, CS_GENERAL, &g_html_refresh_interval, NULL, NULL, 0,
    &g_html_refresh_interval_set, FALSE, NULL, 0, -1},
  {"html_title", V_STR, CS_GENERAL, NULL, NULL, g_html_title, sizeof(g_html_title), &g_html_title_set, FALSE, NULL, 0, -1},
  {"html_directory", V_STR, CS_GENERAL, NULL, NULL, g_html_directory, sizeof(g_html_directory),
    &g_html_directory_set, FALSE, NULL, 0, -1},
  {"html_file", V_STR, CS_GENERAL, NULL, NULL, g_html_file, sizeof(g_html_file), &g_html_file_set, FALSE, NULL, 0, -1},
  {"html_nb_columns", V_INT, CS_GENERAL, &g_html_nb_columns, NULL, NULL, 0, &g_html_nb_columns_set, FALSE, NULL, 0, -1},
  {"webserver_on", V_YESNO, CS_GENERAL, &g_webserver_on, NULL, NULL, 0, &g_webserver_on_set, FALSE, NULL, 0, -1},
  {"webserver_port", V_INT, CS_GENERAL, &g_webserver_port, NULL, NULL, 0, &g_webserver_port_set, FALSE, NULL, 0, -1},

// ALERTS

  {"name", V_STR, CS_ALERT, NULL, &alrt00.name, NULL, 0, &alrt00.name_set, FALSE, NULL, 0, -1},
  {"method", V_STRKEY, CS_ALERT, &alrt00.method, NULL, NULL, 0, &alrt00.method_set, FALSE, l_alert_methods,
    sizeof(l_alert_methods) / sizeof(*l_alert_methods), -1},
  {"threshold", V_INT, CS_ALERT, &(alrt00.threshold), NULL, NULL, 0, &(alrt00.threshold_set), FALSE, NULL, 0, -1},
  {"repeat_every", V_INT, CS_ALERT, &(alrt00.repeat_every), NULL, NULL, 0, &(alrt00.repeat_every_set), FALSE, NULL, 0, -1},
  {"repeat_max", V_INT, CS_ALERT, &(alrt00.repeat_max), NULL, NULL, 0, &(alrt00.repeat_max_set), TRUE, NULL, 0, -1},
  {"recovery", V_YESNO, CS_ALERT, &(alrt00.recovery), NULL, NULL, 0, &(alrt00.recovery_set), FALSE, NULL, 0, -1},
  {"retries", V_INT, CS_ALERT, &(alrt00.retries), NULL, NULL, 0, &(alrt00.retries_set), TRUE, NULL, 0, -1},

// ALERTS -> SMTP method

  {"smtp_smart_host", V_STR, CS_ALERT, NULL, &alrt00.smtp_env.smarthost, NULL, 0,
    &alrt00.smtp_env.smarthost_set, FALSE, NULL, 0, AM_SMTP},
  {"smtp_port", V_INT, CS_ALERT, &alrt00.smtp_env.port, NULL, NULL, 0, &alrt00.smtp_env.port_set, FALSE, NULL, 0, AM_SMTP},
  {"smtp_self", V_STR, CS_ALERT, NULL, &alrt00.smtp_env.self, NULL, 0, &alrt00.smtp_env.self_set, FALSE, NULL, 0, AM_SMTP},
  {"smtp_sender", V_STR, CS_ALERT, NULL, &alrt00.smtp_env.sender, NULL, 0, &alrt00.smtp_env.sender_set, TRUE, NULL, 0, AM_SMTP},
  {"smtp_recipients", V_STR, CS_ALERT, NULL, &alrt00.smtp_env.recipients, NULL, 0,
    &alrt00.smtp_env.recipients_set, FALSE, NULL, 0, AM_SMTP},
  {"smtp_connect_timeout", V_INT, CS_ALERT, &alrt00.smtp_env.connect_timeout, NULL, NULL, 0,
    &alrt00.smtp_env.connect_timeout_set, FALSE, NULL, 0, AM_SMTP},

// ALERTS -> PROGRAM method

  {"program_command", V_STR, CS_ALERT, NULL, &alrt00.prg_command, NULL, 0, &alrt00.prg_command_set, FALSE, NULL, 0, AM_PROGRAM},

// ALERTS -> LOG method

  {"log_file", V_STR, CS_ALERT, NULL, &alrt00.log_file, NULL, 0, &alrt00.log_file_set, FALSE, NULL, 0, AM_LOG},
  {"log_string", V_STR, CS_ALERT, NULL, &alrt00.log_string, NULL, 0, &alrt00.log_string_set, FALSE, NULL, 0, AM_LOG}
};


//
// GENERAL
//


const char *LE_NAMES[] = {
  "None",     // LE_NONE
  "Sent",     // LE_SENT
  "Received"  // LE_RECEIVED
};

int g_trace_network_traffic;

int flag_interrupted = FALSE;
int quitting = FALSE;

long int loop_count = 0;

pthread_mutex_t mutex;

struct loop_t *loops = NULL;
int first_loop = 0;
int last_loop = -1;
int loops_nb_alloc = 0;


//
// FUNCTIONS
//

void rfc821_enveloppe_t_destroy(struct rfc821_enveloppe_t *s) {
  if (s->smarthost != NULL)
    free(s->smarthost);
  if (s->self != NULL)
    free(s->self);
  if (s->sender != NULL)
    free(s->sender);
  if (s->recipients != NULL)
    free(s->recipients);
}

void pop3_account_t_destroy(struct pop3_account_t *p) {
  if (p->server != NULL)
    free(p->server);
  if (p->user != NULL)
    free(p->user);
  if (p->password != NULL)
    free(p->password);
}

void check_t_destroy(struct check_t *chk) {
  if (chk->display_name != NULL)
    free(chk->display_name);

  if (chk->host_name != NULL)
    free(chk->host_name);
  if (chk->tcp_expect != NULL)
    free(chk->tcp_expect);

  if (chk->prg_command != NULL)
    free(chk->prg_command);
  
  rfc821_enveloppe_t_destroy(&chk->loop_smtp);
  if (chk->loop_id != NULL)
    free(chk->loop_id);
  pop3_account_t_destroy(&chk->loop_pop3);

  if (chk->alerts != NULL)
    free(chk->alerts);
  if (chk->str_prev_status != NULL)
    free(chk->str_prev_status);
  if (chk->alert_ctrl != NULL)
    free(chk->alert_ctrl);
}

void rfc821_enveloppe_t_create(struct rfc821_enveloppe_t *smtp_env) {
  smtp_env->smarthost_set = FALSE;
  smtp_env->smarthost = NULL;

  smtp_env->port_set = FALSE;

  smtp_env->self_set = FALSE;
  smtp_env->self = NULL;

  smtp_env->sender_set = FALSE;
  smtp_env->sender = NULL;

  smtp_env->recipients_set = FALSE;
  smtp_env->recipients = NULL;

  smtp_env->connect_timeout_set = FALSE;
}

void pop3_account_t_create(struct pop3_account_t *p) {
  p->server_set = FALSE;
  p->server = NULL;

  p->port_set = FALSE;

  p->user_set = FALSE;
  p->user = NULL;

  p->password_set = FALSE;
  p->password = NULL;

  p->connect_timeout_set = FALSE;
}

//
// Create a check struct
//
void check_t_create(struct check_t *chk) {
/*  dbg_write("Creating check...\n");*/

  chk->is_valid = FALSE;

  chk->method = CM_UNDEF;
  chk->method_set = FALSE;
  chk->guess_method = -1;

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

  rfc821_enveloppe_t_create(&chk->loop_smtp);
  chk->loop_id = NULL;
  chk->loop_id_set = FALSE;
  pop3_account_t_create(&chk->loop_pop3);
  chk->loop_fail_delay_set = FALSE;
  chk->loop_fail_timeout_set = FALSE;
  chk->loop_send_every_set = FALSE;
  chk->loop_send_countdown = -1;

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
  alrt->guess_method = -1;

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

  rfc821_enveloppe_t_create(&alrt->smtp_env);

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
// Return true if s begins with prefix, false otherwise
// String comparison is case insensitive
//
int s_begins_with(const char *s, const char *begins_with) {
  return (strncasecmp(s, begins_with, strlen(begins_with)) == 0);
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
// Fill a string with current date/time, format is
//    dd/mm hh:mm:ss
//
#define STR_NOW 15
void get_str_now(char *s, size_t s_len, const struct tm *ts) {
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
int establish_connection(const char *host_name, int port, const char *expect, int timeout, int *sock, const char *prefix) {
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
int perform_check_tcp(struct check_t *chk, const struct subst_t *subst, int subst_len) {
UNUSED(subst);
UNUSED(subst_len);

  char prefix[SMALLSTRSIZE];
  snprintf(prefix, sizeof(prefix), "TCP check(%s):", chk->display_name);

  int sock;
  int ec = establish_connection(chk->host_name, chk->tcp_port, chk->tcp_expect_set ? chk->tcp_expect : NULL,
    chk->tcp_connect_timeout_set ? chk->tcp_connect_timeout : g_connect_timeout, &sock, prefix);
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
int perform_check_program(struct check_t *chk, const struct subst_t *subst, int subst_len) {
  char prefix[SMALLSTRSIZE];
  snprintf(prefix, sizeof(prefix), "Program check(%s):", chk->display_name);

  char *s_substitued = dollar_subst_alloc(chk->prg_command, subst, subst_len);
  my_logf(LL_VERBOSE, LP_DATETIME, "%s will execute the command:", prefix);
  my_logs(LL_VERBOSE, LP_INDENT, s_substitued);

  int r1 = system(s_substitued);
  int r2 = os_wexitstatus(r1);
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
// Construct a reference for email loops
//
void build_email_ref(const struct check_t *chk, const time_t time_ref, char *s, const size_t s_len) {
  char r1[7];
  char r2[7];
  snprintf(r1, sizeof(r1), "%06d", rand());
  snprintf(r2, sizeof(r2), "%06d", rand());
  r1[sizeof(r1) - 1] = '\0';
  r2[sizeof(r2) - 1] = '\0';
  snprintf(s, s_len, "%s:%s:%010lu-%s-%s:%s",
    LOOP_PREFIX, chk->loop_id_set ? chk->loop_id : DEFAULT_LOOP_ID, (long unsigned int)time_ref, r1, r2, LOOP_POSTFIX);
}

//
// Split a hostname between the real hostname and the port, in case
// the hostname is in the form
//    hostname:port
// If no ':' is found, just return the hostname and the default port
//
int split_hostname(const char *hostname, const int port, const int port_set, const int default_port, const char *prefix,
    char *h, const size_t h_len, int *p) {
  strncpy(h, hostname, h_len);
  h[h_len - 1] = '\0';
  char *col = strchr(h, CFGK_PORT_SEPARATOR);
  if (col != NULL) {
    *col = '\0';
    char *strport = col + 1;
    strport = trim(strport);
    *p = atoi(strport);
  } else {
    *p = port_set ? port : default_port;
  }
  if (*p < 1) {
    my_logf(LL_ERROR, LP_DATETIME, "%s invalid port number (%s:%i)", prefix, h, *p);
    return -1;
  }
  return 0;
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
// Perform an SMTP transaction up to the DATA command (inclusive)
// Returns ERR_SMTP_* constants
//
int smtp_email_sending_pre(struct rfc821_enveloppe_t *env, const char *prefix, int *sock, char *from_buf, size_t from_buf_len) {
  env->nb_recipients_wanted = -1;
  env->nb_recipients_ok = -1;

  char h[SMALLSTRSIZE];
  int p;

  if (split_hostname(env->smarthost, env->port, env->port_set, DEFAULT_SMTP_PORT, prefix, h, sizeof(h), &p))
    return ERR_SMTP_INVALID_PORT_NUMBER;

  int ec = establish_connection(h, p, "220 ",
    env->connect_timeout_set ? env->connect_timeout_set : g_connect_timeout, sock, prefix);
  if (ec != EC_OK)
    return (ec == EC_RESOLVE_ERROR ? ERR_SMTP_RESOLVE_ERROR : ERR_SMTP_NETIO);

  if (socket_line_sendf(sock, g_trace_network_traffic, "EHLO %s",
      env->self_set ? env->self : DEFAULT_LOOP_SMTP_SELF)) {
    return ERR_SMTP_NETIO;
  }

  char *response = NULL;
  int response_size;
  do {
    if (socket_read_line_alloc(*sock, &response, g_trace_network_traffic, &response_size) < 0) {
      return ERR_SMTP_NETIO;
    }
  } while (s_begins_with(response, "250-"));
  if (!s_begins_with(response, "250 ")) {
    my_logf(LL_ERROR, LP_DATETIME, "%s unexpected answer from server '%s'", prefix, response);
    free(response);
    return ERR_SMTP_BAD_ANSWER_TO_EHLO;
  }
  free(response);
  env->from_orig = env->sender_set ? env->sender : DEFAULT_ALERT_SMTP_SENDER;
  strncpy(from_buf, env->from_orig, from_buf_len);
  from_buf[from_buf_len - 1] = '\0';
  env->from = from_buf;
  env->from = smtp_address(env->from);
  if (socket_round_trip(*sock, "250 ", "MAIL FROM: <%s>", env->from) != SRT_SUCCESS) {
    socket_line_sendf(sock, g_trace_network_traffic, "QUIT");
    my_logf(LL_ERROR, LP_DATETIME, "%s sender not accepted, closing connection", prefix);
    return ERR_SMTP_SENDER_REJECTED;
  }

  env->nb_recipients_wanted = 0;
  env->nb_recipients_ok = 0;
  size_t l = strlen(env->recipients) + 1;
  char *recipients = (char *)malloc(l);
  strncpy(recipients, env->recipients, l);
  char *r = recipients;
  char *next = NULL;
  while (*r != '\0') {
    if ((next = strchr(r, CFGK_LIST_SEPARATOR)) != NULL) {
      *next = '\0';
      ++next;
    }
    r = smtp_address(r);
    if (strlen(r) >= 1) {
      env->nb_recipients_wanted++;
      int res = socket_round_trip(*sock, "250 ", "RCPT TO: <%s>", r);
      if (res == SRT_SUCCESS)
        env->nb_recipients_ok++;
      else if (res != SRT_SUCCESS && res != SRT_UNEXPECTED_ANSWER) {
        free(recipients);
        return ERR_SMTP_NETIO;
      }
    }

    r = (next == NULL ? &r[strlen(r)] : next);
  }
  free(recipients);

  if (env->nb_recipients_ok == 0) {
    my_logf(LL_ERROR, LP_DATETIME, "%s no recipient accepted, closing connection", prefix);
    socket_line_sendf(sock, g_trace_network_traffic, "QUIT");
    return ERR_SMTP_NO_RECIPIENT_ACCEPTED;
  }

  int res;
  if ((res = socket_round_trip(*sock, "354 ", "DATA")) != SRT_SUCCESS) {
    my_logf(LL_ERROR, LP_DATETIME, "%s DATA command not accepted, closing connection", prefix);
    return (res == SRT_SOCKET_ERROR ? ERR_SMTP_NETIO : ERR_SMTP_DATA_COMMAND_REJECTED);
  }

  return ERR_SMTP_OK;
}

//
//
//
int smtp_mail_sending_post(int sock, const char *prefix, char *email_ref, const size_t email_ref_len) {
  if (socket_line_sendf(&sock, g_trace_network_traffic, "") || socket_line_sendf(&sock, g_trace_network_traffic, ".")) {
    return ERR_SMTP_NETIO;
  }

  char *response = NULL;
  int response_size;
  if (socket_read_line_alloc(sock, &response, g_trace_network_traffic, &response_size) < 0) {
    free(response);
    os_closesocket(sock);
    return ERR_SMTP_NETIO;
  }
  if (!s_begins_with(response, "250 ")) {
    free(response);
    my_logf(LL_ERROR, LP_DATETIME, "%s remote end did not confirm email reception", prefix);
    return ERR_SMTP_EMAIL_RECEPTION_NOT_CONFIRMED;
  }
  char *e;

#define QUEUED_AS " queued as "
  size_t l = strlen(response + 1);
  char *tmp = (char *)malloc(l);
  int ii;
  for (ii = 0; response[ii] != '\0'; ++ii) {
    if (isupper(response[ii]))
      tmp[ii] = toupper(response[ii]);
    else
      tmp[ii] = response[ii];
  }
  tmp[ii] = '\0';
  if ((e = strstr(tmp, QUEUED_AS)) != NULL) {
    strncpy(email_ref, response + (e - tmp) + strlen(QUEUED_AS), email_ref_len);
    email_ref[email_ref_len - 1] = '\0';
    my_logf(LL_DEBUG, LP_DATETIME, "%s email received by smart host, ref '%s'", prefix, email_ref);
  }
  free(tmp);
  free(response);

  socket_line_sendf(&sock, g_trace_network_traffic, "QUIT");
  os_closesocket(sock);

  my_logf(LL_DEBUG, LP_DATETIME, "Disconnected");

  return ERR_SMTP_OK;
}

//
// Fill the string with a standard date
//
void get_rfc822_header_format_current_date(char *date, const size_t date_len) {
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
//
//
int smtp_mail_sending_stdheaders(int *sock, const struct rfc821_enveloppe_t *smtp) {
  if (strlen(smtp->from_orig) >= 1) {
    socket_line_sendf(sock, g_trace_network_traffic, "return-path: %s", smtp->from);
    socket_line_sendf(sock, g_trace_network_traffic, "sender: %s", smtp->from);
    socket_line_sendf(sock, g_trace_network_traffic, "from: %s", smtp->from_orig);
  }
  socket_line_sendf(sock, g_trace_network_traffic, "to: %s", smtp->recipients);
  socket_line_sendf(sock, g_trace_network_traffic, "x-mailer: %s", PACKAGE_STRING);
  char date[SMALLSTRSIZE];
  get_rfc822_header_format_current_date(date, sizeof(date));
  return socket_line_sendf(sock, g_trace_network_traffic, "date: %s", date) ? ERR_SMTP_NETIO : ERR_SMTP_OK;
}

//
//
//
int loop_send_email(const struct check_t *chk,
    const struct subst_t *subst, int subst_len, const char *prefix) {
UNUSED(subst);
UNUSED(subst_len);

  my_logf(LL_VERBOSE, LP_DATETIME, "%s sending probe email", prefix);

  int sock;

  int r;

  if (g_test_mode == 0) {
    struct rfc821_enveloppe_t smtp = chk->loop_smtp;
    char from_buf[SMALLSTRSIZE];
    if ((r = smtp_email_sending_pre(&smtp, prefix, &sock, from_buf, sizeof(from_buf))) != ERR_SMTP_OK) {
      os_closesocket(sock);
      return LOOP_STATUS_WHEN_SENDING_FAILS;
    }

// Email headers

    if ((r = smtp_mail_sending_stdheaders(&sock, &smtp)) != ERR_SMTP_OK) {
      os_closesocket(sock);
      return LOOP_STATUS_WHEN_SENDING_FAILS;
    }
  }

  ++last_loop;
  if (last_loop >= loops_nb_alloc) {
    loops_nb_alloc += LOOP_ARRAY_REALLOC_STEP;
    loops = (struct loop_t *)realloc(loops, loops_nb_alloc * sizeof(struct loop_t));
  }

  struct loop_t *loop = &loops[last_loop];
  loop->status = LE_NONE;

  struct tm now;
  time_t ltime = time(NULL);
  now = *localtime(&ltime);

  build_email_ref(chk, ltime, loop->loop_ref, sizeof(loop->loop_ref));

  if (g_test_mode == 0) {
      // Corresponds to LOOP_HEADER_REF
    socket_line_sendf(&sock, g_trace_network_traffic, "subject: %s", loop->loop_ref);

    socket_line_sendf(&sock, g_trace_network_traffic, "MIME-Version: 1.0");
    socket_line_sendf(&sock, g_trace_network_traffic, "Content-Type: text/plain");
    socket_line_sendf(&sock, g_trace_network_traffic, "Content-Transfer-Encoding: 7bit");

// Email body

    socket_line_sendf(&sock, g_trace_network_traffic, "");

    socket_line_sendf(&sock, g_trace_network_traffic, "This is a loop email sent by " PACKAGE_STRING);
    char strnow[STR_NOW];
    get_str_now(strnow, sizeof(strnow), &now);
    socket_line_sendf(&sock, g_trace_network_traffic, "Sent: %s", strnow);
    socket_line_sendf(&sock, g_trace_network_traffic, "Refrence: '%s'", loop->loop_ref);
    socket_line_sendf(&sock, g_trace_network_traffic, "");

  // Email end

    char email_ref[SMALLSTRSIZE];
    if ((r = smtp_mail_sending_post(sock, prefix, email_ref, sizeof(email_ref))) == ERR_SMTP_OK) {
      loop->status = LE_SENT;
      loop->sent_time = ltime;
    }
  } else {
    loop->status = LE_SENT;
    loop->sent_time = ltime;
    r = ERR_SMTP_OK;
  }

  return r == ERR_SMTP_OK ? ST_OK : LOOP_STATUS_WHEN_SENDING_FAILS;
}

//
// Check whether the email ref s2 matches s1, that is, belongs to the same loop
// The criteria is: all characters must match (at the exact same position) EXCEPT
// when they are numbers in which case they can differ.
//
// Example of refs that match:
//    s1 = "netmon:MY_ID_REF:1371148661-136482-116824:netmon"
//    s2 = "netmon:MY_ID_REF:1371148555-181991-141563:netmon"
//
// Example of refs that do not match:
//    s1 = "netmon:MY_ID_REF:1371148555-181991-141563:netmon"
//    s2 = "netmon:OTHER_REF:1371148596-530793-827558:netmon"
//
int does_this_email_belong_to_me(const char *s1, const char *s2) {
  int i;
  for (i = 0; s1[i] != '\0'; ++i) {
    if (s1[i] != s2[i]) {
      if (!isdigit(s1[i]) || !isdigit(s2[i])) {
        return FALSE;
      }
    }
  }
  return s2[i] == '\0';
}

//
//
//
void loop_manage_retrieved_email(const char *reference, const char *prefix) {
  int i;
  for (i = last_loop; i >= first_loop; --i) {
    if (loops[i].status != LE_NONE && !strcmp(reference, loops[i].loop_ref)) {
      if (loops[i].status == LE_RECEIVED) {
        my_logf(LL_WARNING, LP_DATETIME, "%s loop email already retrieved, email loop ref = '%s'", prefix, reference);
      }
      loops[i].received_time = time(NULL);
      long int duration = (signed long int)loops[i].received_time - (signed long int)loops[i].sent_time;
      my_logf(LL_VERBOSE, LP_DATETIME, "%s loop email retrieved, delay = %lis, email loop ref = '%s'",
        prefix, duration, reference);
      loops[i].status = LE_RECEIVED;
      return;
    }
  }
  my_logf(LL_WARNING, LP_DATETIME, "%s loop email found without internal match, email loop ref = '%s'", prefix, reference);
}

//
//
//
int loop_receive_emails(const struct check_t *chk,
    const struct subst_t *subst, int subst_len, const char *prefix) {
UNUSED(subst);
UNUSED(subst_len);

  my_logf(LL_VERBOSE, LP_DATETIME, "%s retrieving probe email(s)", prefix);

  char h[SMALLSTRSIZE];
  int p;
  const struct pop3_account_t *pop3 = &chk->loop_pop3;

  if (g_test_mode != 0)
    return ERR_POP3_OK;

  if (split_hostname(pop3->server, pop3->port, pop3->port_set, DEFAULT_POP3_PORT, prefix, h, sizeof(h), &p))
    return ERR_POP3_INVALID_PORT_NUMBER;

  int sock;
  int r;

  int ec = establish_connection(h, p, "+OK ",
    pop3->connect_timeout_set ? pop3->connect_timeout_set : g_connect_timeout, &sock, prefix);
  if (ec != EC_OK)
    return (ec == EC_RESOLVE_ERROR ? ERR_POP3_RESOLVE_ERROR : ERR_POP3_NETIO);

  if ((r = socket_round_trip(sock, "+OK ", "USER %s", pop3->user)) != SRT_SUCCESS) {
    if (r != SRT_SOCKET_ERROR) {
      socket_line_sendf(&sock, g_trace_network_traffic, "QUIT");
      my_logf(LL_ERROR, LP_DATETIME, "%s user not accepted, closing connection", prefix);
      os_closesocket(sock);
      return ERR_POP3_USER_REJECTED;
    } else {
      return ERR_POP3_NETIO;
    }
  }

  if ((r = socket_round_trip(sock, "+OK ", "PASS %s", pop3->password)) != SRT_SUCCESS) {
    if (r != SRT_SOCKET_ERROR) {
      socket_line_sendf(&sock, g_trace_network_traffic, "QUIT");
      my_logf(LL_ERROR, LP_DATETIME, "%s user not accepted, closing connection", prefix);
      os_closesocket(sock);
      return ERR_POP3_PASSWORD_REJECTED;
    } else {
      return ERR_POP3_NETIO;
    }
  }

  if (socket_line_sendf(&sock, g_trace_network_traffic, "STAT")) {
    return ERR_POP3_NETIO;
  }
  char *response = NULL;
  int response_size;
  if (socket_read_line_alloc(sock, &response, g_trace_network_traffic, &response_size) < 0) {
    free(response);
    return ERR_POP3_NETIO;
  }
  char *space = NULL;
  char *strN = response + 4;
  if (strlen(response) >= 5)
    space = strchr(strN, ' ');
  if (!s_begins_with(response, "+OK ") || space == NULL) {
    my_logf(LL_ERROR, LP_DATETIME, "%s unexpected answer from server '%s'", prefix, response);
    free(response);
    os_closesocket(sock);
    return ERR_POP3_STAT_ERROR;
  }
  *space = '\0';
  char *d = strN;
  while (TRUE) {
    if (isdigit(*d))
      d++;
    else if (*d == '\0')
      break;
    else {
      my_logf(LL_ERROR, LP_DATETIME, "%s unable to analyze answer from server: '%s'", prefix, response);
      free(response);
      os_closesocket(sock);
      return ERR_POP3_STAT_ERROR;
    }
  }
  int N = atoi(strN);

  my_logf(LL_DEBUG, LP_DATETIME, "%s number of emails: %d", prefix, N);

  time_t ltime = time(NULL);
  char refex[LOOP_REF_SIZE];
  build_email_ref(chk, ltime, refex, sizeof(refex));

  int I;
  for (I = 1; I <= N; ++I) {

// 1. Retrieve email headers

    if ((r = socket_round_trip(sock, "+OK ", "TOP %d 0", I)) == SRT_SOCKET_ERROR) {
      free(response);
      return ERR_POP3_NETIO;
    } else if (r == SRT_UNEXPECTED_ANSWER) {
      my_logf(LL_ERROR, LP_DATETIME, "%s unable to analyze email #%d", prefix, I);
      continue;
    }

    char *header_value = NULL;
    do {
      if (socket_read_line_alloc(sock, &response, g_trace_network_traffic, &response_size) < 0) {
        free(response);
        return ERR_POP3_NETIO;
      }

      if (s_begins_with(response, LOOP_HEADER_REF)) {
          // Found the header we are interested in!
        char *hv = response + strlen(LOOP_HEADER_REF);
        hv = trim(hv);
        size_t l = strlen(hv) + 1;
        header_value = (char *)malloc(l);
        strncpy(header_value, hv, l);
        header_value[l - 1] = '\0';
      }
    } while (strcmp(response, "."));

// 2. Check the email loop reference

    if (header_value != NULL && does_this_email_belong_to_me(refex, header_value)) {
      my_logf(LL_DEBUG, LP_DATETIME, "%s email %d of reference '%s' is mine", prefix, I, header_value);
      loop_manage_retrieved_email(header_value, prefix);

      if ((r = socket_round_trip(sock, "+OK ", "DELE %d", I)) == SRT_SOCKET_ERROR) {
        free(header_value);
        free(response);
        return ERR_POP3_NETIO;
      } else if (r == SRT_UNEXPECTED_ANSWER) {
        my_logf(LL_ERROR, LP_DATETIME, "%s cannot delete email %d of reference '%s'", prefix, I, header_value);
      } else if (r == SRT_SUCCESS) {
        my_logf(LL_VERBOSE, LP_DATETIME, "%s deleted email %d of reference '%s'", prefix, I, header_value);
      } else {
        internal_error("loop_receive_emails", __FILE__, __LINE__);
      }
    } else {
      my_logf(LL_DEBUG, LP_DATETIME, "%s Email %d of reference '%s' is not mine, ignoring", prefix, I, header_value);
    }
    if (header_value != NULL)
      free(header_value);
  }

  free(response);

  socket_line_sendf(&sock, g_trace_network_traffic, "QUIT");
  my_logf(LL_DEBUG, LP_DATETIME, "%s closing POP3 connection", prefix);
  os_closesocket(sock);

  return ERR_POP3_OK;
}

//
//
//
int perform_check_loop(struct check_t *chk, const struct subst_t *subst, int subst_len) {
  char prefix[SMALLSTRSIZE];
  snprintf(prefix, sizeof(prefix), "Loop check(%s):", chk->display_name);

// 1. Send & received probe emails

  int r = ST_OK;
#ifdef DEBUG_LOOP
  if (loop_count % 2 == 0) {
    loop_receive_emails(chk, subst, subst_len, prefix);
    r = loop_send_email(chk, subst, subst_len, prefix);
  } else {
#endif

// loop send & receive emails -> production code below

    if (--chk->loop_send_countdown <= 0) {
      r = loop_send_email(chk, subst, subst_len, prefix);
      chk->loop_send_countdown = chk->loop_send_every_set ? chk->loop_send_every : DEFAULT_LOOP_SEND_EVERY;
    } else {
      my_logf(LL_VERBOSE, LP_DATETIME, "%s skipping email sending, countdown = %d", prefix, chk->loop_send_countdown);
    }
    loop_receive_emails(chk, subst, subst_len, prefix);

// loop send & receive emails -> production code above

#ifdef DEBUG_LOOP
  }
#endif

  if (g_test_mode == 3)
    os_sleep(1);

// 2. Remove entries beyond timeout

  time_t ltime = time(NULL);
  signed long int timeout = chk->loop_fail_timeout_set ? chk->loop_fail_timeout : DEFAULT_LOOP_FAIL_TIMEOUT;
  int i;
  for (i = first_loop; i <= last_loop; ++i) {
    if (loops[i].status != LE_NONE) {
      signed long int age =  (signed long int)ltime - (signed long int)loops[i].sent_time;

      dbg_write("age = %li, timeout = %li\n", age, timeout);

      if (age < timeout)
          // Entries are created in chronological order, if one is below timeout,
          // next ones will be, too -> no need to continue
        break;
      if (g_test_mode) {
        my_logf(LL_VERBOSE, LP_DATETIME, "%s removing loop email %d of reference '%s' (run to timeout), age = %li > timeout = %li",
          prefix, i, "netmon:MYLOOP:1371397278-137861-187807:netmon", age, timeout);
      } else {
        my_logf(LL_VERBOSE, LP_DATETIME, "%s removing loop email %d of reference '%s' (run to timeout), age = %li > timeout = %li",
          prefix, i, "netmon:MYLOOP:1371397278-137861-187807:netmon", age, timeout);
      }
      loops[i].status = LE_NONE;
    }
  }

// 3. Cleaning list: first entries that are "received" (email went back)
//    or "none" (timeout) are to be deleted.

  while (first_loop <= last_loop && loops[first_loop].status != LE_SENT)
    ++first_loop;
  if (first_loop > last_loop) {
    dbg_write("No more email in the list, restarting from zero\n");
    first_loop = 0;
    last_loop = -1;
  }

#ifdef DEBUG_LOOP
  dbg_write("------\n");
  for (i = first_loop; i <= last_loop; ++i) {
    char sent[STR_NOW] = "n/a";
    char received[STR_NOW] = "n/a";
    if (loops[i].status == LE_SENT || loops[i].status == LE_RECEIVED) {
      struct tm t_sent = *localtime(&loops[i].sent_time);
      get_str_now(sent, sizeof(sent), &t_sent);
    }
    if (loops[i].status == LE_RECEIVED) {
      struct tm t_received = *localtime(&loops[i].received_time);
      get_str_now(received, sizeof(received), &t_received);
    }
    dbg_write("#%05d - %-10s  %-16s %-16s %s\n", i, LE_NAMES[loops[i].status], sent, received, loops[i].loop_ref);
  }
  dbg_write("------\n");
  dbg_write("first_loop = %d, last_loop = %d, loops_nb_alloc = %d, loops = %lu\n",
    first_loop, last_loop, loops_nb_alloc, (long unsigned int)loops);
  dbg_write("------\n");
#endif

// 4. Optimize memory: when first_loop becomes too big, move elements

  if (first_loop >= LOOP_TRIGGER_OPTIMIZE_ARRAY) {
    for (i = first_loop; i <= last_loop; ++i)
      loops[i - first_loop] = loops[i];
    last_loop -= first_loop;
    first_loop = 0;
  }

// 5. Check whether there are entries beyond "delay" => fail condition
  signed long int delay = chk->loop_fail_delay_set ? chk->loop_fail_delay : DEFAULT_LOOP_FAIL_DELAY;
  for (i = first_loop; i <= last_loop; ++i) {
    if (loops[i].status == LE_SENT) {
      signed long int age =  (signed long int)ltime - (signed long int)loops[i].sent_time;
      if (age < timeout && age >= delay) {
        r = ST_FAIL;
        break;
      }
    }
  }

  return r;
}

//
// Get loop_count in a thread-safe manner
//
long int get_loop_count() {
  my_pthread_mutex_lock(&mutex);
  long int lc = loop_count;
  my_pthread_mutex_unlock(&mutex);
  return lc;
}

//
// Convert loop_count to a string
//
void loop_count_to_str(char *lcstr, size_t lcstr_len) {
  snprintf(lcstr, lcstr_len, "%li", get_loop_count());
}

//
//
//
int perform_check(struct check_t *chk) {
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
// Used by execute_alert_smtp
//
int core_execute_alert_smtp_one_host(const struct exec_alert_t *exec_alert, const char *smart_host, const char *prefix) {
  struct alert_t *alrt = exec_alert->alrt;

  int r;
  int sock;

  struct rfc821_enveloppe_t smtp = alrt->smtp_env;
  smtp.smarthost = (char *)smart_host;
  smtp.smarthost_set = TRUE;
  char from_buf[SMALLSTRSIZE];
  if ((r = smtp_email_sending_pre(&smtp, prefix, &sock, from_buf, sizeof(from_buf))) != ERR_SMTP_OK) {
    os_closesocket(sock);
    return r;
  }

/*  my_logf(LL_DEBUG, LP_DATETIME, "%s will now send email content", prefix);*/

// Email headers

  if ((r = smtp_mail_sending_stdheaders(&sock, &smtp)) != ERR_SMTP_OK) {
    os_closesocket(sock);
    return r;
  }
  socket_line_sendf(&sock, g_trace_network_traffic, "subject: %s", exec_alert->desc);
  socket_line_sendf(&sock, g_trace_network_traffic, "MIME-Version: 1.0");
  char boundary[SMALLSTRSIZE];
  get_unique_mime_boundary(boundary, sizeof(boundary));
  socket_line_sendf(&sock, g_trace_network_traffic, "Content-Type: multipart/alternative; boundary=%s", boundary);

// Email body

  socket_line_sendf(&sock, g_trace_network_traffic, "");

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

// Email end

  char email_ref[SMALLSTRSIZE];
  r = smtp_mail_sending_post(sock, prefix, email_ref, sizeof(email_ref));

  return r;
}

//
// Execute alert when method == AM_SMTP
//
int execute_alert_smtp(const struct exec_alert_t *exec_alert) {
  char prefix[SMALLSTRSIZE];

  size_t l = strlen(exec_alert->alrt->smtp_env.smarthost) + 1;
  char *smart_hosts = (char *)malloc(l);
  strncpy(smart_hosts, exec_alert->alrt->smtp_env.smarthost, l);
  char *h = smart_hosts;
  char *next = NULL;

  int nb_attempts_done = 0;
  int err_smtp = ERR_SMTP_OK + 1;
  int smart_host_number = 0;
  while (*h != '\0' && err_smtp != ERR_SMTP_OK) {
    smart_host_number++;
    if (smart_host_number == 1)
      snprintf(prefix, sizeof(prefix), "SMTP alert(%s):", exec_alert->alrt->name);
    else
      snprintf(prefix, sizeof(prefix), "SMTP alert(%s)[%i]:", exec_alert->alrt->name, smart_host_number);

    if ((next = strchr(h, CFGK_LIST_SEPARATOR)) != NULL) {
      *next = '\0';
      ++next;
    }

    my_logf(LL_DEBUG, LP_DATETIME, "%s will attempt SMTP connection", prefix);

    nb_attempts_done++;
    err_smtp = core_execute_alert_smtp_one_host(exec_alert, h, prefix);

    h = (next == NULL ? &h[strlen(h)] : next);
  }
  free (smart_hosts);

  return nb_attempts_done == 0 ? -1 : err_smtp;
}

//
// Execute alert when method == AM_PROGRAM
//
int execute_alert_program(const struct exec_alert_t *exec_alert) {
  struct alert_t *alrt = exec_alert->alrt;

  char prefix[SMALLSTRSIZE];
  snprintf(prefix, sizeof(prefix), "program alert(%s):", alrt->name);

  char *s_substitued = dollar_subst_alloc(alrt->prg_command, exec_alert->subst, exec_alert->subst_len);
  my_logf(LL_VERBOSE, LP_DATETIME, "%s will execute the command:", prefix);
  my_logs(LL_VERBOSE, LP_INDENT, s_substitued);
  int r1 = system(s_substitued);
  int r2 = os_wexitstatus(r1);
  my_logf(LL_VERBOSE, LP_DATETIME, "%s return code: %i", prefix, r2);
  free(s_substitued);
  return r2;
}

//
// Execute alert when method == AM_LOG
//
int execute_alert_log(const struct exec_alert_t *exec_alert) {
  struct alert_t *alrt = exec_alert->alrt;

  char prefix[SMALLSTRSIZE];
  snprintf(prefix, sizeof(prefix), "log alert(%s):", alrt->name);

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
// After checks, render result
//
void manage_output(const struct tm *now_done, float elapsed) {
  if (g_print_status) {
    const char *LC_PREFIX = "Last check: ";
    char now[STR_NOW];
    get_str_now(now, sizeof(now), now_done);
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
    get_str_now(now, sizeof(now), now_done);
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
  int II;
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
          for (j = 0; j <= _ST_LAST; ++j) {
            if (ST_TO_CHAR[j] == c)
              break;
          }
          if (j > _ST_LAST)
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
}

//
// Main loop
//
void almost_neverending_loop() {

  if (g_test_mode == 3) {
    my_logf(LL_DEBUG, LP_DATETIME, "Test mode 3: waiting to be at the middle of a second elapse to start");
    int wday; int year; int month; int day;
    int hour; int minute; int second; long unsigned int usec;
    long int gmtoff;
    while (usec >= 500000)
      get_datetime_of_day(&wday, &year, &month, &day, &hour, &minute, &second, &usec, &gmtoff);
    while (usec < 500000)
      get_datetime_of_day(&wday, &year, &month, &day, &hour, &minute, &second, &usec, &gmtoff);
  }

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
      if (status < 0 || status > _ST_LAST)
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
      my_logf(LL_NORMAL, LP_DATETIME, "%s -> %s (%i)",
        chk->display_name, ST_TO_LONGSTR_FANCY[chk->status], chk->nb_consecutive_notok);
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
          trigger_alert = (repeat_max < 0 ? TRUE : (chk->trigger_sequence <= repeat_max));
          chk->trigger_sequence++;
        }
      }

/*      my_logf(LL_DEBUG, LP_DATETIME, "as = %d, chk->trigger_sequence = %d", as, chk->trigger_sequence);*/
/*      my_logf(LL_DEBUG, LP_DATETIME, "chk->nb_consecutive_notok = %d", chk->nb_consecutive_notok);*/
/*      my_logf(LL_DEBUG, LP_DATETIME, "trigger_alert = %d", trigger_alert);*/

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

/*          my_logf(LL_DEBUG, LP_DATETIME, "chk trigger sequence = %d, alert trigger sequence = %d",*/
/*            chk->trigger_sequence, chk->alert_ctrl[i].trigger_sequence);*/

          struct exec_alert_t exec_alert = { chk->status, as, alrt, &chk->alert_ctrl[i], lc,
            &my_now, &chk->alert_info, &chk->last_status_change,
            chk->nb_consecutive_notok, chk->display_name, chk->host_name,
            NULL, 0, NULL
          };

          int r = execute_alert(&exec_alert);

          my_logf(LL_DEBUG, LP_DATETIME, "Executed alert, result = %d", r);

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

//
// Display result (terminal and HTML file)
//

    manage_output(&now_done, elapsed);

    my_logf(LL_NORMAL, LP_DATETIME, "Check done in %fs", elapsed);

//
// Sleep before next loop
//

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
      else if (g_test_mode == 3 && lc == TEST3_NB_LOOPS)
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
  printf("     --laxist        Continue if errors are found in the ini file (default: stop)\n");
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
    {"laxist", no_argument, NULL, '0'},
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

      case '0':
        g_laxist = TRUE;
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
void check_t_check(struct check_t *chk, const char *cf, int line_number, int *nb_errors) {
  g_nb_checks++;

  int is_valid = TRUE;

  if (chk->guess_method >= 0 && !chk->method_set) {
    chk->method = chk->guess_method;
    chk->method_set = TRUE;
  }
  if (chk->method_set && chk->method >= 0 && chk->guess_method >= 0 && chk->method != chk->guess_method) {
    my_logf(LL_ERROR, LP_DATETIME, "Configuration file '%s', section of line %i: inconsistent methods across variables",
      cf, line_number);
    is_valid = FALSE;
  }

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
  } else if (chk->method_set && chk->method == CM_PROGRAM) {
    if (!chk->prg_command_set) {
      my_logf(LL_ERROR, LP_DATETIME, "Configuration file '%s', section of line %i: no command defined, discarding check",
        cf, line_number);
      is_valid = FALSE;
    }
  }

  chk->is_valid = is_valid;
  if (!chk->is_valid) {
    (*nb_errors)++;
    return;
  }

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
void alert_t_check(struct alert_t *alrt, const char *cf, int line_number, int *nb_errors) {
  g_nb_alerts++;

  int is_valid = TRUE;

  if (alrt->guess_method >= 0 && !alrt->method_set) {
    alrt->method = alrt->guess_method;
    alrt->method_set = TRUE;
  }
  if (alrt->method_set && alrt->method >= 0 && alrt->guess_method >= 0 && alrt->method != alrt->guess_method) {
    my_logf(LL_ERROR, LP_DATETIME, "Configuration file '%s', section of line %i: inconsistent methods across variables",
      cf, line_number);
    is_valid = FALSE;
  }

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
    if (!alrt->smtp_env.smarthost_set) {
      my_logf(LL_ERROR, LP_DATETIME, "Configuration file '%s', section of line %i: no smart host defined, discarding alert",
        cf, line_number);
      is_valid = FALSE;
    }
    if (!alrt->smtp_env.recipients_set) {
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

  if (alrt->is_valid) {
    ++g_nb_valid_alerts;
  } else {
    (*nb_errors)++;
  }
}

//
// Parse the ini file
//
void read_configuration_file(const char *cf, int *nb_errors) {
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
          (*nb_errors)++;
          my_logf(LL_ERROR, LP_DATETIME, "Configuration file '%s', line %i: syntax error", cf, line_number);
        } else {
          char section_name[SMALLSTRSIZE + 1];
          strncpy(section_name, b + 1, SMALLSTRSIZE);
          section_name[slen] = '\0';

          if (read_status == CS_CHECK) {
            if (section_start_line_number < 1 || cur_check < 0)
              internal_error("read_configuration_file", __FILE__, __LINE__);
            checks[cur_check] = chk00;
            check_t_check(&checks[cur_check], cf, section_start_line_number, nb_errors);
          } else if (read_status == CS_ALERT) {
            if (section_start_line_number < 1 || cur_alert < 0)
              internal_error("read_configuration_file", __FILE__, __LINE__);
            alerts[cur_alert] = alrt00;
            alert_t_check(&alerts[cur_alert], cf, section_start_line_number, nb_errors);
          }

          int sec = find_string(l_sections_names, sizeof(l_sections_names) / sizeof(*l_sections_names), section_name);
          if (sec == CS_GENERAL) {
            read_status = CS_GENERAL;
          } else {
            section_start_line_number = line_number;
            read_status = CS_NONE;

            if (sec == CS_CHECK) {
              ++cur_check;
              if (cur_check >= (signed)(sizeof(checks) / sizeof(*checks))) {
                --cur_check;
                (*nb_errors)++;
                my_logf(LL_ERROR, LP_DATETIME,
                  "Configuration file '%s', line %i: reached max number of checks (%i)",
                  cf, line_number, sizeof(checks) / sizeof(*checks));
              } else {
                read_status = CS_CHECK;
                check_t_create(&chk00);
              }
            } else if (sec == CS_ALERT) {
              ++cur_alert;
              if (cur_alert >= (signed)(sizeof(alerts) / sizeof(*alerts))) {
                --cur_alert;
                (*nb_errors)++;
                my_logf(LL_ERROR, LP_DATETIME,
                  "Configuration file '%s', line %i: reached max number of alerts (%i)",
                  cf, line_number, sizeof(alerts) / sizeof(*alerts));
              } else {
                read_status = CS_ALERT;
                alert_t_create(&alrt00);
              }
            } else {
              (*nb_errors)++;
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
          (*nb_errors)++;
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
            (*nb_errors)++;
            my_logf(LL_ERROR, LP_DATETIME, "Configuration file '%s', line %i: syntax error", cf, line_number);
          } else {
            int i;
            int match = FALSE;
            for (i = 0; (unsigned int)i < sizeof(readcfg_vars) / sizeof(*readcfg_vars); ++i) {
              if (strcasecmp(key, readcfg_vars[i].name) == 0 && read_status == readcfg_vars[i].section) {
                match = TRUE;
                const struct readcfg_var_t const *cfg = &readcfg_vars[i];

                long int n = 0;
                if (cfg->plint_target != NULL && cfg->var_type == V_INT)
                  n = atoi(value);

                if (cfg->plint_target != NULL && strlen(value) == 0) {
                  (*nb_errors)++;
                  my_logf(LL_ERROR, LP_DATETIME, "Configuration file '%s', line %i: empty value not allowed",
                    cf, line_number);
                } else if (cfg->plint_target != NULL && cfg->var_type == V_INT && n == 0 && !cfg->allow_null) {
                  (*nb_errors)++;
                  my_logf(LL_ERROR, LP_DATETIME, "Configuration file '%s', line %i: null value not allowed",
                    cf, line_number);
                } else if ((cfg->p_pchar_target != NULL || cfg->pchar_target != NULL) &&
                              strlen(value) == 0 && !cfg->allow_null) {
                  (*nb_errors)++;
                  my_logf(LL_ERROR, LP_DATETIME, "Configuration file '%s', line %i: empty value not allowed",
                    cf, line_number);
                } else if (*cfg->pint_var_set) {
                  (*nb_errors)++;
                  my_logf(LL_ERROR, LP_DATETIME, "Configuration file '%s', line %i: variable %s already defined",
                    cf, line_number, key);
                } else if (cfg->plint_target != NULL) {

                  if (cfg->var_type == V_INT) {
                      // Variable of type long int
                    *cfg->plint_target = n;
                    *cfg->pint_var_set = TRUE;
                  } else if (cfg->var_type == V_YESNO) {

                      // Variable of type long int with yes/no input
                    int yn = find_string(l_yesno, sizeof(l_yesno) / sizeof(*l_yesno), value);
                    if (yn == FIND_STRING_NOT_FOUND) {
                      (*nb_errors)++;
                      my_logf(LL_ERROR, LP_DATETIME,
                        "Configuration file '%s', line %i: variable %s must be set to yes or no",
                        cf, line_number, key);
                    }
                    *cfg->plint_target = (yn == ID_YES ? TRUE : FALSE);
                    *cfg->pint_var_set = TRUE;

                  } else if (cfg->var_type == V_STRKEY) {

                      // Variable of type long int with a string table key list
                    int idx = find_string(cfg->table, cfg->table_nb_elems, value);
                    if (idx == FIND_STRING_NOT_FOUND) {
                      (*nb_errors)++;
                      my_logf(LL_ERROR, LP_DATETIME,
                        "Configuration file '%s', line %i: unknown value '%s' for variable %s",
                        cf, line_number, value, key);
                    }
                    *cfg->plint_target = idx;
                    *cfg->pint_var_set = TRUE;

                  } else {
                    internal_error("read_configuration_file", __FILE__, __LINE__);
                  }

                } else if (cfg->p_pchar_target != NULL) {

                    // Variable of type string, need to malloc
                  if (*cfg->p_pchar_target != NULL)
                    internal_error("read_configuration_file", __FILE__, __LINE__);
                  size_t nn = strlen(value) + 1;
                  *cfg->p_pchar_target = (char *)malloc(nn);
                  strncpy(*cfg->p_pchar_target, value, nn);
                  *cfg->pint_var_set = TRUE;

                } else if (cfg->pchar_target != NULL) {

                    // Variable of type string, no need to malloc
                  strncpy(cfg->pchar_target, value, cfg->char_target_len);
                  *cfg->pint_var_set = TRUE;

                } else {
                  internal_error("read_configuration_file", __FILE__, __LINE__);
                }

                if (cfg->method != -1) {

                  dbg_write("i = %d, cfg->method = %d\n", i, cfg->method);

                  struct section_method_mgmt_t *m = &l_sections_methods[readcfg_vars[i].section];
                  if (m == NULL)
                    internal_error("read_configuration_file", __FILE__, __LINE__);

                  if (*m->guess_method < 0)
                    *m->guess_method = cfg->method;
                  if (*m->method_set && *m->method >= 0 && (*m->method != cfg->method || *m->method != *m->guess_method)) {
                    (*nb_errors)++;
                    my_logf(LL_ERROR, LP_DATETIME,
                      "Configuration file '%s', line %i: variable %s not compatible with method '%s'",
                      cf, line_number, key, m->names[*m->method]);
                  } else if (*m->guess_method != cfg->method) {
                    (*nb_errors)++;
                    my_logf(LL_ERROR, LP_DATETIME,
                      "Configuration file '%s', line %i: variable %s not compatible with guessed method '%s'",
                      cf, line_number, key, m->names[*m->guess_method]);
                  }
                  if (*m->method_set && *m->method >= 0) {
                    *m->guess_method = *m->method;
                  }
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
                (*nb_errors)++;
                my_logf(LL_ERROR, LP_DATETIME,
                  "Configuration file '%s', line %i: variable %s not allowed in this section",
                  cf, line_number, key);
              } else {
                (*nb_errors)++;
                my_logf(LL_ERROR, LP_DATETIME, "Configuration file '%s', line %i: unknown variable %s",
                  cf, line_number, key);
              }
            }
          }
        }
      break;
    }
  }
  if (cur_check >= 0 && read_status == CS_CHECK) {
    if (section_start_line_number < 0)
      internal_error("read_configuration_file", __FILE__, __LINE__);
    checks[cur_check] = chk00;
    check_t_check(&checks[cur_check], cf, section_start_line_number, nb_errors);
  }

  if (cur_alert >= 0 && read_status == CS_ALERT) {
    if (section_start_line_number < 0)
      internal_error("read_configuration_file", __FILE__, __LINE__);
    alerts[cur_alert] = alrt00;
    alert_t_check(&alerts[cur_alert], cf, section_start_line_number, nb_errors);
  }

  if (g_nb_checks != cur_check + 1)
    internal_error("read_configuration_file", __FILE__, __LINE__);

  if (g_nb_alerts != cur_alert + 1)
    internal_error("read_configuration_file", __FILE__, __LINE__);

  if (line != NULL)
    free(line);

  fclose(FCFG);

  strncpy(g_html_complete_file_name, g_html_directory, sizeof(g_html_complete_file_name));
  fs_concatene(g_html_complete_file_name, g_html_file, sizeof(g_html_complete_file_name));

/*  dbg_write("Output HTML file = %s\n", g_html_complete_file_name);*/

  g_print_log = save_g_print_log;
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
void identify_alerts(int *nb_errors) {
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
        (*nb_errors)++;
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
// Log a long int value
//
void d_i(const char *desc, const int is_set, const long int val) {
  if (is_set)
    my_logf(LL_DEBUG, LP_INDENT, "%s%i", desc, val);
  else
    my_logf(LL_DEBUG, LP_INDENT, "%s<unset>", desc);
}

//
// Log a string value
//
void d_s(const char *desc, const int is_set, const char *val) {
  if (is_set)
    my_logf(LL_DEBUG, LP_INDENT, "%s%s", desc, val);
  else
    my_logf(LL_DEBUG, LP_INDENT, "%s<unset>", desc);
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
    my_logf(LL_DEBUG, LP_INDENT,                  "   is_valid       = %s", chk->is_valid ? "Yes" : "No");
    d_s("   display_name   = ", chk->display_name_set, chk->display_name);
    d_s("   host_name      = ", chk->host_name_set, chk->host_name);
    d_s("   method         = ", chk->method_set,
      chk->method_set && chk->method != FIND_STRING_NOT_FOUND ? l_check_methods[chk->method] : "<unknown>");
    if (chk->method == CM_TCP) {
      d_i("   TCP/port                   = ", chk->tcp_port_set, chk->tcp_port);
      d_s("   TCP/expect                 = ", chk->tcp_expect_set, chk->tcp_expect);
    } else if (chk->method == CM_PROGRAM) {
      d_s("   PROGRAM/command            = ", chk->prg_command_set, chk->prg_command);
    } else if (chk->method == CM_LOOP) {
      d_s("   LOOP/id                    = ", chk->loop_id_set, chk->loop_id);
      d_i("   LOOP/fail delay            = ", chk->loop_fail_delay_set, chk->loop_fail_delay);
      d_i("   LOOP/fail timeout          = ", chk->loop_fail_timeout_set, chk->loop_fail_timeout);
      d_i("   LOOP/send every            = ", chk->loop_send_every_set, chk->loop_send_every);
      d_s("   LOOP/smtp/smarthost        = ", chk->loop_smtp.smarthost_set, chk->loop_smtp.smarthost);
      d_i("   LOOP/smtp/port             = ", chk->loop_smtp.port_set, chk->loop_smtp.port);
      d_s("   LOOP/smtp/self             = ", chk->loop_smtp.self_set, chk->loop_smtp.self);
      d_s("   LOOP/smtp/sender           = ", chk->loop_smtp.sender_set, chk->loop_smtp.sender);
      d_s("   LOOP/smtp/recipients       = ", chk->loop_smtp.recipients_set, chk->loop_smtp.recipients);
      d_i("   LOOP/smtp/connect_timeout  = ", chk->loop_smtp.connect_timeout_set, chk->loop_smtp.connect_timeout);
      d_s("   LOOP/pop3/server           = ", chk->loop_pop3.server_set, chk->loop_pop3.server);
      d_i("   LOOP/pop3/port             = ", chk->loop_pop3.port_set, chk->loop_pop3.port);
      d_s("   LOOP/pop3/user             = ", chk->loop_pop3.user_set, chk->loop_pop3.user);
      d_s("   LOOP/pop3/password         = ", chk->loop_pop3.password_set, "*****");
      d_i("   LOOP/pop3/connect_timeout  = ", chk->loop_pop3.connect_timeout_set, chk->loop_pop3.connect_timeout);
    }

    d_s("   alerts         = ", chk->alerts_set, chk->alerts);
    d_i("   nb alerts      = ", TRUE, chk->nb_alerts);
    int j;
    for (j = 0; j < chk->nb_alerts; ++j) {
      my_logf(LL_DEBUG, LP_INDENT, "     alert:     = #%i -> %s",
        j, alerts[chk->alert_ctrl[j].idx].name);
    }
    d_i("   alert_threshold    = ", chk->alert_threshold_set, chk->alert_threshold);
    d_i("   alert_repeat_every = ", chk->alert_repeat_every_set, chk->alert_repeat_every);
    d_i("   alert_repeat_max   = ", chk->alert_repeat_max_set, chk->alert_repeat_max);
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
    d_s("   name              = ", alrt->name_set, alrt->name);
    d_s("   method            = ", alrt->method_set,
      alrt->method_set && alrt->method != FIND_STRING_NOT_FOUND ? l_alert_methods[alrt->method] : "<unknown>");
    d_i("   threshold         = ", alrt->threshold_set, alrt->threshold);
    d_i("   repeat_every      = ", alrt->repeat_every_set, alrt->repeat_every);
    d_i("   repeat_max        = ", alrt->repeat_max_set, alrt->repeat_max);
    d_i("   retries           = ", alrt->retries_set, alrt->retries);
    if (alrt->method == AM_SMTP) {
      d_s("   SMTP/smart host = ", alrt->smtp_env.smarthost_set, alrt->smtp_env.smarthost);
      d_i("   SMTP/port       = ", alrt->smtp_env.port_set, alrt->smtp_env.port);
      d_s("   SMTP/self       = ", alrt->smtp_env.self_set, alrt->smtp_env.self);
      d_s("   SMTP/sender     = ", alrt->smtp_env.sender_set, alrt->smtp_env.sender);
      d_s("   SMTP/recipients = ", alrt->smtp_env.recipients_set, alrt->smtp_env.recipients);
      d_i("   SMTP/timeout    = ", alrt->smtp_env.connect_timeout_set, alrt->smtp_env.connect_timeout);
    } else if (alrt->method == AM_PROGRAM) {
      d_s("   program/command   = ", alrt->prg_command_set, alrt->prg_command);
    } else if (alrt->method == AM_LOG) {
      d_s("   log/log_file      = ", alrt->log_file_set, alrt->log_file);
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

//
// Test one alert
//
void test_alert() {
  int a = find_alert(g_test_alert);
  if (a < 0) {
    printf("Unknown alert '%s'\n", g_test_alert);
    my_log_close();
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

int main(int argc, char *argv[]) {

  dbg_write("** DEBUG ACTIVATED **\n");

    // rand() function used by get_unique_mime_boundary()
  srand(time(NULL));

  my_pthread_mutex_init(&mutex);
  util_my_pthread_init();

  parse_options(argc, argv);

  my_log_open();
  my_logs(LL_NORMAL, LP_NOTHING, "");
  my_logs(LL_NORMAL, LP_DATETIME, PACKAGE_STRING " start");

  int nb_errors = 0;
  read_configuration_file(g_cfg_file, &nb_errors);
    // Match alerts as written in the alerts option of checks (checks[]) with
    // defined alerts (alerts[])
  identify_alerts(&nb_errors);
  if (nb_errors >=1 && g_laxist) {
    my_logf(LL_WARNING, LP_DATETIME, "%d error(s) in the ini file, continuing", nb_errors);
  } else if (nb_errors >=1 && !g_laxist) {
    fprintf(stderr, "%d error(s) found in the ini file, stopping (use --laxist option to continue)\n", nb_errors);
    exit(EXIT_FAILURE);
  }
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

  int ii;
  for (ii = 0; ii < g_nb_checks; ++ii) {
    struct check_t *chk = &checks[ii];
    check_t_getready(chk);
  }

  if (strlen(g_test_alert) >= 1)
    test_alert();

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

