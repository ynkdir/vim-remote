#ifndef VIMREMOTE_H_INCLUDED
#define VIMREMOTE_H_INCLUDED

#include <stddef.h>

typedef int (*vimremote_send_f) (const char *keys);
typedef int (*vimremote_expr_f) (const char *expr, char **result);

void *vimremote_malloc(size_t len);
void vimremote_free(void *p);
int vimremote_init();
int vimremote_uninit();
int vimremote_serverlist(char **servernames);
int vimremote_remotesend(const char *servername, const char *keys);
int vimremote_remoteexpr(const char *servername, const char *expr, char **result);
int vimremote_register(const char *servername, vimremote_send_f send_f, vimremote_expr_f expr_f);
int vimremote_eventloop(int forever);

#endif
