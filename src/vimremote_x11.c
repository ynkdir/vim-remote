
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <time.h>
#include <unistd.h>

#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <X11/Intrinsic.h>
#include <X11/Xatom.h>

#include "vimthings.h"

#include "vimremote.h"

#define VIM_VERSION_SHORT "7.3"
#define MAX_PROP_WORDS 100000

/* Use invalid encoding name to prevent conversion.
 * In Vim, conversion will fail and received data will be used as is. */
char_u p_enc[] = "RAWDATA";

typedef struct PendingCommand {
    int serial;
    int code;
    char_u *result;
    struct PendingCommand *nextPtr;
} PendingCommand;

static PendingCommand *pendingCommands = NULL;

typedef int (*EndCond) (void *);

static Window LookupName(Display *dpy, char_u *name, int delete);
static int SendInit(Display *dpy);
static int DoRegisterName(Display *dpy, char_u *name);
static int serverSendToVim(Display *dpy, char_u *name, char_u *cmd, char_u **result);
static void DeleteAnyLingerer(Display *dpy, Window w);
static int GetRegProp(Display *dpy, char_u **regPropp, long_u *numItemsp);
static void serverEventProc(Display *dpy, XEvent *eventPtr);
static int WaitForPend(void *p);
static int WindowValid(Display *dpy, Window w);
static void ServerWait(Display *dpy, Window w, EndCond endCond, void *endData, int seconds);
static int AppendPropCarefully(Display *display, Window window, Atom property, char_u *value, int length);
static int x_error_check(Display *dpy, XErrorEvent *error_event);
static char_u * serverConvert(char_u *client_enc, char_u *data, char_u **tofree);

static Display *display = NULL;
static Window commWindow = None;
static Atom commProperty = None;
static Atom vimProperty = None;
static Atom registryProperty = None;
static int got_x_error = False;
static vimremote_eval_f usereval = NULL;
static char_u *serverName = NULL;

void *
vimremote_alloc(size_t len)
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
    if (display != NULL) {
        return 0;
    }

    display = XOpenDisplay(NULL);
    if (display == NULL) {
        return -1;
    }

    if (SendInit(display) != 0) {
        return -1;
    }

    return 0;
}

int
vimremote_uninit()
{
    if (display == NULL) {
        return 0;
    }

    if (commWindow != None) {
        XDestroyWindow(display, commWindow);
        commWindow = None;
    }

    XCloseDisplay(display);
    display = NULL;

    return 0;
}

/* @param servernames       line separated list */
int
vimremote_serverlist(char **servernames)
{
    long_u nitems;
    char *prop;
    char *p;
    int windowid;
    char name[256];
    int n;
    garray_T ga;

    if (!GetRegProp(display, (char_u **)&prop, &nitems)) {
        return -1;
    }

    if (prop == NULL) {
        *servernames = strdup("");
        return 0;
    }

    ga_init2(&ga, 1, 100);

    for (p = prop; *p != '\0'; ++p) {
        n = sscanf(p, "%x %s", &windowid, name);
        if (n != 2) {
            /* parse error */
            XFree(prop);
            return -1;
        }
        if (WindowValid(display, (Window)windowid)) {
            ga_concat(&ga, (char_u *)name);
            ga_concat(&ga, (char_u *)"\n");
        }
        p += strlen(p);
    }

    ga_append(&ga, NUL);

    XFree(prop);

    *servernames = ga.ga_data;

    return 0;
}

int
vimremote_remoteexpr(const char *servername, const char *expr, char **result)
{
    return serverSendToVim(display, (char_u *)servername, (char_u *)expr, (char_u **)result);
}

int
vimremote_register(const char *servername, vimremote_eval_f eval)
{
    if (DoRegisterName(display, (char_u *)servername) != 0) {
        return -1;
    }
    usereval = eval;
    return 0;
}

int
vimremote_eventloop(int forever)
{
    XEvent event;
    XPropertyEvent *e = (XPropertyEvent *)&event;

    if (forever) {
        for (;;) {
            XNextEvent(display, &event);
            if (event.type == PropertyNotify && e->window == commWindow) {
                serverEventProc(display, &event);
            }
        }
    } else {
        while (XEventsQueued(display, QueuedAfterReading) > 0) {
            XNextEvent(display, &event);
            if (event.type == PropertyNotify && e->window == commWindow) {
                serverEventProc(display, &event);
            }
        }
    }

    return 0;
}


