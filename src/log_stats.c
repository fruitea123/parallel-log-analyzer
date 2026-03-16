#include "log_stats.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void init_result(result_msg_t *out, const char *path) {
    memset(out, 0, sizeof(*out));
    snprintf(out->path, sizeof(out->path), "%s", path);
}

static int fail_result(result_msg_t *out, int errnum) {
    out->line_count = 0;
    out->error_count = 0;
    out->warning_count = 0;
    out->status = -1;
    out->errnum = errnum;
    return -1;
}

int analyze_log_file(const char *path, result_msg_t *out) {
    FILE *file = NULL;
    char *line = NULL;
    size_t capacity = 0;

    init_result(out, path);

    file = fopen(path, "r");
    if (file == NULL) {
        return fail_result(out, errno);
    }

    while (getline(&line, &capacity, file) != -1) {
        out->line_count++;
        if (strstr(line, "ERROR") != NULL) {
            out->error_count++;
        }
        if (strstr(line, "WARNING") != NULL) {
            out->warning_count++;
        }
    }

    if (ferror(file)) {
        int errnum = errno == 0 ? EIO : errno;
        free(line);
        fclose(file);
        return fail_result(out, errnum);
    }

    free(line);

    if (fclose(file) != 0) {
        return fail_result(out, errno);
    }

    out->status = 0;
    out->errnum = 0;
    return 0;
}
