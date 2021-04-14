# pkg_add sdcc

ASZ80	?= sdasz80 -l
SDCC	?= sdcc -mz80

CFLAGS	+= -Wall

all: wsloader.bin

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
