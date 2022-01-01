autotox: autotox.c
	gcc -Wall -D_FILE_OFFSET_BITS=64 -o autotox autotox.c autotox_file_transfers.c -ltoxcore
clean:
	-rm -f autotox
