// util.c

// Copyright Sébastien Millet, 2013

#ifdef HAVE_CONFIG_H
#include "../config.h"
#else
#include "../extracfg.h"
#endif

#include "util.h"

#include <stdarg.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <fcntl.h>
#include <sys/time.h>
#include <time.h>
#include <ctype.h>
#include <unistd.h>
#include <sys/stat.h>

#include <openssl/err.h>

// Call fflush each time a string is written in the log.
// Useful to debug...
#define FLUSH_LOG

const int crypt_ports[] = {443, 465, 585, 993, 995};

long int g_connect_timeout = DEFAULT_CONNECT_TIMEOUT;
long int g_netio_timeout = DEFAULT_NETIO_TIMEOUT;

loglevel_t g_current_log_level = LL_DEFAULT;

char g_log_file[SMALLSTRSIZE];
long int g_print_subst_error = DEFAULT_PRINT_SUBST_ERROR;
int g_date_df = (DEFAULT_DATE_FORMAT == DF_FRENCH);
int g_print_log = DEFAULT_PRINT_LOG;
long int g_log_usec = DEFAULT_LOG_USEC;
FILE *log_fd = NULL;

/*static pthread_mutex_t util_mutex;*/

const struct connection_table_t connection_table[] = {
    {conn_plain_read, "<<< ", conn_plain_write, ">>> "},        // CONNTYPE_PLAIN
    {conn_ssl_read,     "SSL<<< ", conn_ssl_write,   "SSL>>> "} // CONNTYPE_SSL
};

#ifdef MY_WINDOWS

// * ******* *
// * WINDOWS *
// * ******* *

#include <windows.h>

const char FS_SEPARATOR = '\\';

// HAS_TM_GMTOFF is NOT defined under Windows
// and thus the fnuction below is needed.
long int os_gmtoff() {
    TIME_ZONE_INFORMATION TimeZoneInfo;
    GetTimeZoneInformation(&TimeZoneInfo);
    return -(TimeZoneInfo.Bias + TimeZoneInfo.DaylightBias) * 60;
}

void os_usleep(unsigned long int usec) {
    Sleep(usec);
}

void os_sleep(unsigned int seconds) {
    os_usleep(seconds * 1000L);
}

static void os_set_sock_nonblocking_mode(int sock) {
    u_long iMode = 1;
    int iResult = ioctlsocket(sock, FIONBIO, &iMode);
    if (iResult != NO_ERROR)
        fatal_error("ioctlsocket failed with error: %ld", iResult);
}

static void os_set_sock_blocking_mode(int sock) {
    u_long iMode = 0;
    int iResult = ioctlsocket(sock, FIONBIO, &iMode);
    if (iResult != NO_ERROR)
        fatal_error("ioctlsocket failed with error: %ld", iResult);
}

char *os_last_err_desc_n(char *s, const size_t s_len,
                         const long unsigned e) {
    LPVOID lpMsgBuf;
    FormatMessage(FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM |
                  FORMAT_MESSAGE_IGNORE_INSERTS,
                  NULL, e, MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT), (LPTSTR)&lpMsgBuf, 0,
                  NULL);
    char error[ERR_STR_BUFSIZE];
    strncpy(error, (char *)lpMsgBuf, sizeof(error));
    int n = strlen(error);
    if (n >= 2) {
        if (error[n - 2] == '\r' && error[n - 1] == '\n')
            error[n - 2] = '\0';
    }
    snprintf(s, s_len, "code=%lu (%s)", e, error);
    /*  WSACleanup();*/
    return s;
}

char *os_last_err_desc(char *s, const size_t s_len) {
    return os_last_err_desc_n(s, s_len, WSAGetLastError());
}

void os_init_network() {
    WSADATA wsaData;
    int e;
    char s_err[ERR_STR_BUFSIZE];
    if ((e = WSAStartup(MAKEWORD(2, 2), &wsaData)) != 0) {
        fatal_error("WSAStartup() returned value %i, error: %s", e,
                    os_last_err_desc(s_err, sizeof(s_err)));
        WSACleanup();
    }
}

int os_last_network_op_is_in_progress() {
    return (WSAGetLastError() == WSAEINPROGRESS
            || WSAGetLastError() == WSAEWOULDBLOCK);
}

