// util.c

// Copyright Sébastien Millet, 2013

#include <sys/types.h>

#define FALSE 0
#define TRUE  1

#define SMALLSTRSIZE  200
#define BIGSTRSIZE    1000

#define REGULAR_STR_STRBUFSIZE 2000
#define ERR_STR_BUFSIZE 200

#define MY_GETLINE_INITIAL_ALLOCATE 100
#define MY_GETLINE_MIN_INCREASE     100
#define MY_GETLINE_COEF_INCREASE    2

#define GETTIMEOFDAY_ERROR -1

enum {DF_FRENCH = 0, DF_ENGLISH = 1};
#define DEFAULT_DATE_FORMAT DF_FRENCH

#define DEFAULT_PRINT_LOG FALSE
#define DEFAULT_LOG_USEC TRUE
#define DEFAULT_PRINT_SUBST_ERROR FALSE
#define SUBST_ERROR_PREFIX  "?"
#define SUBST_ERROR_POSTFIX "?"

#define LOG_AFTER_TIMESTAMP "  "

#ifdef DEBUG
void dbg_write(const char *fmt, ...);
#else
#define dbg_write(...)
#endif

  // Level of log
typedef enum {LL_ERROR = -1, LL_WARNING = 0, LL_NORMAL = 1, LL_VERBOSE = 2, LL_DEBUG = 3} loglevel_t;
  // Type of prefix output in the log
typedef enum {LP_DATETIME, LP_NOTHING, LP_INDENT} logdisp_t;

struct subst_t {
  const char *find;
  const char *replace;
};
char *dollar_subst_alloc(const char *s, const struct subst_t *subst, int n);

#define STR_LOG_TIMESTAMP 25
void set_log_timestamp(char *s, size_t s_len,
                       int year, int month, int day, int hour, int minute, int second, long int usec);

char *trim(char *str);

void fatal_error(const char *format, ...);
char *errno_error(char *s, size_t s_len);
void my_logf(const loglevel_t log_level, const logdisp_t log_disp, const char *format, ...);
void my_logs(const loglevel_t log_level, const logdisp_t log_disp, const char *s);

void my_pthread_mutex_lock(pthread_mutex_t *m);
void my_pthread_mutex_unlock(pthread_mutex_t *m);
void my_pthread_mutex_init(pthread_mutex_t *m);
void util_my_pthread_init();

