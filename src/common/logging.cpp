#include "logging.hpp"

#include <cstdarg>
#include <cstdio>
#include <ctime>
#include <sys/stat.h>

namespace logger {

int   g_runtime_level = LOG_LEVEL;
FILE *g_csv_file      = nullptr;

void set_level(int level) {
    g_runtime_level = level;
}

void set_log_file(const char *log_dir) {
    mkdir(log_dir, 0755);

    time_t now = time(nullptr);
    struct tm *t = localtime(&now);
    char path[256];
    snprintf(path, sizeof(path), "%s/%04d-%02d-%02d_%02d%02d%02d.csv",
             log_dir,
             t->tm_year + 1900, t->tm_mon + 1, t->tm_mday,
             t->tm_hour, t->tm_min, t->tm_sec);

    g_csv_file = fopen(path, "w");
    if (g_csv_file)
        fprintf(g_csv_file, "timestamp,level,file,line,func,message\n");
}

void _log(int level, const char *color, const char *tag,
          const char *file, int line, const char *func,
          const char *fmt, ...) {
    if (level < g_runtime_level)
        return;

    struct timespec ts;
    clock_gettime(CLOCK_REALTIME, &ts);
    struct tm *tm_info = localtime(&ts.tv_sec);
    char timebuf[24];
    strftime(timebuf, sizeof(timebuf), "%Y-%m-%d %H:%M:%S", tm_info);
    int ms = static_cast<int>(ts.tv_nsec / 1000000);

    const char *basename = file;
    for (const char *p = file; *p; ++p)
        if (*p == '/') basename = p + 1;

    // stderr 출력
    fprintf(stderr, "%s[%s.%03d] [%s] %s:%d (%s) ",
            color, timebuf + 11, ms, tag, basename, line, func);
    va_list args;
    va_start(args, fmt);
    vfprintf(stderr, fmt, args);
    va_end(args);
    fprintf(stderr, "%s\n", _LOG_COL_RESET);

    // CSV 출력
    if (g_csv_file) {
        char msgbuf[1024];
        va_list args2;
        va_start(args2, fmt);
        vsnprintf(msgbuf, sizeof(msgbuf), fmt, args2);
        va_end(args2);

        // 메시지 안의 큰따옴표 이스케이프 (CSV 규격)
        fprintf(g_csv_file, "%s.%03d,%s,%s,%d,%s,\"",
                timebuf, ms, tag, basename, line, func);
        for (char *p = msgbuf; *p; ++p) {
            if (*p == '"') fputc('"', g_csv_file);
            fputc(*p, g_csv_file);
        }
        fprintf(g_csv_file, "\"\n");
        fflush(g_csv_file);
    }
}

} // namespace logger