static void os_closesocket(int sock) {
    closesocket(sock);
}

int os_wexitstatus(const int r) {
    return r;
}

int add_reader_access_right(const char *f) {
    UNUSED(f);

    return 0;
}

//
// Puts in p the complete file name of the current executable
//
void win_get_exe_file(const char *argv0, char *p, size_t p_len) {
    if (!GetModuleFileName(NULL, p, p_len)) {
        strncpy(p, argv0, p_len);
        p[p_len - 1] = '\0';
    }
}

#define strcasecmp _stricmp
#define strncasecmp _strnicmp

#else

// * *********** *
// * NOT WINDOWS *
// * *********** *

#include <sys/wait.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netdb.h>

#define HAS_TM_GMTOFF
// Because HAS_TM_GMTOFF is defined, the fnuction
// os_gmtoff is not needed.

const char FS_SEPARATOR = '/';

void os_usleep(unsigned long int usec) {
    usleep((useconds_t)usec);
}

void os_sleep(unsigned int seconds) {
    sleep(seconds);
}

static void os_set_sock_nonblocking_mode(int sock) {
    long arg = fcntl(sock, F_GETFL, NULL);
    arg |= O_NONBLOCK;
    fcntl(sock, F_SETFL, arg);
}

static void os_set_sock_blocking_mode(int sock) {
    long arg = fcntl(sock, F_GETFL, NULL);
    arg &= ~O_NONBLOCK;
    fcntl(sock, F_SETFL, arg);
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

static void os_closesocket(int sock) {
    close(sock);
}

int os_wexitstatus(const int r) {
    return WEXITSTATUS(r);
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
        my_logf(LL_ERROR, LP_DATETIME, "Unable to change mode of file '%s': ", f,
                s_err);
    }
    return -1;
}

