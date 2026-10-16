.PHONY: all paku outrun clean

all: paku

paku:
	zcc +gal -create-app -pragma-redirect:fputc_cons=fputc_cons_generic -o paku paku.c

outrun:
	zcc +gal -create-app -pragma-redirect:fputc_cons=fputc_cons_generic -o outrun outrun.c

clean:
	rm -f paku paku.gtp paku.wav paku.o outrun outrun.gtp outrun.wav outrun.o *.bin *.tap
