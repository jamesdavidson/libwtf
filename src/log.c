#include "log.h"

#ifdef WTF_ENABLE_LOGGING
    #include <stdarg.h>
    #include <stdio.h>

void wtf_log_internal(wtf_context* ctx, wtf_log_level_t level, const char* component,
                      const char* file, int line, const char* format, ...)
{
    if (!ctx || !ctx->log_callback) {
        return;
    }

    va_list args;
    va_start(args, format);

    char message[1024];
    vsnprintf(message, sizeof(message), format, args);

    ctx->log_callback(level, component, file, line, message, ctx->log_user_context);

    va_end(args);
}

#endif  // WTF_ENABLE_LOGGING
