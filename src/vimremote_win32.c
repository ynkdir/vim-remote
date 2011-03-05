
#include <stdio.h>

#include <windows.h>

#include "vimthings.h"

#include "vimremote.h"

/* Use invalid encoding name to prevent conversion.
 * In Vim, conversion will fail and received data will be used as is. */
static char_u p_enc[] = "RAWDATA";
static vimremote_eval_f usereval = NULL;
static char_u *serverName = NULL;

static void serverSendEnc(HWND target);
static void CleanUpMessaging(void);
static LRESULT CALLBACK Messaging_WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);
void serverInitMessaging(void);
static int getVimServerName(HWND hwnd, char *name, int namelen);
static BOOL CALLBACK enumWindowsGetServer(HWND hwnd, LPARAM lparam);
static BOOL CALLBACK enumWindowsGetNames(HWND hwnd, LPARAM lparam);
static HWND findServer(char_u *name);
int serverSetName(char_u *name);
char_u *serverGetVimNames(void);
int serverSendReply(char_u *name, char_u *reply);
int serverSendToVim(char_u *name, char_u *cmd, char_u **result);
static int save_reply(HWND server, char_u *reply, int expr);
char_u *serverGetReply(HWND server, int *expr_res, int remove, int wait);
void serverProcessPendingMessages(void);
static char_u * serverConvert(char_u *client_enc, char_u *data, char_u **tofree);

void *
vimremote_malloc(size_t len)
{
    return malloc(len);
}

void
vimremote_free(void *p)
{
    free(p);
}

int
vimremote_init()
{
    serverInitMessaging();
    return 0;
}

int
vimremote_uninit()
{
    CleanUpMessaging();
    return 0;
}

int
vimremote_serverlist(char **servernames)
{
    *servernames = (char *)serverGetVimNames();
    return 0;
}

int
vimremote_remoteexpr(const char *servername, const char *expr, char **result)
{
    return serverSendToVim((char_u *)servername, (char_u *)expr, (char_u **)result);
}

int
vimremote_register(const char *servername, vimremote_eval_f eval)
{
    if (!serverSetName((char_u *)servername))
        return -1;
    usereval = eval;
    return 0;
}

int
vimremote_eventloop(int forever)
{
    BOOL bRet;
    MSG msg;

    if (forever)
    {
        while ((bRet = GetMessage(&msg, NULL, 0, 0)) != 0)
        {
            if (bRet == -1)
                break;
            TranslateMessage(&msg);
            DispatchMessage(&msg);
        }
    }
    else
    {
        while (PeekMessage(&msg, NULL, 0, 0, PM_REMOVE))
        {
            TranslateMessage(&msg);
            DispatchMessage(&msg);
        }
    }

    return 0;
}


/* vim/src/os_mswin.c */
/* vi:set ts=8 sts=4 sw=4:
 *
 * VIM - Vi IMproved	by Bram Moolenaar
 *
 * Do ":help uganda"  in Vim to read copying and usage conditions.
 * Do ":help credits" in Vim to see a list of people who contributed.
 * See README.txt for an overview of the Vim source code.
 */
/*
 * Client-server code for Vim
 *
 * Originally written by Paul Moore
 */

/* In order to handle inter-process messages, we need to have a window. But
 * the functions in this module can be called before the main GUI window is
 * created (and may also be called in the console version, where there is no
 * GUI window at all).
 *
 * So we create a hidden window, and arrange to destroy it on exit.
 */
HWND message_window = 0;	    /* window that's handling messsages */

#define VIM_CLASSNAME      "VIM_MESSAGES"
#define VIM_CLASSNAME_LEN  (sizeof(VIM_CLASSNAME) - 1)

/* Communication is via WM_COPYDATA messages. The message type is send in
 * the dwData parameter. Types are defined here. */
#define COPYDATA_KEYS		0
#define COPYDATA_REPLY		1
#define COPYDATA_EXPR		10
#define COPYDATA_RESULT		11
#define COPYDATA_ERROR_RESULT	12
#define COPYDATA_ENCODING	20

/* This is a structure containing a server HWND and its name. */
struct server_id
{
    HWND hwnd;
    char_u *name;
};

/* Last received 'encoding' that the client uses. */
static char_u	*client_enc = NULL;

/*
 * Tell the other side what encoding we are using.
 * Errors are ignored.
 */
    static void
serverSendEnc(HWND target)
{
    COPYDATASTRUCT data;

    data.dwData = COPYDATA_ENCODING;
    data.cbData = (DWORD)STRLEN(p_enc) + 1;
    data.lpData = p_enc;
    (void)SendMessage(target, WM_COPYDATA, (WPARAM)message_window,
							     (LPARAM)(&data));
}