/* vim/src/if_xcmdsrv.c */
/* vi:set ts=8 sts=4 sw=4:
 *
 * VIM - Vi IMproved	by Bram Moolenaar
 * X command server by Flemming Madsen
 *
 * Do ":help uganda"  in Vim to read copying and usage conditions.
 * Do ":help credits" in Vim to see a list of people who contributed.
 * See README.txt for an overview of the Vim source code.
 *
 * if_xcmdsrv.c: Functions for passing commands through an X11 display.
 *
 */
/*
 * This file provides procedures that implement the command server
 * functionality of Vim when in contact with an X11 server.
 *
 * Adapted from TCL/TK's send command  in tkSend.c of the tk 3.6 distribution.
 * Adapted for use in Vim by Flemming Madsen. Protocol changed to that of tk 4
 */

/*
 * Copyright (c) 1989-1993 The Regents of the University of California.
 * All rights reserved.
 *
 * Permission is hereby granted, without written agreement and without
 * license or royalty fees, to use, copy, modify, and distribute this
 * software and its documentation for any purpose, provided that the
 * above copyright notice and the following two paragraphs appear in
 * all copies of this software.
 *
 * IN NO EVENT SHALL THE UNIVERSITY OF CALIFORNIA BE LIABLE TO ANY PARTY FOR
 * DIRECT, INDIRECT, SPECIAL, INCIDENTAL, OR CONSEQUENTIAL DAMAGES ARISING OUT
 * OF THE USE OF THIS SOFTWARE AND ITS DOCUMENTATION, EVEN IF THE UNIVERSITY OF
 * CALIFORNIA HAS BEEN ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 * THE UNIVERSITY OF CALIFORNIA SPECIFICALLY DISCLAIMS ANY WARRANTIES,
 * INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY
 * AND FITNESS FOR A PARTICULAR PURPOSE.  THE SOFTWARE PROVIDED HEREUNDER IS
 * ON AN "AS IS" BASIS, AND THE UNIVERSITY OF CALIFORNIA HAS NO OBLIGATION TO
 * PROVIDE MAINTENANCE, SUPPORT, UPDATES, ENHANCEMENTS, OR MODIFICATIONS.
 */

static Window
LookupName(Display *dpy, char_u *name, int delete)
{
    char_u	*regProp, *entry;
    char_u	*p;
    long_u	numItems;
    int_u	returnValue;

    /*
     * Read the registry property.
     */
    if (!GetRegProp(dpy, &regProp, &numItems))
	return 0;

    if (regProp == NULL) {
        return None;
    }

    /*
     * Scan the property for the desired name.
     */
    returnValue = (int_u)None;
    entry = NULL;	/* Not needed, but eliminates compiler warning. */
    for (p = regProp; (long_u)(p - regProp) < numItems; )
    {
	entry = p;
	while (*p != 0 && !isspace(*p))
	    p++;
	if (*p != 0 && STRICMP(name, p + 1) == 0)
	{
	    sscanf((char *)entry, "%x", &returnValue);
	    break;
	}
	while (*p != 0)
	    p++;
	p++;
    }

    /*
     * Delete the property, if that is desired (copy down the
     * remainder of the registry property to overlay the deleted
     * info, then rewrite the property).
     */
    if (delete && returnValue != (int_u)None)
    {
	int count;

	while (*p != 0)
	    p++;
	p++;
	count = numItems - (p - regProp);
	if (count > 0)
	    memmove(entry, p, count);
	XChangeProperty(dpy, RootWindow(dpy, 0), registryProperty, XA_STRING,
			8, PropModeReplace, regProp,
			(int)(numItems - (p - entry)));
	XSync(dpy, False);
    }

    XFree(regProp);

    return (Window)returnValue;
}

