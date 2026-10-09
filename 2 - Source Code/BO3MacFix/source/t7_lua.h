#ifndef MACFIX_T7_LUA_H
#define MACFIX_T7_LUA_H
#ifdef __cplusplus
extern "C" {
#endif
// Caller owns the returned malloc-compatible string; NULL on invalid input.
char *t7_rewrite_trace(const char *trace, const char *mod_database, const char *stock_database);
#ifdef __cplusplus
}
#endif
#endif