/*
 * Clean up on exit. This destroys the hidden message window.
 */
    static void
CleanUpMessaging(void)
{
    if (message_window != 0)
    {
	DestroyWindow(message_window);
	message_window = 0;
    }
}

static int save_reply(HWND server, char_u *reply, int expr);

/*s
 * The window procedure for the hidden message window.
 * It handles callback messages and notifications from servers.
 * In order to process these messages, it is necessary to run a
 * message loop. Code which may run before the main message loop
 * is started (in the GUI) is careful to pump messages when it needs
 * to. Features which require message delivery during normal use will
 * not work in the console version - this basically means those
 * features which allow Vim to act as a server, rather than a client.
 */
    static LRESULT CALLBACK
Messaging_WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    int err;

    if (msg == WM_COPYDATA)
    {
	/* This is a message from another Vim. The dwData member of the
	 * COPYDATASTRUCT determines the type of message:
	 *   COPYDATA_ENCODING:
	 *	The encoding that the client uses. Following messages will
	 *	use this encoding, convert if needed.
	 *   COPYDATA_KEYS:
	 *	A key sequence. We are a server, and a client wants these keys
	 *	adding to the input queue.
	 *   COPYDATA_REPLY:
	 *	A reply. We are a client, and a server has sent this message
	 *	in response to a request.  (server2client())
	 *   COPYDATA_EXPR:
	 *	An expression. We are a server, and a client wants us to
	 *	evaluate this expression.
	 *   COPYDATA_RESULT:
	 *	A reply. We are a client, and a server has sent this message
	 *	in response to a COPYDATA_EXPR.
	 *   COPYDATA_ERROR_RESULT:
	 *	A reply. We are a client, and a server has sent this message
	 *	in response to a COPYDATA_EXPR that failed to evaluate.
	 */
	COPYDATASTRUCT	*data = (COPYDATASTRUCT*)lParam;
	HWND		sender = (HWND)wParam;
	COPYDATASTRUCT	reply;
	char_u		*res;
	int		retval;
	char_u		*str;
	char_u		*tofree;

	switch (data->dwData)
	{
	case COPYDATA_ENCODING:
	    /* Remember the encoding that the client uses. */
	    vim_free(client_enc);
            // TODO:
	    //client_enc = enc_canonize((char_u *)data->lpData);
	    client_enc = vim_strsave(data->lpData);
	    return 1;

	case COPYDATA_KEYS:
            /* not supported */
	    return 1;

	case COPYDATA_EXPR:
	    str = serverConvert(client_enc, (char_u *)data->lpData, &tofree);

            if (usereval == NULL)
            {
                err = -1;
                res = NULL;
            }
            else
            {
                res = NULL;
                err = usereval((char *)str, (char **)&res);
            }

	    vim_free(tofree);

            if (!err)
		reply.dwData = COPYDATA_RESULT;
            else
		reply.dwData = COPYDATA_ERROR_RESULT;
	    reply.lpData = (res == NULL) ? (char_u *)"" : res;
	    reply.cbData = (res == NULL) ? 1 : (DWORD)STRLEN(res) + 1;

	    serverSendEnc(sender);
	    retval = (int)SendMessage(sender, WM_COPYDATA, (WPARAM)message_window,
							    (LPARAM)(&reply));
	    vim_free(res);
	    return retval;

	case COPYDATA_REPLY:
	case COPYDATA_RESULT:
	case COPYDATA_ERROR_RESULT:
	    if (data->lpData != NULL)
	    {
		str = serverConvert(client_enc, (char_u *)data->lpData,
								     &tofree);
		if (tofree == NULL)
		    str = vim_strsave(str);
		if (save_reply(sender, str,
			   (data->dwData == COPYDATA_REPLY ?  0 :
			   (data->dwData == COPYDATA_RESULT ? 1 :
							      2))) == FAIL)
		    vim_free(str);
	    }
	    return 1;
	}

	return 0;
    }

    return DefWindowProc(hwnd, msg, wParam, lParam);
}

/*
 * Initialise the message handling process.  This involves creating a window
 * to handle messages - the window will not be visible.
 */
    void
