CC=gcc
CFLAGS=-Wall
DEPS = ext2.h ext2_helper.h

all : ext2_cp ext2_mkdir ext2_ln ext2_rm ext2_restore ext2_checker

ext2_cp : ext2_cp.c ext2_helper.c $(DEPS)
	$(CC) $(CFLAGS) -o ext2_cp ext2_cp.c -I.

ext2_mkdir : ext2_mkdir.c ext2_helper.c $(DEPS)
	$(CC) $(CFLAGS) -o ext2_mkdir ext2_mkdir.c -I.

ext2_ln : ext2_ln.c ext2_helper.c $(DEPS)
	$(CC) $(CFLAGS) -o ext2_ln ext2_ln.c -I.

ext2_rm : ext2_rm.c ext2_helper.c $(DEPS)
	$(CC) $(CFLAGS) -o ext2_rm ext2_rm.c -I.

ext2_restore : ext2_restore.c ext2_helper.c $(DEPS)
	$(CC) $(CFLAGS) -o ext2_restore ext2_restore.c -I.

ext2_checker : ext2_checker.c ext2_helper.c $(DEPS)
	$(CC) $(CFLAGS) -o ext2_checker ext2_checker.c -I.