void win_get_exe_file(const char *argv0, char *p, size_t p_len) {
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
#endif

#ifdef DEBUG_DYNMEM

//
// Debug calls to malloc...
// Linked to DEBUG_DYNMEM macro definition
//
void *debug_malloc(size_t size, const char *var, const char *source_file,
                   const long int line) {
    FILE *F = fopen(DEBUG_MALLOC_LOGFILE, "a");
    if (F != NULL) {
        char dt[REGULAR_STR_STRBUFSIZE];
        my_log_core_get_dt_str(LP_DATETIME, dt, sizeof(dt));
        fprintf(F, "%s\tMALLOC.\t%s\t%lu\t%s:%li\n", dt, var,
                (long unsigned int)size, source_file, line);
        fclose(F);
    }
    return malloc(size);
}

//
// Debug calls to realloc...
// Linked to DEBUG_DYNMEM macro definition
//
void *debug_realloc(void *ptr, size_t size, const char *var,
                    const char *source_file, const long int line) {
    FILE *F = fopen(DEBUG_MALLOC_LOGFILE, "a");
    if (F != NULL) {
        char dt[REGULAR_STR_STRBUFSIZE];
        my_log_core_get_dt_str(LP_DATETIME, dt, sizeof(dt));
        fprintf(F, "%s\tREALLOC\t%s\t%lu\t%s:%li\n", dt, var,
                (long unsigned int)size, source_file, line);
        fclose(F);
    }
    return realloc(ptr, size);
}

//
// Debug calls to free...
// Linked to DEBUG_DYNMEM macro definition
//
void debug_free(void *ptr, const char *var, const char *source_file,
                const long int line) {
    FILE *F = fopen(DEBUG_MALLOC_LOGFILE, "a");
    if (F != NULL) {
        char dt[REGULAR_STR_STRBUFSIZE];
        my_log_core_get_dt_str(LP_DATETIME, dt, sizeof(dt));
        fprintf(F, "%s\tFREE...\t%s\t\t%s:%li\n", dt, var, source_file, line);
        fclose(F);
    }
    free(ptr);
}

#endif  // DEBUG_DYNMEM

FILE *my_fopen(const char *filename, const char *mode,
               const int nb_retries, const unsigned long int usec_delay) {
    int retry;
    FILE *f = NULL;
    for (retry = 1; retry <= nb_retries; ++retry, os_usleep(usec_delay)) {
        if ((f = fopen(filename, mode)) != NULL)
            break;
    }
    return f;
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
    if (*lineptr == NULL || *n == 0) {
        *n = MY_GETLINE_INITIAL_ALLOCATE;
        *lineptr = (char *)MYMALLOC(*n, lineptr);
        if (*lineptr == NULL) {
            errno = ENOMEM;
            return -1;
        }
    }
    char *write_head = *lineptr;
    ssize_t char_read = 0;
    int c;
    while (1) {

        // Check there's enough memory to store read characters
        if ((ssize_t)*n - char_read <= 2) {
            size_t increase = *n * MY_GETLINE_COEF_INCREASE;
            if (increase < MY_GETLINE_MIN_INCREASE)
                increase = MY_GETLINE_MIN_INCREASE;
            (*n) += increase;

            char *old_lineptr = *lineptr;
            *lineptr = (char *)MYREALLOC(*lineptr, *n);
            write_head += (*lineptr - old_lineptr);

            if (*lineptr == NULL) {
                errno = ENOMEM;
                return -1;
            }
        }

        // Now read one character from stream
        c = getc(stream);

        /*      dbg_write("Char: %i (%c) [*n = %lu]\n", c, c, *n);*/

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

        *write_head++ = (char)c;
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
// Finds a word in a table, return index found or -1 if not found.
// Case insensitive search
//
int find_string(const char **table, int n, const char *elem) {
    int i;
    for (i = 0; i < n; ++i) {
        if (strcasecmp(table[i], elem) == 0)
            return i;
    }
    return FIND_STRING_NOT_FOUND;
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

    char fatal[REGULAR_STR_STRBUFSIZE];
    vsnprintf(fatal, sizeof(fatal), format, args);

    if (my_is_log_open()) {
        my_logf(LL_ERROR, LP_DATETIME, "FATAL: %s", fatal);
        my_logs(LL_NORMAL, LP_DATETIME, PACKAGE_NAME " aborted");
    }

    strncat(fatal, "\n", sizeof(fatal) - 1);
    fprintf(stderr, fatal, NULL);
    va_end(args);
    exit(EXIT_FAILURE);
}

//
// Initializes the program log
//
void my_log_open() {
    if (strlen(g_log_file) >= 1)
        log_fd = my_fopen(g_log_file, "a", 1, 0);
    else
        log_fd = NULL;
}

//
// Closes the program log
//
void my_log_close() {
    if (log_fd != NULL)
        fclose(log_fd);
}

//
// Is log open?
//
int my_is_log_open() {
    return (log_fd != NULL);
}

//
// Do string substitution.
// The subst_t array can be NULL (meaning, no substitution to do), in that case
// n MUST be set to 0.
//

/*extern pthread_mutex_t mutex;*/

char *dollar_subst_alloc(const char *s, const struct subst_t *subst,
                         int n) {
    size_t sc_len = strlen(s) + 1;
    char *sc = (char *)MYMALLOC(sc_len, sc);
    strncpy(sc, s, sc_len);

    char var[SMALLSTRSIZE];
    char var2[SMALLSTRSIZE];

    char *p = sc;
    for (; *p != '\0'; ++p) {
        if (*p == '$' && *(p + 1) == '{') {
            char *c = p + 2;
            for (; *c != '}' && *c != '\0'; ++c)
                ;
            int l = (int)(c - p - 2);
            if (*c == '}' && l >= 1) {
                *c = '\0';
                strncpy(var, p + 2, sizeof(var));
                var[sizeof(var) - 1] = '\0';

                /*              dbg_write("Found variable '%s'\n", var);*/

                int i;

                if (g_print_subst_error) {
                    strncpy(var2, SUBST_ERROR_PREFIX, sizeof(var2) - 1);
                    strncat(var2, var, sizeof(var2) - 1);
                    strncat(var2, SUBST_ERROR_POSTFIX, sizeof(var2) - 1);
                } else {
                    strncpy(var2, "", sizeof(var2) - 1);
                }

                char *rep = var2;
                for (i = 0; i < n; ++i) {
                    if (strcasecmp(subst[i].find, var) == 0) {
                        rep = (char *)subst[i].replace;
                        break;
                    }
                }

                /*              dbg_write("=== before: '%s'\n", sc);*/

                size_t buf_len = strlen(c + 1) + 1;
                char *buf = (char *)MYMALLOC(buf_len, buf);
                strncpy(buf, c + 1, buf_len);
                *p = '\0';
                size_t need_len = strlen(sc) + strlen(rep) + strlen(buf) + 1;
                if (need_len > sc_len) {
                    char *new_sc = (char *)MYREALLOC(sc, need_len);

                    /*                    dbg_write("Reallocated from %lu to %lu\n", sc_len, need_len);*/

                    sc_len = need_len;
                    p += (new_sc - sc);
                    sc = new_sc;
                }
                strncat(sc, rep, sc_len);
                strncat(sc, buf, sc_len);
                MYFREE(buf);

                /*              dbg_write("=== after:  '%s'\n", sc);*/
            }
        }
    }

    return sc;
}

//
// Get date/time of day
//
void get_datetime_of_day(int *wday, int *year, int *month, int *day,
                         int *hour, int *minute, int *second,
                         long int *usec, long int *gmtoff) {
    time_t ltime = time(NULL);
    struct tm ts;
    ts = *localtime(&ltime);

    struct timeval tv;
    struct timezone tz;
    if (gettimeofday(&tv, &tz) == GETTIMEOFDAY_ERROR) {
        char s_err[ERR_STR_BUFSIZE];
        fatal_error("gettimeofday() error, %s", os_last_err_desc(s_err,
                    sizeof(s_err)));
    }

    *wday = ts.tm_wday;
    *year = ts.tm_year + 1900;
    *month = ts.tm_mon + 1;
    *day = ts.tm_mday;
    *hour = ts.tm_hour;
    *minute = ts.tm_min;
    *second = ts.tm_sec;
    *usec = tv.tv_usec;

#ifdef HAS_TM_GMTOFF
    *gmtoff = ts.tm_gmtoff;
#else
    *gmtoff = os_gmtoff();
#endif
}

//
// Remplit la structure avec les date/heure actuelles
void set_current_tm(struct tm *ts) {
    time_t ltime = time(NULL);
    *ts = *localtime(&ltime);
}

//
//
//
void set_log_timestamp(char *s, size_t s_len,
                       int year, int month, int day, int hour, int minute, int second,
                       long int usec) {
    if (g_log_usec && usec >= 0) {
        snprintf(s, s_len, "%02i/%02i/%02i %02i:%02i:%02i.%06lu",
                 g_date_df ? day : month, g_date_df ? month : day,
                 year % 100, hour, minute, second, usec);
    } else {
        snprintf(s, s_len, "%02i/%02i/%02i %02i:%02i:%02i",
                 g_date_df ? day : month, g_date_df ? month : day,
                 year % 100, hour, minute, second);
    }
    s[s_len - 1] = '\0';
}

//
// Prepare prefix string, used by my_log only
//
void my_log_core_get_dt_str(const logdisp_t log_disp, char *dt,
                            size_t dt_len) {
    int wday;
    int year;
    int month;
    int day;
    int hour;
    int minute;
    int second;
    long int usec;
    long int gmtoff;
    get_datetime_of_day(&wday, &year, &month, &day, &hour, &minute, &second,
                        &usec, &gmtoff);

    set_log_timestamp(dt, dt_len, year, month, day, hour, minute, second,
                      usec);
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
    /*  my_pthread_mutex_lock(&util_mutex);*/

    if (log_fd) {
        fputs(s, log_fd);
        fputs("\n", log_fd);

#ifdef FLUSH_LOG
        fflush(log_fd);
#endif

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

#ifdef FLUSH_LOG
        fflush(stdout);
#endif

    }

    /*  my_pthread_mutex_unlock(&util_mutex);*/
}

//
// Output a string in the program log
//
void my_logs(const loglevel_t log_level, const logdisp_t log_disp,
             const char *s) {
    if (log_level > g_current_log_level)
        return;

    char dt[REGULAR_STR_STRBUFSIZE];

    my_log_core_get_dt_str(log_disp, dt, sizeof(dt));
    strncat(dt, LOG_AFTER_TIMESTAMP, sizeof(dt) - 1);
    size_t dt_len = strlen(dt);
    strncat(dt, s, sizeof(dt) - 1);
    my_log_core_output(dt, dt_len);
}

//
// Output a formatted string in the program log
//
void my_logf(const loglevel_t log_level, const logdisp_t log_disp,
             const char *format, ...) {

    if (log_level > g_current_log_level)
        return;

    char dt[REGULAR_STR_STRBUFSIZE];
    char log_string[REGULAR_STR_STRBUFSIZE];
    my_log_core_get_dt_str(log_disp, dt, sizeof(dt));
    va_list args;
    va_start(args, format);
    vsnprintf(log_string, sizeof(log_string), format, args);
    va_end(args);
    strncat(dt, LOG_AFTER_TIMESTAMP, sizeof(dt) - 1);
    size_t dt_len = strlen(dt);
    strncat(dt, log_string, sizeof(dt) - 1);
    my_log_core_output(dt, dt_len);
}

//
// Return true if s begins with prefix, false otherwise
// String comparison is case insensitive
//
int s_begins_with(const char *s, const char *begins_with) {
    return (strncasecmp(s, begins_with, strlen(begins_with)) == 0);
}

//
// Sets the string passed as argument to the last SSL error
//
char *ssl_get_error(const unsigned long e, char *s, const size_t s_len) {
    ERR_error_string_n(e, s, s_len);
    return s;
}

//
// Initializes the connection_t object
//
void conn_init(connection_t *conn, int type) {
    conn->type = type;
    conn->sock = -1;
    conn->ssl = NULL;
    conn->ssl_context = NULL;

    conn->sock_read = connection_table[conn->type].sock_read;
    conn->log_prefix_received =
        connection_table[conn->type].log_prefix_received;
    conn->sock_write = connection_table[conn->type].sock_write;
    conn->log_prefix_sent = connection_table[conn->type].log_prefix_sent;
}

//
// Closes the connection
//
void conn_close(connection_t *conn) {
    os_closesocket(conn->sock);
    if (conn->ssl != NULL) {
        SSL_shutdown(conn->ssl);
        SSL_free(conn->ssl);
        conn->ssl = NULL;
    }
    if (conn->ssl_context != NULL) {
        SSL_CTX_free(conn->ssl_context);
        conn->ssl_context = NULL;
    }
    conn->sock = -1;
}

//
// Check whether a given connection_t object is closed
//
int conn_is_closed(connection_t *conn) {
    if (conn->sock != -1 || conn->ssl_context != NULL || conn->ssl != NULL)
        return FALSE;
    return TRUE;
}

//
// Connect to a remote host, with a timeout
// Return EC_* code.
//
int conn_connect(connection_t *conn, const struct sockaddr_in *server,
                 const int conn_to, const int netio_to, const char *desc,
                 const char *prefix) {
    fd_set fdset;
    FD_ZERO(&fdset);
    FD_SET((unsigned int)(conn->sock), &fdset);

    os_set_sock_nonblocking_mode(conn->sock);

    char s_err[ERR_STR_BUFSIZE];

    struct timeval conn_tv;
    conn_tv.tv_sec = conn_to;
    conn_tv.tv_usec = 0;

    int cr = CONNRES_OK;
    if (connect(conn->sock, (struct sockaddr *)server,
                sizeof(*server)) == CONNECT_ERROR) {
        if (os_last_network_op_is_in_progress()) {
            if (select((conn->sock) + 1, NULL, &fdset, NULL, &conn_tv) <= 0) {
                my_logf(LL_ERROR, LP_DATETIME, "%s timeout connecting to %s, %s",
                        prefix, desc, os_last_err_desc(s_err, sizeof(s_err)));
                cr = CONNRES_CONNECTION_TIMEOUT;
            } else {
                char so_error;
                socklen_t len = sizeof(so_error);
                getsockopt(conn->sock, SOL_SOCKET, SO_ERROR, &so_error, &len);
                if (so_error != 0) {
                    my_logf(LL_ERROR, LP_DATETIME,
                            "%s network error connecting to %s, code=%i (%s)",
                            prefix, desc, so_error, strerror(so_error));
                }
                cr = so_error != 0 ? CONNRES_NETIO : CONNRES_OK;
            }
        } else {
            my_logf(LL_ERROR, LP_DATETIME, "%s error connecting to %s, %s",
                    prefix, desc, os_last_err_desc(s_err, sizeof(s_err)));
            cr = CONNRES_CONNECTION_ERROR;
        }
    } else {
        my_logf(LL_ERROR, LP_DATETIME, "%s unknown error connecting to %s",
                prefix, desc);
        cr = CONNRES_CONNECTION_ERROR;
    }

    if (cr != CONNRES_OK) {
        conn_close(conn);
        return cr;
    }

    os_set_sock_blocking_mode(conn->sock);

    struct timeval netio_tv;
    netio_tv.tv_sec = netio_to;
    netio_tv.tv_usec = 0;
    if (setsockopt(conn->sock, SOL_SOCKET, SO_RCVTIMEO, (char *)&netio_tv,
                   sizeof(netio_tv))) {
        my_logf(LL_ERROR, LP_DATETIME, "%s unable to set timeout to network I/O",
                prefix);
    }

    if (conn->type == CONNTYPE_PLAIN)
        return CONNRES_OK;

    // Assume we are a v2 or v3 client
    if ((conn->ssl_context = SSL_CTX_new(SSLv23_client_method())) == NULL) {
        my_logf(LL_ERROR, LP_DATETIME, "%s SSL error: %d (%s)",
                prefix, ERR_get_error(), ssl_get_error(ERR_get_error(), s_err,
                        sizeof(s_err)));
        cr = CONNRES_SSL_CONNECTION_ERROR;
    }

    /* Create SSL connection */
    else if ((conn->ssl = SSL_new(conn->ssl_context)) == NULL) {
        my_logf(LL_ERROR, LP_DATETIME, "%s SSL error: %d (%s)",
                prefix, ERR_get_error(), ssl_get_error(ERR_get_error(), s_err,
                        sizeof(s_err)));
        cr =    CONNRES_SSL_CONNECTION_ERROR;
    }

    /* Connect the SSL struct to our connection */
    else if (!SSL_set_fd(conn->ssl, conn->sock)) {
        my_logf(LL_ERROR, LP_DATETIME, "%s SSL error: %d (%s)",
                prefix, ERR_get_error(), ssl_get_error(ERR_get_error(), s_err,
                        sizeof(s_err)));
        cr = CONNRES_SSL_CONNECTION_ERROR;
    }

    /* Initiate SSL handshake */
    else if (SSL_connect(conn->ssl) != 1) {
        my_logf(LL_ERROR, LP_DATETIME, "%s SSL error: %d (%s)",
                prefix, ERR_get_error(), ssl_get_error(ERR_get_error(), s_err,
                        sizeof(s_err)));
        cr = CONNRES_SSL_CONNECTION_ERROR;
    }

    if (cr == CONNRES_OK) {
        my_logf(LL_DEBUG, LP_DATETIME, "%s SSL: handshake [%s] successful", prefix,
                desc);
        os_set_sock_blocking_mode(conn->sock);
    } else {
        conn_close(conn);
    }

    return cr;
}

//
// Split a hostname between the real hostname and the port, in case
// the hostname is in the form
//      hostname:port
// If no ':' is found, just return the hostname and the default port
//
static int split_hostname(const char *hostname, const int port_set,
                          const int port, const int default_port,
                          const char *prefix, char *h, const size_t h_len,
                          int *p) {
    strncpy(h, hostname, h_len);
    h[h_len - 1] = '\0';
    char *col = strchr(h, PORT_SEPARATOR);
    if (col != NULL) {
        *col = '\0';
        char *strport = col + 1;
        strport = trim(strport);
        *p = atoi(strport);
    } else {
        *p = port_set ? port : default_port;
    }
    if (*p < 1) {
        my_logf(LL_ERROR, LP_DATETIME, "%s invalid port number (%s:%i)",
                prefix, h, *p);
        return -1;
    }
    return 0;
}

//
// Guess if SSL is to be used given the port number
//
int guess_conntype(const long int p, const int crypt_set,
                   const int crypt) {
    if (!crypt_set || crypt == FIND_STRING_NOT_FOUND) {
        int i;
        for (i = 0; i < (signed int)(sizeof(crypt_ports) / sizeof(*crypt_ports));
                ++i) {
            if (p == crypt_ports[i])
                return CONNTYPE_SSL;
        }
        return CONNTYPE_PLAIN;
    } else {
        return crypt;
    }
}

//
// Establish a connection, including all what it takes ->
//      Host name resolution
//      TCP connection open
//      Check server answer
//
int conn_establish_connection(connection_t *conn, const conn_def_t *srv,
                              const int default_port, const char *expect,
                              const char *prefix, const int trace) {

    char h[SMALLSTRSIZE];
    int p;

    if (split_hostname(srv->server, srv->port_set, (int)srv->port,
                       default_port, prefix, h, sizeof(h), &p)) {
        return CONNRES_INVALID_PORT_NUMBER;
    }

    conn_init(conn, guess_conntype(p, srv->crypt_set, (int)srv->crypt));

    my_logf(LL_DEBUG, LP_DATETIME, "%s connecting to %s:%i...", prefix, h, p);

    char server_desc[SMALLSTRSIZE + 100];
    char s_err[ERR_STR_BUFSIZE];

    snprintf(server_desc, sizeof(server_desc), "%s:%i", h, p);

    // Resolving server name
    struct sockaddr_in server;
    struct hostent *hostinfo = NULL;
    my_logf(LL_DEBUG, LP_DATETIME, "Running gethosbyname() on %s", h);
    hostinfo = gethostbyname(h);
    if (hostinfo == NULL) {
        my_logf(LL_ERROR, LP_DATETIME, "Unknown host %s, %s", h,
                os_last_err_desc(s_err, sizeof(s_err)));
        return CONNRES_RESOLVE_ERROR;
    }

    int ret = CONNRES_CONNECTION_ERROR;

    if ((conn->sock = socket(AF_INET, SOCK_STREAM,
                             IPPROTO_TCP)) == SOCKET_ERROR) {
        fatal_error("%s socket() error to create connection socket, %s", prefix,
                    os_last_err_desc(s_err, sizeof(s_err)));
    }
    server.sin_family = AF_INET;
    server.sin_port = htons((uint16_t)p);
    server.sin_addr = *(struct in_addr *)hostinfo->h_addr;
    // tv value is undefined after call to connect() as per documentation, so
    // it is to be re-set every time.
    int conn_to = (int)(srv->connect_timeout_set ? srv->connect_timeout :
                        g_connect_timeout);
    int netio_to = (int)(srv->netio_timeout_set ? srv->netio_timeout :
                         g_netio_timeout);

    my_logf(LL_DEBUG, LP_DATETIME,
            "%s will connect to %s:%i, connect timeout = %d, netio timeout = %d",
            prefix, h, p, conn_to, netio_to);

    if ((ret = conn_connect(conn, &server, conn_to, netio_to, server_desc,
                            prefix)) == CONNRES_OK) {
        my_logf(LL_DEBUG, LP_DATETIME, "%s connected to %s", prefix, server_desc);

        if (expect != NULL && strlen(expect) >= 1) {
            char *response = NULL;
            size_t response_size;
            if (conn_read_line_alloc(conn, &response, trace, &response_size) < 0) {
                ;
            } else if (s_begins_with(response, expect)) {
                my_logf(LL_VERBOSE, LP_DATETIME,
                        "%s received expected answer: '%s' (expected '%s')",
                        prefix, response, expect);
                ret = CONNRES_OK;
            } else {
                my_logf(LL_ERROR, LP_DATETIME,
                        "%s received unexpected answer: '%s' (expected '%s')",
                        prefix, response, expect);
                ret = CONNRES_UNEXPECTED_ANSWER;
            }
            MYFREE(response);
        } else {
            ret = CONNRES_OK;
        }

    }

    return ret;
}

//
// Receives a line from a socket (terminated by \015\010)
// Return -1 if an error occured, 1 if reading is successful,
// 0 if transmission is closed.
//
int conn_read_line_alloc(connection_t *conn, char **out, int trace,
                         size_t *size) {
    const int INITIAL_READLINE_BUFFER_SIZE = 100;

    int i = 0;
    int cr = FALSE;
    char ch;
    ssize_t nb;

    if (*out == NULL) {
        *size = (size_t)INITIAL_READLINE_BUFFER_SIZE;
        *out = (char *)MYMALLOC(*size, out);
    }

    for (;;) {
        if ((nb = conn->sock_read(conn, &ch, 1)) == SOCKET_ERROR) {
            char s_err[ERR_STR_BUFSIZE];
            my_logf(LL_ERROR, LP_DATETIME, "Error reading socket, error %s",
                    os_last_err_desc(s_err, sizeof(s_err)));
            conn_close(conn);
            return -1;
        }

        if ((unsigned)i >= *size) {
            if (*size * 2 <= MAX_READLINE_SIZE) {
                *size *= 2;
                *out = (char *)MYREALLOC(*out, *size);
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
            my_logf(LL_DEBUGTRACE, LP_DATETIME, "%s%s", conn->log_prefix_received,
                    *out);
        }
        return 1;
    }
}

//
// Send a line to a socket
// Return 0 if OK, -1 if error.
// Manage logging an error code and closing socket.
//
int conn_line_sendf(connection_t *conn, int trace, const char *fmt, ...) {

    if (conn->sock == -1) {
        return -1;
    }

    size_t l = (long int)(strlen(fmt) + 100);
    char *to_send = (char *)MYMALLOC(l + 1, to_send);

    va_list args;
    va_start(args, fmt);
    vsnprintf(to_send, l, fmt, args);
    va_end(args);

    if (trace)
        my_logf(LL_DEBUGTRACE, LP_DATETIME, "%s%s", conn->log_prefix_sent,
                to_send);

    strncat(to_send, "\015\012", l);
    to_send[l] = '\0';

    ssize_t e = conn->sock_write(conn, to_send, strlen(to_send));

    MYFREE(to_send);

    if (e == SOCKET_ERROR) {
        char s_err[ERR_STR_BUFSIZE];
        my_logf(LL_ERROR, LP_DATETIME, "Network I/O error: %s",
                os_last_err_desc(s_err, sizeof(s_err)));
        conn_close(conn);
        return -1;
    }

    return 0;
}

//
// Send a string to a socket and chck answer (telnet-style communication)
//
int conn_round_trip(connection_t *conn, const char *expect, int trace,
                    const char *fmt, ...) {
    size_t l = strlen(fmt) + 100;
    char *formatted_str = (char *)MYMALLOC(l + 1, formatted_str);

    va_list args;
    va_start(args, fmt);
    vsnprintf(formatted_str, l, fmt, args);
    va_end(args);

    int e = conn_line_sendf(conn, trace, formatted_str);
    MYFREE(formatted_str);
    if (e)
        return CONNRES_NETIO;

    char *response = NULL;
    size_t response_size;
    if (conn_read_line_alloc(conn, &response, trace, &response_size) < 0) {
        MYFREE(response);
        return CONNRES_NETIO;
    }

    if (s_begins_with(response, expect)) {
        MYFREE(response);
        return CONNRES_OK;
    }

    MYFREE(response);
    return CONNRES_UNEXPECTED_ANSWER;
}

//
// CONNTYPE_PLAIN -> read sock
//
ssize_t conn_plain_read(connection_t *conn, void *buf,
                        const size_t buf_len) {
    return recv(conn->sock, buf, buf_len, 0);
}

//
// CONNTYPE_PLAIN -> write sock
//
ssize_t conn_plain_write(connection_t *conn, void *buf,
                         const size_t buf_len) {
    return send(conn->sock, buf, buf_len, 0);
}

//
// CONNTYPE_SSL -> read sock
//
ssize_t conn_ssl_read(connection_t *conn, void *buf,
                      const size_t buf_len) {
    return SSL_read(conn->ssl, buf, (int)buf_len);
}

//
// CONNTYPE_SSL -> write sock
//
ssize_t conn_ssl_write(connection_t *conn, void *buf,
                       const size_t buf_len) {
    return SSL_write(conn->ssl, buf, (int)buf_len);
}