static int
SendInit(Display *dpy)
{
    XErrorHandler old_handler;

    /*
     * Create the window used for communication, and set up an
     * event handler for it.
     */
    old_handler = XSetErrorHandler(x_error_check);
    got_x_error = False;

    if (commProperty == None)
	commProperty = XInternAtom(dpy, "Comm", False);
    if (vimProperty == None)
	vimProperty = XInternAtom(dpy, "Vim", False);
    if (registryProperty == None)
	registryProperty = XInternAtom(dpy, "VimRegistry", False);

    if (commWindow == None)
    {
	commWindow = XCreateSimpleWindow(dpy, XDefaultRootWindow(dpy),
				getpid(), 0, 10, 10, 0,
				WhitePixel(dpy, DefaultScreen(dpy)),
				WhitePixel(dpy, DefaultScreen(dpy)));
	XSelectInput(dpy, commWindow, PropertyChangeMask);
	/* WARNING: Do not step through this while debugging, it will hangup
	 * the X server! */
	XGrabServer(dpy);
	DeleteAnyLingerer(dpy, commWindow);
	XUngrabServer(dpy);
    }

    /* Make window recognizable as a vim window */
    XChangeProperty(dpy, commWindow, vimProperty, XA_STRING,
		    8, PropModeReplace, (char_u *)VIM_VERSION_SHORT,
			(int)STRLEN(VIM_VERSION_SHORT) + 1);

    XSync(dpy, False);
    (void)XSetErrorHandler(old_handler);

    return got_x_error ? -1 : 0;
}

static int
DoRegisterName(Display *dpy, char_u *name)
{
    Window	w;
    XErrorHandler old_handler;
#define MAX_NAME_LENGTH 100
    char_u	propInfo[MAX_NAME_LENGTH + 20];

    /*
     * Make sure the name is unique, and append info about it to
     * the registry property.  It's important to lock the server
     * here to prevent conflicting changes to the registry property.
     * WARNING: Do not step through this while debugging, it will hangup the X
     * server!
     */
    XGrabServer(dpy);
    w = LookupName(dpy, name, FALSE);
    if (w != (Window)0)
    {
	Status		status;
	int		dummyInt;
	unsigned int	dummyUns;
	Window		dummyWin;

	/*
	 * The name is currently registered.  See if the commWindow
	 * associated with the name exists.  If not, or if the commWindow
	 * is *our* commWindow, then just unregister the old name (this
	 * could happen if an application dies without cleaning up the
	 * registry).
	 */
	old_handler = XSetErrorHandler(x_error_check);
	status = XGetGeometry(dpy, w, &dummyWin, &dummyInt, &dummyInt,
				  &dummyUns, &dummyUns, &dummyUns, &dummyUns);
	(void)XSetErrorHandler(old_handler);
	if (status != Success && w != commWindow)
	{
	    XUngrabServer(dpy);
	    XFlush(dpy);
	    return -1;
	}
	(void)LookupName(dpy, name, /*delete=*/TRUE);
    }
    sprintf((char *)propInfo, "%x %.*s", (int_u)commWindow,
						       MAX_NAME_LENGTH, name);
    old_handler = XSetErrorHandler(x_error_check);
    got_x_error = FALSE;
    XChangeProperty(dpy, RootWindow(dpy, 0), registryProperty, XA_STRING, 8,
		    PropModeAppend, propInfo, STRLEN(propInfo) + 1);
    XUngrabServer(dpy);
    XSync(dpy, False);
    (void)XSetErrorHandler(old_handler);

    if (!got_x_error)
    {
        serverName = vim_strsave(name);
	return 0;
    }
    return -2;
}

static int
serverSendToVim(Display *dpy, char_u *name, char_u *cmd, char_u **result)
{
    Window	    w;
    char_u	    *property;
    int		    length;
    int		    res;
    static int	    serial = 0;	/* Running count of sent commands.
				 * Used to give each command a
				 * different serial number. */
    PendingCommand  pending;

    w = LookupName(dpy, name, False);
    if (w == None) {
        return -1;
    }

    /*
     * Send the command to target interpreter by appending it to the
     * comm window in the communication window.
     * Length must be computed exactly!
     */
    length = STRLEN(name) + STRLEN(p_enc) + STRLEN(cmd) + 14;
    property = (char_u *)malloc((unsigned)length + 30);

    sprintf((char *)property, "%c%c%c-n %s%c-E %s%c-s %s",
		      0, 'c', 0, name, 0, p_enc, 0, cmd);
    /* Add a back reference to our comm window */
    serial++;
    sprintf((char *)property + length, "%c-r %x %d",
						0, (int_u)commWindow, serial);
    /* Add length of what "-r %x %d" resulted in, skipping the NUL. */
    length += STRLEN(property + length + 1) + 1;

    res = AppendPropCarefully(dpy, w, commProperty, property, length + 1);
    free(property);
    if (res < 0)
    {
	return -1;
    }

    /*
     * Register the fact that we're waiting for a command to
     * complete (this is needed by SendEventProc and by
     * AppendErrorProc to pass back the command's results).
     */
    pending.serial = serial;
    pending.code = 0;
    pending.result = NULL;
    pending.nextPtr = pendingCommands;
    pendingCommands = &pending;

    ServerWait(dpy, w, WaitForPend, &pending, 600);

    /*
     * Unregister the information about the pending command
     * and return the result.
     */
    if (pendingCommands == &pending)
	pendingCommands = pending.nextPtr;
    else
    {
	PendingCommand *pcPtr;

	for (pcPtr = pendingCommands; pcPtr != NULL; pcPtr = pcPtr->nextPtr)
	    if (pcPtr->nextPtr == &pending)
	    {
		pcPtr->nextPtr = pending.nextPtr;
		break;
	    }
    }
    if (result != NULL)
	*result = pending.result;
    else
	free(pending.result);

    return pending.code == 0 ? 0 : -1;
}


