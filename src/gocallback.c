
#include "_cgo_export.h"

static int wrap_eval(const char *expr, char **result) {
  return GoEval(expr, result);
}

int vimremote_register_wrap(const char *servername) {
    return vimremote_register(servername, wrap_eval);
}
