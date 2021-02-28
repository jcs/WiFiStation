# pkg_add sdcc

ASZ80	?= sdasz80 -l
SDCC	?= sdcc -mz80

CFLAGS	+= -Wall

all: loader.bin

clean:
	rm -f *.{bin,ihx,lk,lst,map,noi,rel}

# parallel loader
loader.rel: loader.asm
	$(ASZ80) -o $@ $>

loader.ihx: loader.rel
	$(SDCC) --no-std-crt0 -o $@ $>

loader.bin: loader.ihx
	objcopy -Iihex -Obinary $> $@
	hexdump -C $@
