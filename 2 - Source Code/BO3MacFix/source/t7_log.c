#include "t7_runtime.h"
#include <stdio.h>
#include <stdarg.h>
#include <pthread.h>

#include <stdlib.h>
#include <sys/stat.h>

void bo3_data_path(const char *name, char *out, size_t capacity) {
    const char *dir = getenv("BO3MACFIX_DATA_DIR");
    if (dir && *dir) {
        mkdir(dir, 0755);
        snprintf(out, capacity, "%s/%s", dir, name);
    } else {
        // The classic launcher starts the game in Contents/MacOS.
        snprintf(out, capacity, "../../../BO3MacFix/%s", name);
    }
}

void t7_log(const char *format, ...) {
    static pthread_mutex_t lock = PTHREAD_MUTEX_INITIALIZER;
    pthread_mutex_lock(&lock);
    va_list args, copy;
    va_start(args, format);
    va_copy(copy, args);
    vfprintf(stderr, format, args);
    char path[1024];
    // engine.log in the launcher's data directory, t7-status.log in the
    // classic BO3MacFix folder.
    const char *dir = getenv("BO3MACFIX_DATA_DIR");
    bo3_data_path(dir && *dir ? "engine.log" : "t7-status.log", path, sizeof(path));
    FILE *file = fopen(path, "a");
    if (file) { vfprintf(file, format, copy); fclose(file); }
    va_end(copy);
    va_end(args);
    pthread_mutex_unlock(&lock);
}
