#ifndef VIMREMOTE_H_INCLUDED
#define VIMREMOTE_H_INCLUDED

#include <stddef.h>

typedef int (*vimremote_eval_f) (const char *expr, char **result);

void *vimremote_alloc(size_t len);
void vimremote_free(void *p);
int vimremote_init();
int vimremote_uninit();
int vimremote_serverlist(char **servernames);
int vimremote_remoteexpr(const char *servername, const char *expr, char **result);
int vimremote_register(const char *servername, vimremote_eval_f eval);
int vimremote_eventloop(int forever);

#endif
