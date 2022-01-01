minitox: autotox.c
	$(CC) -Wall -D_FILE_OFFSET_BITS=64 -std=c99 -o autotox autotox.c autotox_file_transfers.c -ltoxcore
clean:
	-rm -f autotox
