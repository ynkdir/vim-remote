
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>

#include "vimremote.h"

static void fatal(const char *format, ...);
static void command_serverlist();
static void command_remoteexpr(const char *servername, const char *expr);
static void command_server(const char *servername);
static int echoeval(const char *expr, char **result);
static void usage();

static void
fatal(const char *format, ...)
{
    va_list ap;

    va_start(ap, format);
    vfprintf(stderr, format, ap);
    fprintf(stderr, "\n");
    va_end(ap);

    exit(EXIT_FAILURE);
}

static void
command_serverlist()
{
    char *servernames;

    if (vimremote_serverlist(&servernames) != 0) {
        fatal("vimremote_serverlist() failed");
    }

    printf("%s", servernames);

    vimremote_free(servernames);
}

static void
command_remoteexpr(const char *servername, const char *expr)
{
    char *result;

    if (vimremote_remoteexpr(servername, expr, &result) != 0) {
        fatal("vimremote_remoteexpr() failed: %s",
                result == NULL ? "" : result);
    }

    printf("%s\n", result);

    vimremote_free(result);
}

static void
command_server(const char *servername)
{
    if (vimremote_register(servername, echoeval) != 0) {
        fatal("vimremote_register() failed");
    }
    vimremote_eventloop(1);
}

static int
echoeval(const char *expr, char **result)
{
    printf("%s\n", expr);
    *result = vimremote_alloc(strlen(expr) + 1);
    strcpy(*result, expr);
    return 0;
}

static void
usage()
{
    printf("vimremote\n");
    printf("  -h or --help          display help\n");
    printf("  --serverlist          List available Vim server names\n");
    printf("  --servername <name>   Vim server name\n");
    printf("  --remote-expr <expr>  Evaluate <expr> in a Vim server\n");
    printf("  --server              Start server\n");
}

int
main(int argc, char **argv)
{
    const char *servername = NULL;
    const char *remoteexpr = NULL;
    int help = 0;
    int serverlist = 0;
    int server = 0;
    int i;

    if (argc == 1) {
        usage();
        return 0;
    }

    if (vimremote_init() != 0) {
        fatal("vimremote_init() failed");
    }

    for (i = 1; i < argc; ++i) {
        if (strcmp(argv[i], "-h") == 0 || strcmp(argv[i], "--help") == 0) {
            help = 1;
        } else if (strcmp(argv[i], "--serverlist") == 0) {
            serverlist = 1;
        } else if (strcmp(argv[i], "--servername") == 0) {
            servername = argv[++i];
        } else if (strcmp(argv[i], "--remote-expr") == 0) {
            remoteexpr = argv[++i];
        } else if (strcmp(argv[i], "--server") == 0) {
            server = 1;
        } else {
            fatal("unknown option");
        }
    }

    if (help) {
        usage();
    } else if (serverlist) {
        command_serverlist();
    } else if (remoteexpr != NULL) {
        if (servername == NULL) {
            fatal("remoteexpr requires servername");
        }
        command_remoteexpr(servername, remoteexpr);
    } else if (server) {
        if (servername == NULL) {
            fatal("server requires servername");
        }
        command_server(servername);
    }

    if (vimremote_uninit() != 0) {
        fatal("vimremote_uninit() failed");
    }

    return 0;
}

