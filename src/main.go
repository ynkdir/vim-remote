package main

/*
#include <stdlib.h>
#include <string.h>
#include "vimremote.h"
extern int vimremote_register_wrap(const char *servername);
*/
import "C"

import "flag"
import "unsafe"
import "go/token"
import "exp/eval"
import "fmt"

var fset = token.NewFileSet()

func remote_expr(servername string, expr string) (string) {
  a := C.CString(servername)
  defer C.free(unsafe.Pointer(a))
  b := C.CString(expr)
  defer C.free(unsafe.Pointer(b))
  var p [1]*C.char
  ng := C.vimremote_remoteexpr(a, b, &p[0])
  defer C.vimremote_free(unsafe.Pointer(p[0]))
  if ng != 0 {
    panic("vimremote_remoteexpr() failed: " + C.GoString(p[0]))
  }
  return C.GoString(p[0])
}

func command_serverlist() {
  var p [1]*C.char
  ng := C.vimremote_serverlist(&p[0])
  defer C.vimremote_free(unsafe.Pointer(p[0]))
  if ng != 0 {
    panic("vimremote_serverlist() failed")
  }
  print(C.GoString(p[0]))
}

func command_remotesend(servername string, keys string) {
  a := C.CString(servername)
  defer C.free(unsafe.Pointer(a))
  b := C.CString(keys)
  defer C.free(unsafe.Pointer(b))
  ng := C.vimremote_remotesend(a, b)
  if ng != 0 {
    panic("vimremote_remotesend() failed")
  }
}

func command_remoteexpr(servername string, expr string) {
  println(remote_expr(servername, expr))
}

func command_server(servername string) {
  a := C.CString(servername)
  defer C.free(unsafe.Pointer(a))
  // FIXME: How to pass callback function without separating file?
  if C.vimremote_register_wrap(a) != 0 {
    panic("vimremote_register() failed")
  }
  C.vimremote_eventloop(1)
}

type w_remote_expr struct {
  f *eval.Frame
}

func (self *w_remote_expr) NewFrame() *eval.Frame {
  self.f = &eval.Frame{nil, make([]eval.Value, 3)}
  return self.f
}

func (self *w_remote_expr) Call(t *eval.Thread) {
  servername := self.f.Get(0, 0).(eval.StringValue).Get(t)
  expr := self.f.Get(0, 1).(eval.StringValue).Get(t)
  var res string
  defer func() {
    if x := recover(); x != nil {
      self.f.Get(0, 2).(eval.StringValue).Set(t, fmt.Sprintf("%s", x))
    } else {
      self.f.Get(0, 2).(eval.StringValue).Set(t, res)
    }
  }()
  res = remote_expr(servername, expr)
}

type funcV struct {
  target eval.Func
}
func (v *funcV) String() string {
  return "func {...}"
}
func (v *funcV) Assign(t *eval.Thread, o eval.Value) { v.target = o.(eval.FuncValue).Get(t) }
func (v *funcV) Get(*eval.Thread) eval.Func { return v.target }
func (v *funcV) Set(t *eval.Thread, x eval.Func) { v.target = x }

//export GoEval
func GoEval(expr *C.char, result **C.char) (int) {
  var ret int
  func() {
    defer func() {
      if x := recover(); x != nil {
        ret = -1
        res := fmt.Sprintf("%s", x)
        p := C.CString(res)
        defer C.free(unsafe.Pointer(p))
        *result = (*C.char)(C.vimremote_malloc((C.size_t)(len(res) + 1)))
        C.memmove(unsafe.Pointer(*result), unsafe.Pointer(p), (C.size_t)(len(res) + 1))
      }
    }()
    ret = GoEval2(expr, result)
  }()
  return ret
}

func GoEval2(expr *C.char, result **C.char) (int) {
  var res string
  var ret int
  src := C.GoString(expr)
  world := eval.NewWorld()
  world.DefineVar("remote_expr", eval.NewFuncType([]eval.Type{eval.StringType, eval.StringType}, false, []eval.Type{eval.StringType}), &funcV{&w_remote_expr{}})
  code, err := world.Compile(fset, src)
  if err != nil {
    res = err.String()
    ret = -1
  } else {
    value, err := code.Run()
    if err != nil {
      res = err.String()
      ret = -1
    } else {
      res = value.String()
      ret = 0
    }
  }
  p := C.CString(res)
  defer C.free(unsafe.Pointer(p))
  *result = (*C.char)(C.vimremote_malloc((C.size_t)(len(res) + 1)))
  C.memmove(unsafe.Pointer(*result), unsafe.Pointer(p), (C.size_t)(len(res) + 1))
  return ret
}

func main() {
  serverlist := flag.Bool("serverlist", false, "List available Vim server names")
  servername := flag.String("servername", "", "Vim server name")
  remotesend := flag.String("remote-send", "", "Send <keys> to a Vim server and exit")
  remoteexpr := flag.String("remote-expr", "", "Evaluate <expr> in a Vim server")
  server := flag.Bool("server", false, "Start server")
  flag.Parse()

  if C.vimremote_init() != 0 {
    panic("vimremote_init() failed")
  }

  if *serverlist {
    command_serverlist()
  } else if *remotesend != "" {
    if *servername == "" {
      panic("remotesend requires servername")
    }
    command_remotesend(*servername, *remotesend)
  } else if *remoteexpr != "" {
    if *servername == "" {
      panic("remoteexpr requires servername")
    }
    command_remoteexpr(*servername, *remoteexpr)
  } else if *server {
    if *servername == "" {
      panic("server requires servername")
    }
    command_server(*servername)
  } else {
    flag.PrintDefaults()
  }

  if C.vimremote_uninit() != 0 {
    panic("vimremote_uninit() failed")
  }
}