static void
DeleteAnyLingerer(Display *dpy, Window win)
{
    char_u	*regProp, *entry = NULL;
    char_u	*p;
    long_u	numItems;
    int_u	wwin;

    /*
     * Read the registry property.
     */
    if (!GetRegProp(dpy, &regProp, &numItems))
	return;

    if (regProp == NULL)
        return;

    /* Scan the property for the window id.  */
    for (p = regProp; (long_u)(p - regProp) < numItems; )
    {
	if (*p != 0)
	{
	    sscanf((char *)p, "%x", &wwin);
	    if ((Window)wwin == win)
	    {
		int lastHalf;

		/* Copy down the remainder to delete entry */
		entry = p;
		while (*p != 0)
		    p++;
		p++;
		lastHalf = numItems - (p - regProp);
		if (lastHalf > 0)
		    memmove(entry, p, lastHalf);
		numItems = (entry - regProp) + lastHalf;
		p = entry;
		continue;
	    }
	}
	while (*p != 0)
	    p++;
	p++;
    }

    if (entry != NULL)
    {
	XChangeProperty(dpy, RootWindow(dpy, 0), registryProperty,
			XA_STRING, 8, PropModeReplace, regProp,
			(int)(p - regProp));
	XSync(dpy, False);
    }

    XFree(regProp);
}

static int
GetRegProp(Display *dpy, char_u **regPropp, long_u *numItemsp)
{
    int result;
    Atom actual_type;
    int actual_format;
    unsigned long nitems;
    unsigned long bytes_after;
    unsigned char *prop;
    XErrorHandler old_handler;

    got_x_error = False;
    old_handler = XSetErrorHandler(x_error_check);
    result = XGetWindowProperty(dpy, RootWindow(dpy, 0), registryProperty,
            0L, (long)MAX_PROP_WORDS, False,
            XA_STRING, &actual_type, &actual_format,
            &nitems, &bytes_after, &prop);
    XSync(dpy, False);
    XSetErrorHandler(old_handler);

    if (got_x_error) {
        return False;
    }

    if (actual_type == None) {
        /* No prop yet. Logically equal to the empty list */
        *numItemsp = 0;
        *regPropp = NULL;
        return True;
    }

    /* If the property is improperly formed, then delete it. */
    if (result != Success || actual_format != 8 || actual_type != XA_STRING) {
        if (prop != NULL) {
            XFree(prop);
        }
        XDeleteProperty(dpy, RootWindow(dpy, 0), registryProperty);
        return False;
    }

    *numItemsp = nitems;
    *regPropp = prop;

    return True;
}

