minitox: autotox.c
	$(CC) -std=c99 -o autotox autotox.c autotox_file_transfers.c -ltoxcore
clean:
	-rm -f autotox
