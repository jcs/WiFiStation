# pkg_add sdcc

ASZ80	?= sdasz80 -l
SDCC	?= sdcc -mz80

CFLAGS	+= -Wall

all: wsloader.bin flashloader.bin

clean:
	rm -f *.{bin,ihx,lk,lst,map,noi,rel}

# parallel loader
wsloader.rel: wsloader.asm
	$(ASZ80) -o $@ $<

wsloader.ihx: wsloader.rel
	$(SDCC) --no-std-crt0 -o $@ $<

wsloader.bin: wsloader.ihx
	objcopy -Iihex -Obinary $< $@
	hexdump -C $@

# dataflash loader
flashloader.rel: flashloader.asm
	$(ASZ80) -o $@ $<

flashloader.ihx: flashloader.rel
	$(SDCC) --no-std-crt0 -o $@ $<

flashloader.bin: flashloader.ihx
	objcopy -Iihex -Obinary $< $@
