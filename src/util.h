// util.c

// Copyright Sébastien Millet, 2013

//#define DEBUG

#if defined(_WIN32) || defined(_WIN64)
#define MY_WINDOWS
#include <winsock2.h>
#else
#define MY_LINUX
#endif

#include <sys/types.h>
#include <stdio.h>
#include <openssl/ssl.h>

// !!!!!     WARNING      !!!!!
// !!!!! USE WITH CAUTION !!!!!
//
// EACH CALL TO MYMALLOC, MYREALLOC AND MYFREE WILL
// BE SUBJECT (EACH TIME) TO:
//    OPENING OF A LOG FILE
//    WRITING IN THIS LOG FILE
//    CLOSING OF THIS LOG FILE
//
//#define DEBUG_DYNMEM

#ifdef DEBUG_DYNMEM

#ifdef MY_LINUX
#define DEBUG_MALLOC_LOGFILE "/home/sebastien/travail/cpp/seb/netmon/memdbg.log"
#endif
#ifdef MY_WINDOWS
#define DEBUG_MALLOC_LOGFILE "C:\\seb\\netmon\\memdbg.log"
#endif

#define MYMALLOC(a, b)  debug_malloc(a, #b, __FILE__, __LINE__)
#define MYREALLOC(a, b) debug_realloc(a, b, #a, __FILE__, __LINE__)
#define MYFREE(a)       debug_free(a, #a, __FILE__, __LINE__)
#else
#define MYMALLOC(a, b)  malloc(a)
#define MYREALLOC(a, b) realloc(a, b)
#define MYFREE(a)       free(a)
#endif

#ifdef MY_WINDOWS
// WINDOWS
#include <stdint.h>
#else
// NOT WINDOWS
#include <netinet/in.h>
#endif

#define PORT_SEPARATOR ':'

#define assert(b) \
  if (!(b)) \
    fatal_error("assert failed, file %s, line %d", __FILE__, __LINE__);

#define FALSE 0
#define TRUE  1

#define CONNECT_ERROR -1
#ifndef SOCKET_ERROR
#define SOCKET_ERROR -1
#endif
#define GETTIMEOFDAY_ERROR -1

#ifdef MY_WINDOWS
typedef int socklen_t;
#endif

// Maximum size of an input line in the TCP connection
#define MAX_READLINE_SIZE 10000
#define SMALLSTRSIZE  999
#define BIGSTRSIZE    2000

#ifndef MAX_PATH
#define MAX_PATH      (SMALLSTRSIZE)
#endif

#define REGULAR_STR_STRBUFSIZE 2000
#define ERR_STR_BUFSIZE 200

#define MY_GETLINE_INITIAL_ALLOCATE 100
#define MY_GETLINE_MIN_INCREASE     100
#define MY_GETLINE_COEF_INCREASE    1

enum {DF_FRENCH = 0, DF_ENGLISH = 1};
#define DEFAULT_DATE_FORMAT DF_FRENCH

#define DEFAULT_CONNECT_TIMEOUT 5
#define DEFAULT_NETIO_TIMEOUT   10
#define DEFAULT_PRINT_LOG FALSE
#define DEFAULT_LOG_USEC TRUE
#define DEFAULT_PRINT_SUBST_ERROR FALSE
#define SUBST_ERROR_PREFIX  "?"
#define SUBST_ERROR_POSTFIX "?"

#define LOG_AFTER_TIMESTAMP "  "

// Used by find_word function
#define FIND_STRING_NOT_FOUND -1

#ifdef DEBUG
void dbg_write(const char *fmt, ...)
     __attribute__((format(printf, 1, 2)));
#else
#define dbg_write(...)
#endif

#define UNUSED(x) (void)(x)

// Level of log
typedef enum {
    LL_ERROR = 0,
    LL_WARNING = 1,
    LL_NORMAL = 2,
    LL_VERBOSE = 3,
    LL_DEBUG = 4,
    LL_DEBUGTRACE = 5
} loglevel_t;
#define LL_DEFAULT LL_NORMAL

// Type of prefix output in the log
typedef enum {LP_DATETIME, LP_NOTHING, LP_INDENT} logdisp_t;
// Return value of socket-based functions
enum {
    CONNRES_OK,
    CONNRES_NETIO,
    CONNRES_UNEXPECTED_ANSWER,
    CONNRES_RESOLVE_ERROR,
    CONNRES_CONNECTION_ERROR,
    CONNRES_SSL_CONNECTION_ERROR,
    CONNRES_CONNECTION_TIMEOUT,
    CONNRES_INVALID_PORT_NUMBER
};

struct subst_t {
    const char *find;
    const char *replace;
};
char *dollar_subst_alloc(const char *s, const struct subst_t *subst,
                         int n);

