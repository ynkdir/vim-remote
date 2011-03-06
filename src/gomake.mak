include $(GOROOT)/src/Make.inc

TARGEXE = vimremotego

ifeq ($(GOOS),windows)
TARGEXE:=$(TARGEXE).exe
CGO_OFILES = vimremote_win32.o vimthings.o gocallback.o
CGO_LDFLAGS =
else
CGO_OFILES = vimremote_x11.o vimthings.o gocallback.o
CGO_LDFLAGS = -lX11
endif

TARG = main
CGOFILES = main.go

myall: package $(TARGEXE)

vimremotego: $(CGOFILES) $(CGO_OFILES)
	$(LD) -o $@ _obj/$(TARG).a

.c.o:
	$(CC) -o $@ -c $<

include $(GOROOT)/src/Make.pkg
