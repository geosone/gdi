# GeosOne DOS Installer (INSTALL.EXE) - GNU make, Open Watcom
#   make [WATCOM=/opt/watcom]
WATCOM ?= /opt/watcom
export WATCOM
OUT ?= build

CC   := $(WATCOM)/binl/wcc
LINK := $(WATCOM)/binl/wlink
COPT := -q -bt=dos -ms -0 -os -s -zq -wx -I$(WATCOM)/h

OBJS := $(addprefix $(OUT)/,install.obj screen.obj ini.obj)

all: $(OUT)/INSTALL.EXE

$(OUT):
	mkdir -p $@

$(OUT)/%.obj: src/%.c src/gdi.h | $(OUT)
	$(CC) $(COPT) -fo=$@ $<

$(OUT)/INSTALL.EXE: $(OBJS)
	$(LINK) option quiet format dos name $@ option map=$(OUT)/INSTALL.map \
	  $(foreach o,$(OBJS),file $(o)) libpath $(WATCOM)/lib286/dos libpath $(WATCOM)/lib286 \
	  option stack=8k

clean:
	rm -rf $(OUT)

.PHONY: all clean
