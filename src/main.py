# encoding: utf-8

from __future__ import print_function

import sys
import os
import argparse
import time
from ctypes import CDLL, CFUNCTYPE, POINTER, byref, c_int, c_char_p, \
        c_void_p, memmove


ENCODING = "utf-8"


argument_parser = argparse.ArgumentParser()
argument_parser.add_argument("--serverlist", action="store_true",
        help="List available Vim server names")
argument_parser.add_argument("--servername",
        help="Vim server name")
argument_parser.add_argument("--remote-expr", dest="remoteexpr",
        help="Evaluate <expr> in a Vim server")
argument_parser.add_argument("--server", action="store_true",
        help="Start server")

if sys.platform == "win32":
    vimremote = CDLL(os.path.join(
        os.path.dirname(os.path.abspath(__file__)),
        "vimremote.dll"))
else:
    vimremote = CDLL(os.path.join(
        os.path.dirname(os.path.abspath(__file__)),
        "vimremote.so"))


def remote_expr(servername, expr):
    result = c_char_p()
    if sys.version_info[0] >= 3:
        servername = servername.encode(ENCODING)
        expr = expr.encode(ENCODING)
    ng = vimremote.vimremote_remoteexpr(servername, expr, byref(result))
    s = result.value
    if s is None:
        s = ""
    vimremote.vimremote_free(result)
    if ng:
        raise Exception("vimremote_remoteexpr() failed: " + s)
    return s


@CFUNCTYPE(c_int, c_char_p, POINTER(c_void_p))
def feval(expr, result):
    try:
        res = str(eval(expr))
        err = 0
    except Exception as e:
        res = str(e)
        err = -1
    if sys.version_info[0] >= 3:
        res = res.encode(ENCODING)
    result[0] = vimremote.vimremote_malloc(len(res) + 1)
    memmove(result[0], c_char_p(res), len(res) + 1)
    return err


def command_serverlist():
    servernames = c_char_p()
    if vimremote.vimremote_serverlist(byref(servernames)) != 0:
        raise Exception("vimremote_serverlist() failed")
    print(servernames.value, end='')
    vimremote.vimremote_free(servernames)


def command_remoteexpr(servername, expr):
    print(remote_expr(servername, expr))


def command_server(servername):
    if sys.version_info[0] >= 3:
        servername = servername.encode(ENCODING)
    if vimremote.vimremote_register(servername, feval) != 0:
        raise Exception("vimremote_register() failed")
    while True:
        vimremote.vimremote_eventloop(0)
        time.sleep(0.1)


def main():
    args = argument_parser.parse_args()

    if vimremote.vimremote_init() != 0:
        raise Exception("vimremote_init() failed")

    if args.serverlist:
        command_serverlist()
    elif args.remoteexpr is not None:
        if args.servername is None:
            raise Exception("remoteexpr requires servername")
        command_remoteexpr(args.servername, args.remoteexpr)
    elif args.server:
        if args.servername is None:
            raise Exception("server requires servername")
        command_server(args.servername)
    else:
        argument_parser.print_help()

    if vimremote.vimremote_uninit() != 0:
        raise Exception("vimremote_uninit() failed")


if __name__ == "__main__":
    main()
