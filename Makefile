# Makefile for pfst

DEST=/usr/local/bin
CFLAGS=-O
OBJS=pfst.o

all: pfst
	$(CC) -o pfst $(OBJS)

clean:
	-rm -f *~ *.o pfst core \#*

install: pfst
	cp pfst $(DEST)