serverInitMessaging(void)
{
    WNDCLASS wndclass;
    HINSTANCE s_hinst;

    /* Clean up on exit */
    atexit(CleanUpMessaging);

    /* Register a window class - we only really care
     * about the window procedure
     */
    s_hinst = (HINSTANCE)GetModuleHandle(0);
    wndclass.style = 0;
    wndclass.lpfnWndProc = Messaging_WndProc;
    wndclass.cbClsExtra = 0;
    wndclass.cbWndExtra = 0;
    wndclass.hInstance = s_hinst;
    wndclass.hIcon = NULL;
    wndclass.hCursor = NULL;
    wndclass.hbrBackground = NULL;
    wndclass.lpszMenuName = NULL;
    wndclass.lpszClassName = VIM_CLASSNAME;
    RegisterClass(&wndclass);

    /* Create the message window. It will be hidden, so the details don't
     * matter.  Don't use WS_OVERLAPPEDWINDOW, it will make a shortcut remove
     * focus from gvim. */
    message_window = CreateWindow(VIM_CLASSNAME, "",
			 WS_POPUPWINDOW | WS_CAPTION,
			 CW_USEDEFAULT, CW_USEDEFAULT,
			 100, 100, NULL, NULL,
			 s_hinst, NULL);
}

/*
 * Get the title of the window "hwnd", which is the Vim server name, in
 * "name[namelen]" and return the length.
 * Returns zero if window "hwnd" is not a Vim server.
 */
    static int
getVimServerName(HWND hwnd, char *name, int namelen)
{
    int		len;
    char	buffer[VIM_CLASSNAME_LEN + 1];

    /* Ignore windows which aren't Vim message windows */
    len = GetClassName(hwnd, buffer, sizeof(buffer));
    if (len != VIM_CLASSNAME_LEN || STRCMP(buffer, VIM_CLASSNAME) != 0)
	return 0;

    /* Get the title of the window */
    return GetWindowText(hwnd, name, namelen);
}

    static BOOL CALLBACK
enumWindowsGetServer(HWND hwnd, LPARAM lparam)
{
    struct	server_id *id = (struct server_id *)lparam;
    char	server[MAX_PATH];

    /* Get the title of the window */
    if (getVimServerName(hwnd, server, sizeof(server)) == 0)
	return TRUE;

    /* If this is the server we're looking for, return its HWND */
    if (STRICMP(server, id->name) == 0)
    {
	id->hwnd = hwnd;
	return FALSE;
    }

    /* Otherwise, keep looking */
    return TRUE;
}

    static BOOL CALLBACK
enumWindowsGetNames(HWND hwnd, LPARAM lparam)
{
    garray_T	*ga = (garray_T *)lparam;
    char	server[MAX_PATH];

    /* Get the title of the window */
    if (getVimServerName(hwnd, server, sizeof(server)) == 0)
	return TRUE;

    /* Add the name to the list */
    ga_concat(ga, (char_u *)server);
    ga_concat(ga, (char_u *)"\n");
    return TRUE;
}

    static HWND
findServer(char_u *name)
{
    struct server_id id;

    id.name = name;
    id.hwnd = 0;

    EnumWindows(enumWindowsGetServer, (LPARAM)(&id));

    return id.hwnd;
}

    int
serverSetName(char_u *name)
{
    HWND	hwnd = 0;

    hwnd = findServer(name);
    if (hwnd != 0)
        return FALSE;

    /* Remember the name */
    serverName = vim_strsave(name);

    /* Update the message window title */
    SetWindowText(message_window, (char *)name);

    return TRUE;
}

    char_u *
serverGetVimNames(void)
{
    garray_T ga;

    ga_init2(&ga, 1, 100);

    EnumWindows(enumWindowsGetNames, (LPARAM)(&ga));
    ga_append(&ga, NUL);

    return ga.ga_data;
}

    int
serverSendReply(name, reply)
    char_u	*name;		/* Where to send. */
    char_u	*reply;		/* What to send. */
{
    HWND	target;
    COPYDATASTRUCT data;
    long_u	n = 0;

    /* The "name" argument is a magic cookie obtained from expand("<client>").
     * It should be of the form 0xXXXXX - i.e. a C hex literal, which is the
     * value of the client's message window HWND.
     */
    sscanf((char *)name, SCANF_HEX_LONG_U, &n);
    if (n == 0)
	return -1;

    target = (HWND)n;
    if (!IsWindow(target))
	return -1;

    data.dwData = COPYDATA_REPLY;
    data.cbData = (DWORD)STRLEN(reply) + 1;
    data.lpData = reply;

    serverSendEnc(target);
    if (SendMessage(target, WM_COPYDATA, (WPARAM)message_window,
							     (LPARAM)(&data)))
	return 0;

    return -1;
}

    int
