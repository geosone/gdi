# GeosOne Dual Installer (INSTALL.EXE) - GNU make, Open Watcom
#   make [WATCOM=/opt/watcom]
WATCOM ?= /opt/watcom
export WATCOM
OUT ?= build
UPX_SRC := vendor/build-deps/upx
UPX_BUILD := $(OUT)/tools/upx
UPX := $(UPX_BUILD)/upx

CC   := $(WATCOM)/binl/wcc
LINK := $(WATCOM)/binl/wlink
COPT := -q -bt=dos -ms -0 -os -s -zq -wx -I$(WATCOM)/h
WCOPT := -q -bt=windows -ms -0 -zW -os -s -zq -w4 -we -I$(WATCOM)/h -I$(WATCOM)/h/win

OBJS := $(addprefix $(OUT)/,install.obj screen.obj ini.obj archive.obj hexpatch.obj regtxn.obj progman.obj)
WINOBJS := $(OUT)/wininstall.obj $(OUT)/winini.obj $(OUT)/winregtxn.obj $(OUT)/winreg16.obj $(OUT)/wintasks.obj
DOSIMAGE := $(OUT)/INSTALL.STB
DOSPACKED := $(OUT)/INSTALL.PST
WINIMAGE := $(OUT)/INSTALL.UNP

.DELETE_ON_ERROR:

all: $(OUT)/INSTALL.EXE

$(OUT):
	mkdir -p $@

$(OUT)/%.obj: src/%.c src/gdi.h src/hexpatch.h | $(OUT)
	$(CC) $(COPT) -fo=$@ $<

$(DOSIMAGE): $(OBJS)
	$(LINK) option quiet format dos name $@ option map=$(OUT)/INSTALL.map \
	  $(foreach o,$(OBJS),file $(o)) libpath $(WATCOM)/lib286/dos libpath $(WATCOM)/lib286 \
	  option stack=8k

upx:
	@test -f $(UPX_SRC)/vendor/ucl/src/ucl_init.c || { \
	  echo "UPX submodules are missing; run: git submodule update --init --recursive" >&2; exit 1; }
	cmake -S $(UPX_SRC) -B $(UPX_BUILD) -G Ninja \
	  -DCMAKE_BUILD_TYPE=Release -DUPX_CONFIG_DISABLE_GITREV=ON \
	  -DUPX_CONFIG_DISABLE_SELF_PACK_TEST=ON
	cmake --build $(UPX_BUILD) --target upx

$(DOSPACKED): $(DOSIMAGE) upx
	cp $< $@
	$(UPX) --best $@

$(OUT)/wininstall.obj: src/wininstall.c src/gdi.h src/wintasks.h | $(OUT)
	$(CC) $(WCOPT) -fo=$@ $<

$(OUT)/winini.obj: src/ini.c src/gdi.h | $(OUT)
	$(CC) $(WCOPT) -fo=$@ $<

$(OUT)/winregtxn.obj: src/regtxn.c src/regtxn.h | $(OUT)
	$(CC) $(WCOPT) -fo=$@ $<

$(OUT)/wintasks.obj: src/wintasks.c src/wintasks.h | $(OUT)
	$(CC) $(WCOPT) -fo=$@ $<

$(OUT)/winreg16.obj: src/winreg16.c src/winreg16.h src/regtxn.h | $(OUT)
	$(CC) $(WCOPT) -fo=$@ $<

$(WINIMAGE): $(DOSPACKED) $(WINOBJS)
	$(LINK) option quiet format windows name $@ option map=$(OUT)/INSTALLW.map \
	  $(foreach o,$(WINOBJS),file $(o)) libpath $(WATCOM)/lib286/win \
	  libpath $(WATCOM)/lib286 library windows,shell,ddeml,clibs \
	  option stack=8k,heapsize=4k,version=3.10 option stub=$(DOSPACKED)

$(OUT)/INSTALL.EXE: $(WINIMAGE) upx
	cp $< $@
	$(UPX) --best $@
	$(UPX) -t $@

clean:
	rm -rf $(OUT)

test-host:
	test/host.sh

.PHONY: all clean test-host upx

# boot sector for the disk images (tools/boot/bs9x.asm -> tools/GdiDisk.cs)
bootsector: | $(OUT)
	nasm -f bin -o $(OUT)/bs9x.bin tools/boot/bs9x.asm
	python3 -c "import base64,re,sys; p='tools/GdiDisk.cs'; s=open(p).read(); b=base64.b64encode(open('$(OUT)/bs9x.bin','rb').read()).decode(); open(p,'w').write(re.sub(r'(FromBase64String\(\n\t\t\t\")[^\"]*', lambda m: m.group(1)+b, s))"

.PHONY: bootsector
