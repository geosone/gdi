# GeosOne DOS Installer (INSTALL.EXE) - GNU make, Open Watcom
#   make [WATCOM=/opt/watcom]
WATCOM ?= /opt/watcom
export WATCOM
OUT ?= build

CC   := $(WATCOM)/binl/wcc
LINK := $(WATCOM)/binl/wlink
COPT := -q -bt=dos -ms -0 -os -s -zq -wx -I$(WATCOM)/h

OBJS := $(addprefix $(OUT)/,install.obj screen.obj ini.obj archive.obj)

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

# boot sector for the disk images (tools/boot/bs9x.asm -> tools/GdiDisk.cs)
bootsector: | $(OUT)
	nasm -f bin -o $(OUT)/bs9x.bin tools/boot/bs9x.asm
	python3 -c "import base64,re,sys; p='tools/GdiDisk.cs'; s=open(p).read(); b=base64.b64encode(open('$(OUT)/bs9x.bin','rb').read()).decode(); open(p,'w').write(re.sub(r'(FromBase64String\(\n\t\t\t\")[^\"]*', lambda m: m.group(1)+b, s))"

.PHONY: bootsector