void
serverEventProc(Display *dpy, XEvent *eventPtr)
{
    char_u	*propInfo;
    char_u	*p;
    int		result, actualFormat, code;
    long_u	numItems, bytesAfter;
    Atom	actualType;
    char_u	*tofree;

    if (eventPtr != NULL)
    {
	if (eventPtr->xproperty.atom != commProperty
		|| eventPtr->xproperty.state != PropertyNewValue)
	    return;
    }

    /*
     * Read the comm property and delete it.
     */
    propInfo = NULL;
    result = XGetWindowProperty(dpy, commWindow, commProperty, 0L,
				(long)MAX_PROP_WORDS, True,
				XA_STRING, &actualType,
				&actualFormat, &numItems, &bytesAfter,
				&propInfo);

    /* If the property doesn't exist or is improperly formed then ignore it. */
    if (result != Success || actualType != XA_STRING || actualFormat != 8)
    {
	if (propInfo != NULL)
	    XFree(propInfo);
	return;
    }

    /*
     * Several commands and results could arrive in the property at
     * one time;  each iteration through the outer loop handles a
     * single command or result.
     */
    for (p = propInfo; (long_u)(p - propInfo) < numItems; )
    {
	/*
	 * Ignore leading NULs; each command or result starts with a
	 * NUL so that no matter how badly formed a preceding command
	 * is, we'll be able to tell that a new command/result is
	 * starting.
	 */
	if (*p == 0)
	{
	    p++;
	    continue;
	}

	if ((*p == 'c' || *p == 'k') && (p[1] == 0))
	{
	    Window	resWindow;
	    char_u	*name, *script, *serial, *end, *res;
	    Bool	asKeys = *p == 'k';
	    garray_T	reply;
	    char_u	*enc;
            int err;

	    /*
	     * This is an incoming command from some other application.
	     * Iterate over all of its options.  Stop when we reach
	     * the end of the property or something that doesn't look
	     * like an option.
	     */
	    p += 2;
	    name = NULL;
	    resWindow = None;
	    serial = (char_u *)"";
	    script = NULL;
	    enc = NULL;
	    while ((long_u)(p - propInfo) < numItems && *p == '-')
	    {
		switch (p[1])
		{
		    case 'r':
			end = skipwhite(p + 2);
			resWindow = 0;
			while (vim_isxdigit(*end))
			{
			    resWindow = 16 * resWindow + (long_u)hex2nr(*end);
			    ++end;
			}
			if (end == p + 2 || *end != ' ')
			    resWindow = None;
			else
			{
			    p = serial = end + 1;
			}
			break;
		    case 'n':
			if (p[2] == ' ')
			    name = p + 3;
			break;
		    case 's':
			if (p[2] == ' ')
			    script = p + 3;
			break;
		    case 'E':
			if (p[2] == ' ')
			    enc = p + 3;
			break;
		}
		while (*p != 0)
		    p++;
		p++;
	    }

	    if (script == NULL || name == NULL)
		continue;

            /* remote_send() is not supported */
            if (asKeys)
                continue;

	    /*
	     * Initialize the result property, so that we're ready at any
	     * time if we need to return an error.
	     */
	    if (resWindow != None)
	    {
		ga_init2(&reply, 1, 100);
		ga_grow(&reply, 50 + STRLEN(p_enc));
		sprintf(reply.ga_data, "%cr%c-E %s%c-s %s%c-r ",
						   0, 0, p_enc, 0, serial, 0);
		reply.ga_len = 14 + STRLEN(p_enc) + STRLEN(serial);
	    }
            err = 0;
	    res = NULL;
	    if (serverName != NULL && STRICMP(name, serverName) == 0)
	    {
		script = serverConvert(enc, script, &tofree);
                if (usereval == NULL)
                {
                    err = -1;
                    res = NULL;
                }
                else
                {
                    err = usereval((char *)script, (char **)&res);
                }
                vim_free(tofree);
	    }
	    if (resWindow != None)
	    {
                if (!err)
                {
		    ga_concat(&reply, (res == NULL ? (char_u *)"" : res));
                }
                else
                {
		    ga_concat(&reply, (res == NULL ? (char_u *)"" : res));
		    ga_append(&reply, 0);
		    ga_concat(&reply, (char_u *)"-c 1");
		}
		ga_append(&reply, NUL);
		(void)AppendPropCarefully(dpy, resWindow, commProperty,
					   reply.ga_data, reply.ga_len);
		ga_clear(&reply);
	    }
	    vim_free(res);
	}
	else if (*p == 'r' && p[1] == 0)
	{
	    int		    serial, gotSerial;
	    char_u	    *res;
	    PendingCommand  *pcPtr;
	    char_u	    *enc;

	    /*
	     * This is a reply to some command that we sent out.  Iterate
	     * over all of its options.  Stop when we reach the end of the
	     * property or something that doesn't look like an option.
	     */
	    p += 2;
	    gotSerial = 0;
	    res = (char_u *)"";
	    code = 0;
	    enc = NULL;
	    while ((long_u)(p - propInfo) < numItems && *p == '-')
	    {
		switch (p[1])
		{
		    case 'r':
			if (p[2] == ' ')
			    res = p + 3;
			break;
		    case 'E':
			if (p[2] == ' ')
			    enc = p + 3;
			break;
		    case 's':
			if (sscanf((char *)p + 2, " %d", &serial) == 1)
			    gotSerial = 1;
			break;
		    case 'c':
			if (sscanf((char *)p + 2, " %d", &code) != 1)
			    code = 0;
			break;
		}
		while (*p != 0)
		    p++;
		p++;
	    }

	    if (!gotSerial)
		continue;

	    /*
	     * Give the result information to anyone who's
	     * waiting for it.
	     */
	    for (pcPtr = pendingCommands; pcPtr != NULL; pcPtr = pcPtr->nextPtr)
	    {
		if (serial != pcPtr->serial || pcPtr->result != NULL)
		    continue;

		pcPtr->code = code;
		if (res != NULL)
		{
		    res = serverConvert(enc, res, &tofree);
		    if (tofree == NULL)
			res = vim_strsave(res);
		    pcPtr->result = res;
		}
		else
		    pcPtr->result = vim_strsave((char_u *)"");
		break;
	    }
	}
	else if (*p == 'n' && p[1] == 0)
	{
	    Window	win = 0;
	    unsigned int u;
	    int		gotWindow;
	    char_u	*str;
	    char_u	*enc;

	    /*
	     * This is a (n)otification.  Sent with serverreply_send in VimL.
	     * Execute any autocommand and save it for later retrieval
	     */
	    p += 2;
	    gotWindow = 0;
	    str = (char_u *)"";
	    enc = NULL;
	    while ((long_u)(p - propInfo) < numItems && *p == '-')
	    {
		switch (p[1])
		{
		    case 'n':
			if (p[2] == ' ')
			    str = p + 3;
			break;
		    case 'E':
			if (p[2] == ' ')
			    enc = p + 3;
			break;
		    case 'w':
			if (sscanf((char *)p + 2, " %x", &u) == 1)
			{
			    win = u;
			    gotWindow = 1;
			}
			break;
		}
		while (*p != 0)
		    p++;
		p++;
	    }

	    if (!gotWindow)
		continue;

            /* not supported */
	}
	else
	{
	    /*
	     * Didn't recognize this thing.  Just skip through the next
	     * null character and try again.
	     * Even if we get an 'r'(eply) we will throw it away as we
	     * never specify (and thus expect) one
	     */
	    while (*p != 0)
		p++;
	    p++;
	}
    }
    XFree(propInfo);
}

