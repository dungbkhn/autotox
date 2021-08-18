

#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include "autotox_file_transfers.h"


/* number of "#"'s in file transfer progress bar. Keep well below MAX_STR_SIZE */
#define NUM_PROG_MARKS 50
#define STR_BUF_SIZE 30

static void clear_file_transfer(struct FileTransfer *ft)
{
    *ft = (struct FileTransfer) {
        0
    };
}

/* Returns a pointer to friendnumber's FileTransfer struct associated with filenumber.
 * Returns NULL if filenumber is invalid.
 */
struct FileTransfer *get_file_transfer_struct(struct Friend *f, uint32_t filenumber)
{
    for (size_t i = 0; i < MAX_FILES; ++i) {
	 struct FileTransfer *ft_send = &f->file_sender[i];

        if (ft_send->state != FILE_TRANSFER_INACTIVE && ft_send->filenumber == filenumber) {
            return ft_send;
        }
        
        struct FileTransfer *ft_recv = &f->file_receiver[i];

        if (ft_recv->state != FILE_TRANSFER_INACTIVE && ft_recv->filenumber == filenumber) {
            return ft_recv;
        }
    }

    return NULL;
}

/* Returns a pointer to the FileTransfer struct associated with index with the direction specified.
 * Returns NULL on failure.
 */
struct FileTransfer *get_file_transfer_struct_index(struct Friend *f, uint32_t index,
        FILE_TRANSFER_DIRECTION direction)
{
    if (direction != FILE_TRANSFER_RECV && direction != FILE_TRANSFER_SEND) {
        return NULL;
    }

    for (size_t i = 0; i < MAX_FILES; ++i) {
        struct FileTransfer *ft = &f->file_receiver[i];

        if (ft->state != FILE_TRANSFER_INACTIVE && ft->index == index) {
            return ft;
        }
    }

    return NULL;
}



/* Returns a pointer to an unused file receiver.
 * Returns NULL if all file receivers are in use.
 */
static struct FileTransfer *new_file_receiver(struct Friend *f, uint32_t friendnumber, uint32_t filenumber,
        uint8_t type)
{
    
    for (size_t i = 0; i < MAX_FILES; ++i) {
        struct FileTransfer *ft = &f->file_receiver[i];
       
        if (ft->state == FILE_TRANSFER_INACTIVE) {
            clear_file_transfer(ft);
            ft->index = i;
            ft->friendnumber = friendnumber;
            ft->filenumber = filenumber;
            ft->file_type = type;
            ft->state = FILE_TRANSFER_PENDING;
            return ft;
        }
    }

    return NULL;
}

/* Returns a pointer to an unused file sender.
 * Returns NULL if all file senders are in use.
 */
static struct FileTransfer *new_file_sender(struct Friend *f, uint32_t friendnumber, uint32_t filenumber, uint8_t type)
{
    for (size_t i = 0; i < MAX_FILES; ++i) {
        struct FileTransfer *ft = &f->file_sender[i];

        if (ft->state == FILE_TRANSFER_INACTIVE) {
            clear_file_transfer(ft);
            //ft->window = window;
            ft->index = i;
            ft->friendnumber = friendnumber;
            ft->filenumber = filenumber;
            ft->file_type = type;
            ft->state = FILE_TRANSFER_PENDING;
            return ft;
        }
    }

    return NULL;
}

/* Initializes an unused file transfer and returns its pointer.
 * Returns NULL on failure.
 */
struct FileTransfer *new_file_transfer(struct Friend *f, uint32_t friendnumber, uint32_t filenumber,
                                       FILE_TRANSFER_DIRECTION direction, uint8_t type)
{
    if (direction == FILE_TRANSFER_RECV) {
        return new_file_receiver(f, friendnumber, filenumber, type);
    }

    if (direction == FILE_TRANSFER_SEND) {
        return new_file_sender(f, friendnumber, filenumber, type);
    }

    return NULL;
}

/* Closes file transfer ft.
 *
 * Set CTRL to -1 if we don't want to send a control signal.
 * Set message or self to NULL if we don't want to display a message.
 */
void close_file_transfer(Tox *m, struct FileTransfer *ft, int CTRL, const char *message)
{
    if (!ft) {
        return;
    }

    if (ft->state == FILE_TRANSFER_INACTIVE) {
        return;
    }

    if (ft->file) {
        fclose(ft->file);
    }

    if (CTRL >= 0) {
        tox_file_control(m, ft->friendnumber, ft->filenumber, (Tox_File_Control) CTRL, NULL);
    }

    if (message) {
        PRINT("%s", message);
    }

    clear_file_transfer(ft);
}
