CFLAGS=-Wall -std=gnu99
LOADLIBES=-lm

sauger: sauger.c aes.c md5.c json.c miio.c
