#ifndef _LOGGING_HPP_
#define _LOGGING_HPP_

#include <cstdio>
#include <ctime>

// ── 컴파일 타임 레벨 (빌드 시 -DLOG_LEVEL=n 으로 덮어쓸 수 있음) ──────────
#define LOG_LEVEL_TRACE  0
#define LOG_LEVEL_DEBUG  1
#define LOG_LEVEL_INFO   2
#define LOG_LEVEL_WARN   3
#define LOG_LEVEL_ERROR  4
#define LOG_LEVEL_NONE   5

#ifndef LOG_LEVEL
  #ifdef NDEBUG
    #define LOG_LEVEL LOG_LEVEL_INFO
  #else
    #define LOG_LEVEL LOG_LEVEL_DEBUG
  #endif
#endif

// ── ANSI 컬러 (릴리즈 빌드에서는 꺼짐) ────────────────────────────────────
#ifndef NDEBUG
  #define _LOG_COL_RESET  "\033[0m"
  #define _LOG_COL_TRACE  "\033[90m"   // 회색
  #define _LOG_COL_DEBUG  "\033[36m"   // 청록
  #define _LOG_COL_INFO   "\033[32m"   // 초록
  #define _LOG_COL_WARN   "\033[33m"   // 노랑
  #define _LOG_COL_ERROR  "\033[31m"   // 빨강
#else
  #define _LOG_COL_RESET  ""
  #define _LOG_COL_TRACE  ""
  #define _LOG_COL_DEBUG  ""
  #define _LOG_COL_INFO   ""
  #define _LOG_COL_WARN   ""
  #define _LOG_COL_ERROR  ""
#endif

// ── 런타임 레벨 제어 ───────────────────────────────────────────────────────
namespace logger {
    extern int g_runtime_level;
    void set_level(int level);

    // log_dir: 디렉토리 경로. 빌드 시작 시각 기준으로 CSV 파일을 생성함.
    // ex) set_log_file("logs") → logs/2026-05-09_123456.csv
    void set_log_file(const char *log_dir);

    void _log(int level, const char *color, const char *tag,
              const char *file, int line, const char *func,
              const char *fmt, ...) __attribute__((format(printf, 7, 8)));
}

// ── 매크로 ─────────────────────────────────────────────────────────────────
#if LOG_LEVEL <= LOG_LEVEL_TRACE
  #define LOG_TRACE(fmt, ...) \
    logger::_log(LOG_LEVEL_TRACE, _LOG_COL_TRACE, "TRACE", \
                 __FILE__, __LINE__, __func__, fmt, ##__VA_ARGS__)
#else
  #define LOG_TRACE(fmt, ...) do {} while(0)
#endif

#if LOG_LEVEL <= LOG_LEVEL_DEBUG
  #define LOG_DEBUG(fmt, ...) \
    logger::_log(LOG_LEVEL_DEBUG, _LOG_COL_DEBUG, "DEBUG", \
                 __FILE__, __LINE__, __func__, fmt, ##__VA_ARGS__)
#else
  #define LOG_DEBUG(fmt, ...) do {} while(0)
#endif

#if LOG_LEVEL <= LOG_LEVEL_INFO
  #define LOG_INFO(fmt, ...) \
    logger::_log(LOG_LEVEL_INFO, _LOG_COL_INFO, "INFO ", \
                 __FILE__, __LINE__, __func__, fmt, ##__VA_ARGS__)
#else
  #define LOG_INFO(fmt, ...) do {} while(0)
#endif

#if LOG_LEVEL <= LOG_LEVEL_WARN
  #define LOG_WARN(fmt, ...) \
    logger::_log(LOG_LEVEL_WARN, _LOG_COL_WARN, "WARN ", \
                 __FILE__, __LINE__, __func__, fmt, ##__VA_ARGS__)
#else
  #define LOG_WARN(fmt, ...) do {} while(0)
#endif

#if LOG_LEVEL <= LOG_LEVEL_ERROR
  #define LOG_ERROR(fmt, ...) \
    logger::_log(LOG_LEVEL_ERROR, _LOG_COL_ERROR, "ERROR", \
                 __FILE__, __LINE__, __func__, fmt, ##__VA_ARGS__)
#else
  #define LOG_ERROR(fmt, ...) do {} while(0)
#endif

#endif /* _LOGGING_HPP_ */