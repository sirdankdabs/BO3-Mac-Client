#include "t7_runtime.h"
#include <stdio.h>
#include <stdarg.h>
#include <pthread.h>

void t7_log(const char *format, ...) {
    static pthread_mutex_t lock = PTHREAD_MUTEX_INITIALIZER;
    pthread_mutex_lock(&lock);
    va_list args, copy;
    va_start(args, format);
    va_copy(copy, args);
    vfprintf(stderr, format, args);
    // The existing launcher starts the game in Contents/MacOS.
    FILE *file = fopen("../../../BO3MacFix/t7-status.log", "a");
    if (file) { vfprintf(file, format, copy); fclose(file); }
    va_end(copy);
    va_end(args);
    pthread_mutex_unlock(&lock);
}
