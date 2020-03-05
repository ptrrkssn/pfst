# Makefile for pfst

DEST=/usr/local/bin
CFLAGS=-O
OBJS=pfst.o

all: pfst

pfst: $(OBJS)
	$(CC) -o pfst $(OBJS)

clean:
	-rm -f *~ *.o pfst core \#*

install: pfst
	cp pfst $(DEST)

push:	clean
	git add -A && git commit -a

pull:
	git pull
