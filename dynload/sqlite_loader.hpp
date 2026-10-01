#pragma once

#include <sqlite3.h>

#define TCC_SQLITE_FUNCS(X)                                                    \
  X(sqlite3_open_v2);                                                          \
  X(sqlite3_close);                                                            \
  X(sqlite3_errmsg);                                                           \
  X(sqlite3_exec);                                                             \
  X(sqlite3_free);                                                             \
  X(sqlite3_busy_timeout);                                                     \
  X(sqlite3_prepare_v2);                                                       \
  X(sqlite3_finalize);                                                         \
  X(sqlite3_reset);                                                            \
  X(sqlite3_step);                                                             \
  X(sqlite3_bind_text);                                                        \
  X(sqlite3_bind_int64);                                                       \
  X(sqlite3_bind_double);                                                      \
  X(sqlite3_column_type);                                                      \
  X(sqlite3_column_int64);                                                     \
  X(sqlite3_column_double);                                                    \
  X(sqlite3_column_text);

struct SqliteLib {
#define X(name) decltype(&::name) name = nullptr;
  TCC_SQLITE_FUNCS(X)
#undef X
  void *handle = nullptr;
};

extern SqliteLib *SQLITE_LIB;

#ifndef TCC_DYNLOAD_SKIP_DEFINES
#define sqlite3_open_v2 SQLITE_LIB->sqlite3_open_v2
#define sqlite3_close SQLITE_LIB->sqlite3_close
#define sqlite3_errmsg SQLITE_LIB->sqlite3_errmsg
#define sqlite3_exec SQLITE_LIB->sqlite3_exec
#define sqlite3_free SQLITE_LIB->sqlite3_free
#define sqlite3_busy_timeout SQLITE_LIB->sqlite3_busy_timeout
#define sqlite3_prepare_v2 SQLITE_LIB->sqlite3_prepare_v2
#define sqlite3_finalize SQLITE_LIB->sqlite3_finalize
#define sqlite3_reset SQLITE_LIB->sqlite3_reset
#define sqlite3_step SQLITE_LIB->sqlite3_step
#define sqlite3_bind_text SQLITE_LIB->sqlite3_bind_text
#define sqlite3_bind_int64 SQLITE_LIB->sqlite3_bind_int64
#define sqlite3_bind_double SQLITE_LIB->sqlite3_bind_double
#define sqlite3_column_type SQLITE_LIB->sqlite3_column_type
#define sqlite3_column_int64 SQLITE_LIB->sqlite3_column_int64
#define sqlite3_column_double SQLITE_LIB->sqlite3_column_double
#define sqlite3_column_text SQLITE_LIB->sqlite3_column_text
#endif
