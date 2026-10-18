# GeosOne DOS Installer (INSTALL.EXE) - GNU make, Open Watcom
#   make [WATCOM=/opt/watcom]
WATCOM ?= /opt/watcom
export WATCOM
OUT ?= build

CC   := $(WATCOM)/binl/wcc
LINK := $(WATCOM)/binl/wlink
COPT := -q -bt=dos -ms -0 -os -s -zq -wx -I$(WATCOM)/h
WCOPT := -q -bt=windows -ms -0 -zW -os -s -zq -w4 -we -I$(WATCOM)/h -I$(WATCOM)/h/win

OBJS := $(addprefix $(OUT)/,install.obj screen.obj ini.obj archive.obj)
WINOBJS := $(OUT)/wininstall.obj $(OUT)/winini.obj

all: $(OUT)/INSTALL.EXE $(OUT)/WINSTALL.EXE

$(OUT):
	mkdir -p $@

$(OUT)/%.obj: src/%.c src/gdi.h | $(OUT)
	$(CC) $(COPT) -fo=$@ $<

$(OUT)/INSTALL.EXE: $(OBJS)
	$(LINK) option quiet format dos name $@ option map=$(OUT)/INSTALL.map \
	  $(foreach o,$(OBJS),file $(o)) libpath $(WATCOM)/lib286/dos libpath $(WATCOM)/lib286 \
	  option stack=8k

$(OUT)/wininstall.obj: src/wininstall.c src/gdi.h | $(OUT)
	$(CC) $(WCOPT) -fo=$@ $<

$(OUT)/winini.obj: src/ini.c src/gdi.h | $(OUT)
	$(CC) $(WCOPT) -fo=$@ $<

$(OUT)/WINSTALL.EXE: $(WINOBJS)
	$(LINK) option quiet format windows name $@ option map=$(OUT)/WINSTALL.map \
	  $(foreach o,$(WINOBJS),file $(o)) libpath $(WATCOM)/lib286/win \
	  libpath $(WATCOM)/lib286 library windows,clibs \
	  option stack=8k,heapsize=4k,version=3.10

clean:
	rm -rf $(OUT)

.PHONY: all clean

# boot sector for the disk images (tools/boot/bs9x.asm -> tools/GdiDisk.cs)
bootsector: | $(OUT)
	nasm -f bin -o $(OUT)/bs9x.bin tools/boot/bs9x.asm
	python3 -c "import base64,re,sys; p='tools/GdiDisk.cs'; s=open(p).read(); b=base64.b64encode(open('$(OUT)/bs9x.bin','rb').read()).decode(); open(p,'w').write(re.sub(r'(FromBase64String\(\n\t\t\t\")[^\"]*', lambda m: m.group(1)+b, s))"

.PHONY: bootsector