static int
WaitForPend(void *p)
{
    PendingCommand *pending = (PendingCommand *) p;
    return pending->result != NULL;
}

static int
WindowValid(Display *dpy, Window w)
{
    Atom *plist;
    int num_prop;
    int i;
    XErrorHandler old_handler;

    got_x_error = False;
    old_handler = XSetErrorHandler(x_error_check);
    plist = XListProperties(dpy, w, &num_prop);
    XSync(dpy, False);
    XSetErrorHandler(old_handler);

    if (plist == NULL || got_x_error) {
        return False;
    }

    for (i = 0; i < num_prop; ++i) {
        if (plist[i] == vimProperty) {
            XFree(plist);
            return True;
        }
    }
    XFree(plist);
    return False;
}

static void
ServerWait(Display *dpy, Window w, EndCond endCond, void *endData, int seconds)
{
    time_t	    start;
    time_t	    now;
    XEvent	    event;
    XPropertyEvent *e = (XPropertyEvent *)&event;
#   define SEND_MSEC_POLL 50

    time(&start);
    while (endCond(endData) == 0)
    {
	time(&now);
	if (seconds >= 0 && (now - start) >= seconds)
	    break;
        if (!WindowValid(dpy, w))
            break;
        while (XEventsQueued(dpy, QueuedAfterReading) > 0)
        {
            XNextEvent(dpy, &event);
            if (event.type == PropertyNotify && e->window == commWindow)
                serverEventProc(dpy, &event);
        }
        usleep(SEND_MSEC_POLL);
    }
}

static int
AppendPropCarefully(Display *dpy, Window window, Atom property, char_u *value, int length)
{
    XErrorHandler old_handler;

    old_handler = XSetErrorHandler(x_error_check);
    got_x_error = False;
    XChangeProperty(dpy, window, property, XA_STRING, 8,
            PropModeAppend, value, length);
    XSync(dpy, False);
    XSetErrorHandler(old_handler);
    return got_x_error ? -1 : 0;
}

static int
x_error_check(Display * UNUSED(dpy), XErrorEvent * UNUSED(error_event))
{
    got_x_error = True;
    return 0;
}

static char_u *
serverConvert(char_u * UNUSED(client_enc), char_u *data, char_u **tofree)
{
    char_u	*res = data;
    *tofree = NULL;
    return res;
}

