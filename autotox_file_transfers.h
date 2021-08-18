
#ifndef MINITOX_FILE_TRANSFERS_H
#define MINITOX_FILE_TRANSFERS_H

#include <linux/limits.h>
#include <stdio.h>
#include <stdint.h>
#include <tox/tox.h>

#define KiB 1024
#define MiB 1048576       /* 1024^2 */
#define GiB 1073741824    /* 1024^3 */

#define MAX_FILES 32

#define MAX_STR_SIZE TOX_MAX_MESSAGE_LENGTH    /* must be >= TOX_MAX_MESSAGE_LENGTH */

/*******************************************************************************
 *
 * Utils
 *
 ******************************************************************************/

#define RESIZE(key, size_key, length) \
    if ((size_key) < (length + 1)) { \
        size_key = (length+1);\
        key = calloc(1, size_key);\
    }

#define LIST_FIND(_p, _condition) \
    for (;*(_p) != NULL;_p = &((*_p)->next)) { \
        if (_condition) { \
            break;\
        }\
    }\

#define CODE_ERASE_LINE    "\r\033[2K"
#define INDEX_TO_TYPE(idx) (idx % TALK_TYPE_COUNT)
#define INDEX_TO_NUM(idx)  (idx / TALK_TYPE_COUNT)
#define GEN_INDEX(num,type) (num * TALK_TYPE_COUNT + type)

#define PRINT(_fmt, ...) \
    fputs(CODE_ERASE_LINE,stdout);\
    printf(_fmt "\n", ##__VA_ARGS__);
    
typedef enum FILE_TRANSFER_STATE {
    FILE_TRANSFER_INACTIVE,
    FILE_TRANSFER_PAUSED,
    FILE_TRANSFER_PENDING,
    FILE_TRANSFER_STARTED,
} FILE_TRANSFER_STATE;

typedef enum FILE_TRANSFER_DIRECTION {
    FILE_TRANSFER_SEND,
    FILE_TRANSFER_RECV
} FILE_TRANSFER_DIRECTION;

struct FileTransfer {
    FILE *file;
    FILE_TRANSFER_STATE state;
    uint8_t file_type;
    char file_name[TOX_MAX_FILENAME_LENGTH + 1];
    char file_path[PATH_MAX + 1];    /* Not used by senders */
    double   bps;
    uint32_t filenumber;
    uint32_t friendnumber;
    size_t   index;
    uint64_t file_size;
    uint64_t position;
    time_t   last_line_progress;   /* The last time we updated the progress bar */
    uint32_t line_id;
    uint8_t  file_id[TOX_FILE_ID_LENGTH];
};

struct Friend {
    uint32_t friend_num;
    char *name;
    char *status_message;
    uint8_t pubkey[TOX_PUBLIC_KEY_SIZE];
    TOX_CONNECTION connection;
    
    struct ChatHist *hist;
    struct FileTransfer file_receiver[MAX_FILES];
    struct FileTransfer file_sender[MAX_FILES];
    struct Friend *next;
};

/* Returns a pointer to friendnumber's FileTransfer struct associated with filenumber.
 * Returns NULL if filenumber is invalid.
 */
struct FileTransfer *get_file_transfer_struct(struct Friend *f, uint32_t filenumber);

/* Returns a pointer to the FileTransfer struct associated with index with the direction specified.
 * Returns NULL on failure.
 */
struct FileTransfer *get_file_transfer_struct_index(struct Friend *f, uint32_t index,
        FILE_TRANSFER_DIRECTION direction);
        
/* Initializes an unused file transfer and returns its pointer.
 * Returns NULL on failure.
 */
struct FileTransfer *new_file_transfer(struct Friend *f, uint32_t friendnumber, uint32_t filenumber,
                                       FILE_TRANSFER_DIRECTION direction, uint8_t type);

/* Closes file transfer ft.
 *
 * Set CTRL to -1 if we don't want to send a control signal.
 * Set message or self to NULL if we don't want to display a message.
 */
void close_file_transfer(Tox *m, struct FileTransfer *ft, int CTRL, const char *message);

#endif /* MINITOX_FILE_TRANSFERS_H */