serverSendToVim(name, cmd, result)
    char_u	 *name;			/* Where to send. */
    char_u	 *cmd;			/* What to send. */
    char_u	 **result;		/* Result of eval'ed expression */
{
    HWND	target;
    COPYDATASTRUCT data;
    char_u	*retval = NULL;
    int		retcode = 0;

    target = findServer(name);

    if (target == 0)
    {
	return -1;
    }

    data.dwData = COPYDATA_EXPR;
    data.cbData = (DWORD)STRLEN(cmd) + 1;
    data.lpData = cmd;

    serverSendEnc(target);
    if (SendMessage(target, WM_COPYDATA, (WPARAM)message_window,
							(LPARAM)(&data)) == 0)
	return -1;

    retval = serverGetReply(target, &retcode, TRUE, TRUE);

    if (result == NULL)
	vim_free(retval);
    else
	*result = retval; /* Caller assumes responsibility for freeing */

    return retcode;
}

/* Replies from server need to be stored until the client picks them up via
 * remote_read(). So we maintain a list of server-id/reply pairs.
 * Note that there could be multiple replies from one server pending if the
 * client is slow picking them up.
 * We just store the replies in a simple list. When we remove an entry, we
 * move list entries down to fill the gap.
 * The server ID is simply the HWND.
 */
typedef struct
{
    HWND	server;		/* server window */
    char_u	*reply;		/* reply string */
    int		expr_result;	/* 0 for REPLY, 1 for RESULT 2 for error */
} reply_T;

static garray_T reply_list = {0, 0, sizeof(reply_T), 5, 0};

#define REPLY_ITEM(i) ((reply_T *)(reply_list.ga_data) + (i))
#define REPLY_COUNT (reply_list.ga_len)

/* Flag which is used to wait for a reply */
static int reply_received = 0;

/*
 * Store a reply.  "reply" must be allocated memory (or NULL).
 */
    static int
save_reply(HWND server, char_u *reply, int expr)
{
    reply_T *rep;

    if (ga_grow(&reply_list, 1) == FAIL)
	return FAIL;

    rep = REPLY_ITEM(REPLY_COUNT);
    rep->server = server;
    rep->reply = reply;
    rep->expr_result = expr;
    if (rep->reply == NULL)
	return FAIL;

    ++REPLY_COUNT;
    reply_received = 1;
    return OK;
}

/*
 * Get a reply from server "server".
 * When "expr_res" is non NULL, get the result of an expression, otherwise a
 * server2client() message.
 * When non NULL, point to return code. 0 => OK, -1 => ERROR
 * If "remove" is TRUE, consume the message, the caller must free it then.
 * if "wait" is TRUE block until a message arrives (or the server exits).
 */
    char_u *
serverGetReply(HWND server, int *expr_res, int remove, int wait)
{
    int		i;
    char_u	*reply;
    reply_T	*rep;

    /* When waiting, loop until the message waiting for is received. */
    for (;;)
    {
	/* Reset this here, in case a message arrives while we are going
	 * through the already received messages. */
	reply_received = 0;

	for (i = 0; i < REPLY_COUNT; ++i)
	{
	    rep = REPLY_ITEM(i);
	    if (rep->server == server
		    && ((rep->expr_result != 0) == (expr_res != NULL)))
	    {
		/* Save the values we've found for later */
		reply = rep->reply;
		if (expr_res != NULL)
		    *expr_res = rep->expr_result == 1 ? 0 : -1;

		if (remove)
		{
		    /* Move the rest of the list down to fill the gap */
		    mch_memmove(rep, rep + 1,
				     (REPLY_COUNT - i - 1) * sizeof(reply_T));
		    --REPLY_COUNT;
		}

		/* Return the reply to the caller, who takes on responsibility
		 * for freeing it if "remove" is TRUE. */
		return reply;
	    }
	}

	/* If we got here, we didn't find a reply. Return immediately if the
	 * "wait" parameter isn't set.  */
	if (!wait)
	    break;

	/* We need to wait for a reply. Enter a message loop until the
	 * "reply_received" flag gets set. */

	/* Loop until we receive a reply */
	while (reply_received == 0)
	{
	    /* Wait for a SendMessage() call to us.  This could be the reply
	     * we are waiting for.  Use a timeout of a second, to catch the
	     * situation that the server died unexpectedly. */
	    MsgWaitForMultipleObjects(0, NULL, TRUE, 1000, QS_ALLINPUT);

	    /* If the server has died, give up */
	    if (!IsWindow(server))
		return NULL;

	    serverProcessPendingMessages();
	}
    }

    return NULL;
}

/*
 * Process any messages in the Windows message queue.
 */
    void
serverProcessPendingMessages(void)
{
    MSG msg;

    while (PeekMessage(&msg, NULL, 0, 0, PM_REMOVE))
    {
	TranslateMessage(&msg);
	DispatchMessage(&msg);
    }
}

static char_u *
serverConvert(char_u * UNUSED(client_enc), char_u *data, char_u **tofree)
{
    char_u	*res = data;
    *tofree = NULL;
    return res;
}