// Replaces a simple "int sock" in connection functions, so
// as to allow SSL-based operations.
enum {CONNTYPE_PLAIN = 0, CONNTYPE_SSL = 1};

// Definition of a connection
typedef struct {
    char *server;
    long int port;
    long int crypt;
    long int connect_timeout;
    long int netio_timeout;
    int server_set;
    int port_set;
    int crypt_set;
    int connect_timeout_set;
    int netio_timeout_set;
} conn_def_t;

// Live connection
typedef struct connection connection_t;
typedef struct connection {
    int type;
    int sock;
    SSL *ssl;
    SSL_CTX *ssl_context;
    ssize_t (*sock_read) (connection_t *, void *, const size_t);
    const char *log_prefix_received;
    ssize_t (*sock_write) (connection_t *, void *, const size_t);
    const char *log_prefix_sent;
} connection_t;

struct connection_table_t {
    ssize_t (*sock_read)(connection_t *, void *, const size_t);
    const char *log_prefix_received;
    ssize_t (*sock_write)(connection_t *, void *, const size_t);
    const char *log_prefix_sent;
};

#define STR_LOG_TIMESTAMP 25
void set_log_timestamp(char *s, size_t s_len,
                       int year, int month, int day,
                       int hour, int minute, int second, long int usec);

void my_log_open();
void my_log_close();
int my_is_log_open();
char *trim(char *str);

void fatal_error(const char *format, ...)
     __attribute__((format(printf, 1, 2)));
char *errno_error(char *s, size_t s_len);
void my_logf(const loglevel_t log_level, const logdisp_t log_disp,
             const char *format, ...)
     __attribute__((format(printf, 3, 4)));
void my_logs(const loglevel_t log_level, const logdisp_t log_disp,
             const char *s);

int os_wexitstatus(const int r);
int find_string(const char **table, int n, const char *elem);
ssize_t my_getline(char **lineptr, size_t *n, FILE *stream);
void os_sleep(unsigned int seconds);
void os_usleep(unsigned long int usec);
void fs_concatene(char *dst, const char *src, size_t dst_len);
void set_current_tm(struct tm *ts);
int add_reader_access_right(const char *f);
void get_datetime_of_day(int *wday, int *year, int *month, int *day,
                         int *hour, int *minute, int *second,
                         long int *usec, long int *gmtoff);

char *os_last_err_desc(char *s, const size_t s_bufsize);
char *os_last_err_desc_n(char *s, const size_t s_len,
                         const long unsigned e);
int os_last_network_op_is_in_progress();

FILE *my_fopen(const char *filename, const char *mode,
               const int nb_retries, const unsigned long int usec_delay);

void os_init_network();
int s_begins_with(const char *s, const char *begins_with);
int os_setsock_timeout(int sock, int timeout_in_seconds);

void win_get_exe_file(const char *argv0, char *p, size_t p_len);

void conn_init(connection_t *conn, int type);
void conn_close(connection_t *conn);
int conn_is_closed(connection_t *conn);
int conn_line_sendf(void (*l)(const loglevel_t, const logdisp_t,
                              const char *, ...), connection_t *conn, int trace, const char *fmt, ...)
    __attribute__((format(printf, 4, 5)));
int conn_read_line_alloc(void (*lp)(const loglevel_t, const logdisp_t,
                                    const char *, ...), connection_t *conn, char **out, int trace,
                         size_t *size);
int conn_connect(connection_t *conn, const struct sockaddr_in *server,
                 const int conn_to, const int netio_to, const char *desc,
                 const char *prefix);
int conn_round_trip(void (*l)(const loglevel_t, const logdisp_t,
                              const char *, ...), connection_t *conn, const char *expect, int trace,
                    const char *fmt, ...)
    __attribute__((format(printf, 5, 6)));
int conn_establish_connection(connection_t *conn, const conn_def_t *srv,
                              const int default_port,
                              const char *expect, const char *prefix,
                              const int trace);
ssize_t conn_plain_read(connection_t *conn, void *buf,
                        const size_t buf_len);
ssize_t conn_plain_write(connection_t *conn, void *buf,
                         const size_t buf_len);
ssize_t conn_ssl_read(connection_t *conn, void *buf, const size_t buf_len);
ssize_t conn_ssl_write(connection_t *conn, void *buf,
                       const size_t buf_len);

#ifdef DEBUG_DYNMEM
void *debug_malloc(size_t size, const char *var, const char *source_file,
                   const long int line);
void *debug_realloc(void *ptr, size_t size, const char *var,
                    const char *source_file, const long int line);
void debug_free(void *ptr, const char *var, const char *source_file,
                const long int line);
#endif

void my_log_core_get_dt_str(const logdisp_t log_disp, char *dt,
                            size_t dt_len);

