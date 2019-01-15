CFLAGS = -std=gnu99 -Wall -g
PROG = ext2_mkdir ext2_cp ext2_ln ext2_rm ext2_restore ext2_checker
SRC = ext2_mkdir.c ext2_cp.c ext2_ln.c ext2_rm.c ext2_restore.c ext2_checker.c

all: ext2_mkdir ext2_cp ext2_ln ext2_rm ext2_restore ext2_checker

ext2_mkdir: ext2_mkdir.c ext2.h ext2_functions.o
	gcc ${CFLAGS} -o $@ $< ext2_functions.o -lm

ext2_cp: ext2_cp.c ext2.h ext2_functions.o
	gcc ${CFLAGS} -o $@ $< ext2_functions.o -lm

ext2_ln: ext2_ln.c ext2.h ext2_functions.o
	gcc ${CFLAGS} -o $@ $< ext2_functions.o -lm

ext2_rm: ext2_rm.c ext2.h ext2_functions.o
	gcc ${CFLAGS} -o $@ $< ext2_functions.o -lm

ext2_restore: ext2_restore.c ext2.h ext2_functions.o
	gcc ${CFLAGS} -o $@ $< ext2_functions.o -lm

ext2_checker: ext2_checker.c ext2.h ext2_functions.o
	gcc ${CFLAGS} -o $@ $< ext2_functions.o -lm

ext2_functions.o: ext2_functions.c ext2_functions.h ext2.h
	gcc ${CFLAGS} -c -o $@ $<

clean:
	rm -rf $(PROG) *.dSYM *.o
