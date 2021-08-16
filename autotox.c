/*
 * AutoTox 
 */

#ifndef _POSIX_C_SOURCE
#define _POSIX_C_SOURCE 200809L
#endif

#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <stdarg.h>

#include <termios.h>
#include <unistd.h>
#include <fcntl.h>

#include <tox/tox.h>
#include "autotox_file_transfers.h"

/*******************************************************************************
 *
 * Consts & Macros
 *
 ******************************************************************************/

// where to save the tox data.
// if don't want to save, set it to NULL.
const char *savedata_filename = "./savedata.tox";
const char *savedata_tmp_filename = "./savedata.tox.tmp";



#define LINE_MAX_SIZE 512  // If input line's length surpassed this value, it will be truncated.

#define PORT_RANGE_START 33445     // tox listen port range
#define PORT_RANGE_END   34445

#define AREPL_INTERVAL  30  // Async REPL iterate interval. unit: millisecond.

#define DEFAULT_CHAT_HIST_COUNT  20 // how many items of chat history to show by default;

#define SAVEDATA_AFTER_COMMAND true // whether save data after executing any command

/// Macros for terminal display



#define RESET_COLOR        "\x01b[0m"
#define SELF_TALK_COLOR    "\x01b[35m"  // magenta
#define GUEST_TALK_COLOR   "\x01b[90m" // bright black
#define CMD_PROMPT_COLOR   "\x01b[34m" // blue

#define CMD_PROMPT   CMD_PROMPT_COLOR "> " RESET_COLOR // green
#define FRIEND_TALK_PROMPT  CMD_PROMPT_COLOR "%-.12s << " RESET_COLOR

#define GUEST_MSG_PREFIX  GUEST_TALK_COLOR "%s  %12.12s | " RESET_COLOR
#define SELF_MSG_PREFIX  SELF_TALK_COLOR "%s  %12.12s | " RESET_COLOR
#define CMD_MSG_PREFIX  CMD_PROMPT



#define COLOR_PRINT(_color, _fmt,...) PRINT(_color _fmt RESET_COLOR, ##__VA_ARGS__)

#define INFO(_fmt,...) COLOR_PRINT("\x01b[36m", _fmt, ##__VA_ARGS__)  // cyran
#define WARN(_fmt,...) COLOR_PRINT("\x01b[33m", _fmt, ##__VA_ARGS__) // yellow
#define ERROR(_fmt,...) COLOR_PRINT("\x01b[31m", _fmt, ##__VA_ARGS__) // red


/*******************************************************************************
 *
 * Headers
 *
 ******************************************************************************/

Tox *tox;

typedef void CommandHandler(int narg, char **args);

struct Command {
    char* name;
    char* desc;
    int   narg;
    CommandHandler *handler;
};


struct FriendUserData {
    uint8_t pubkey[TOX_PUBLIC_KEY_SIZE];
};

union RequestUserData {
    struct FriendUserData friend;
};

struct Request {
    char *msg;
    uint32_t id;
    bool is_friend_request;
    union RequestUserData userdata;
    struct Request *next;
};

struct ChatHist {
    char *msg;
    struct ChatHist *next;
    struct ChatHist *prev;
};





int NEW_STDIN_FILENO = STDIN_FILENO;

struct Request *requests = NULL;

struct Friend *friends = NULL;
struct Friend self;


enum TALK_TYPE { TALK_TYPE_FRIEND, TALK_TYPE_COUNT, TALK_TYPE_NULL = UINT32_MAX };

uint32_t TalkingTo = TALK_TYPE_NULL;




bool str2uint(char *str, uint32_t *num) {
    char *str_end;
    long l = strtol(str,&str_end,10);
    if (str_end == str || l < 0 ) return false;
    *num = (uint32_t)l;
    return true;
}

char* genmsg(struct ChatHist **pp, const char *fmt, ...) {
    va_list va;
    va_start(va, fmt);

    va_list va2;
    va_copy(va2, va);
    size_t len = vsnprintf(NULL, 0, fmt, va2);
    va_end(va2);

    struct ChatHist *h = malloc(sizeof(struct ChatHist));
    h->prev = NULL;
    h->next = (*pp);
    if (*pp) (*pp)->prev = h;
    *pp = h;
    h->msg = malloc(len+1);

    vsnprintf(h->msg, len+1, fmt, va);
    va_end(va);

    return h->msg;
}

char* getftime(void) {
    static char timebuf[64];

    time_t tt = time(NULL);
    struct tm *tm = localtime(&tt);
    strftime(timebuf, sizeof(timebuf), "%H:%M:%S", tm);
    return timebuf;
}

const char * connection_enum2text(TOX_CONNECTION conn) {
    switch (conn) {
        case TOX_CONNECTION_NONE:
            return "Offline";
        case TOX_CONNECTION_TCP:
            return "Online(TCP)";
        case TOX_CONNECTION_UDP:
            return "Online(UDP)";
        default:
            return "UNKNOWN";
    }
}

/* Converts bytes to appropriate unit and puts in buf as a string */
void bytes_convert_str(char *buf, int size, uint64_t bytes)
{
    double conv = bytes;
    const char *unit;

    if (conv < KiB) {
        unit = "Bytes";
    } else if (conv < MiB) {
        unit = "KiB";
        conv /= (double) KiB;
    } else if (conv < GiB) {
        unit = "MiB";
        conv /= (double) MiB;
    } else {
        unit = "GiB";
        conv /= (double) GiB;
    }

    snprintf(buf, size, "%.1f %s", conv, unit);
}

static bool valid_file_name(const char *filename, size_t length)
{
    if (length == 0) {
        return false;
    }

    if (filename[0] == ' ' || filename[0] == '-') {
        return false;
    }

    if (strcmp(filename, ".") == 0 || strcmp(filename, "..") == 0) {
        return false;
    }

    for (size_t i = 0; i < length; ++i) {
        if (filename[i] == '/') {
            return false;
        }
    }

    return true;
}

struct Friend *getfriend(uint32_t friend_num) {
    struct Friend **p = &friends;
    LIST_FIND(p, (*p)->friend_num == friend_num);
    return *p;
}

struct Friend *addfriend(uint32_t friend_num) {
    struct Friend *f = calloc(1, sizeof(struct Friend));
    f->next = friends;
    friends = f;
    f->friend_num = friend_num;
    f->connection = TOX_CONNECTION_NONE;
    tox_friend_get_public_key(tox, friend_num, f->pubkey, NULL);
    return f;
}


bool delfriend(uint32_t friend_num) {
    struct Friend **p = &friends;
    LIST_FIND(p, (*p)->friend_num == friend_num);
    struct Friend *f = *p;
    if (f) {
        *p = f->next;
        if (f->name) free(f->name);
        if (f->status_message) free(f->status_message);
        while (f->hist) {
            struct ChatHist *tmp = f->hist;
            f->hist = f->hist->next;
            free(tmp);
        }
        free(f);
        return 1;
    }
    return 0;
}


uint8_t *hex2bin(const char *hex)
{
    size_t len = strlen(hex) / 2;
    uint8_t *bin = malloc(len);

    for (size_t i = 0; i < len; ++i, hex += 2) {
        sscanf(hex, "%2hhx", &bin[i]);
    }

    return bin;
}

char *bin2hex(const uint8_t *bin, size_t length) {
    char *hex = malloc(2*length + 1);
    char *saved = hex;
    for (int i=0; i<length;i++,hex+=2) {
        sprintf(hex, "%02X",bin[i]);
    }
    return saved;
}

struct ChatHist ** get_current_histp(void) {
    if (TalkingTo == TALK_TYPE_NULL) return NULL;
    uint32_t num = INDEX_TO_NUM(TalkingTo);
    switch (INDEX_TO_TYPE(TalkingTo)) {
        case TALK_TYPE_FRIEND: {
            struct Friend *f = getfriend(num);
            if (f) return &f->hist;
            break;
        }
       
    }
    return NULL;
}


/*******************************************************************************
 *
 * Tox Callbacks
 *
 ******************************************************************************/

void friend_message_cb(Tox *tox, uint32_t friend_num, TOX_MESSAGE_TYPE type, const uint8_t *message,
                                   size_t length, void *user_data)
{
    struct Friend *f = getfriend(friend_num);
    if (!f) return;
    if (type != TOX_MESSAGE_TYPE_NORMAL) {
        INFO("* receive MESSAGE ACTION type from %s, no supported", f->name);
        return;
    }

    char *msg = genmsg(&f->hist, GUEST_MSG_PREFIX "%.*s", getftime(), f->name, (int)length, (char*)message);
    if (GEN_INDEX(friend_num, TALK_TYPE_FRIEND) == TalkingTo) {
        PRINT("%s", msg);
    } else {
        INFO("* receive message from %s, autoreplied to remote\n",f->name);
        char *msg="autoreply";
         tox_friend_send_message(tox, friend_num, TOX_MESSAGE_TYPE_NORMAL, (uint8_t*)msg, strlen(msg), NULL);
    }
}

void friend_name_cb(Tox *tox, uint32_t friend_num, const uint8_t *name, size_t length, void *user_data) {
    struct Friend *f = getfriend(friend_num);

    if (f) {
        f->name = realloc(f->name, length+1);
        sprintf(f->name, "%.*s", (int)length, (char*)name);
        if (GEN_INDEX(friend_num, TALK_TYPE_FRIEND) == TalkingTo) {
            INFO("* Opposite changed name to %.*s", (int)length, (char*)name)
            //sprintf(async_repl->prompt, FRIEND_TALK_PROMPT, f->name);
        }
    }
}

void friend_status_message_cb(Tox *tox, uint32_t friend_num, const uint8_t *message, size_t length, void *user_data) {
    struct Friend *f = getfriend(friend_num);
    if (f) {
        f->status_message = realloc(f->status_message, length + 1);
        sprintf(f->status_message, "%.*s",(int)length, (char*)message);
    }
}

void friend_connection_status_cb(Tox *tox, uint32_t friend_num, TOX_CONNECTION connection_status, void *user_data)
{
    struct Friend *f = getfriend(friend_num);
    if (f) {
        f->connection = connection_status;
        INFO("* %s is %s", f->name, connection_enum2text(connection_status));
    }
}

void friend_request_cb(Tox *tox, const uint8_t *public_key, const uint8_t *message, size_t length, void *user_data) {
    INFO("* receive friend request(use `/accept` to see).");

    struct Request *req = malloc(sizeof(struct Request));

    req->id = 1 + ((requests != NULL) ? requests->id : 0);
    req->is_friend_request = true;
    memcpy(req->userdata.friend.pubkey, public_key, TOX_PUBLIC_KEY_SIZE);
    req->msg = malloc(length + 1);
    sprintf(req->msg, "%.*s", (int)length, (char*)message);

    req->next = requests;
    requests = req;
}

void self_connection_status_cb(Tox *tox, TOX_CONNECTION connection_status, void *user_data)
{
    self.connection = connection_status;
    INFO("* You are %s", connection_enum2text(connection_status));
}



void onFileRecv(Tox *m, uint32_t friendnum, uint32_t filenumber, uint64_t file_size,
                            const char *filename, size_t name_length);
                            
void on_file_recv_cb(Tox *tox, uint32_t friendnumber, uint32_t filenumber, uint32_t kind, uint64_t file_size,
                  const uint8_t *filename, size_t filename_length, void *userdata)
{
    //UNUSED_VAR(userdata);

    PRINT("File info: %d %d %d %ld %s %ld\n",friendnumber,filenumber,kind,file_size,filename,filename_length);
    onFileRecv(tox, friendnumber, filenumber, file_size, filename, filename_length);
}

static void onFileRecvChunk(Tox *m, uint32_t friendnum, uint32_t filenumber, uint64_t position,
                                 const char *data, size_t length)
{   
    PRINT("chat_onFileRecvChunk 0 ");
    struct Friend *f = getfriend(friendnum); 

    struct FileTransfer *ft = get_file_transfer_struct(f, filenumber);

    if (!ft) {
        return;
    }

    if (ft->state != FILE_TRANSFER_STARTED) {
        return;
    }
    
    char msg[MAX_STR_SIZE];
  
    if (length == 0) {
        snprintf(msg, sizeof(msg), "File '%s' successfully received.", ft->file_name);
        
        close_file_transfer(m, ft, -1, msg);
        return;
    }

    if (ft->file == NULL) {
        snprintf(msg, sizeof(msg), "File transfer for '%s' failed: Invalid file pointer.", ft->file_name);
        close_file_transfer( m, ft, TOX_FILE_CONTROL_CANCEL, msg);
        return;
    }

    if (fwrite(data, length, 1, ft->file) != 1) {
        snprintf(msg, sizeof(msg), "File transfer for '%s' failed: Write fail.", ft->file_name);
        close_file_transfer( m, ft, TOX_FILE_CONTROL_CANCEL, msg);
        return;
    }

    ft->bps += length;
    ft->position += length;
}

void on_file_recv_chunk_cb(Tox *m, uint32_t friendnumber, uint32_t filenumber, uint64_t position,
                        const uint8_t *data, size_t length, void *userdata)
{
    //UNUSED_VAR(userdata);

    //PRINT("File info: %d %d %ld %ld\n",friendnumber,filenumber,position,length);
    
    onFileRecvChunk(m, friendnumber, filenumber, position, (char *) data, length);
}

/*******************************************************************************
 *
 * Tox Setup
 *
 ******************************************************************************/

void create_tox(void)
{
    struct Tox_Options *options = tox_options_new(NULL);
    tox_options_set_start_port(options, PORT_RANGE_START);
    tox_options_set_end_port(options, PORT_RANGE_END);

    if (savedata_filename) {
        FILE *f = fopen(savedata_filename, "rb");
        if (f) {
            fseek(f, 0, SEEK_END);
            long fsize = ftell(f);
            fseek(f, 0, SEEK_SET);

            char *savedata = malloc(fsize);
            fread(savedata, fsize, 1, f);
            fclose(f);

            tox_options_set_savedata_type(options, TOX_SAVEDATA_TYPE_TOX_SAVE);
            tox_options_set_savedata_data(options, (uint8_t*)savedata, fsize);

            tox = tox_new(options, NULL);

            free(savedata);
        }
    }

    if (!tox) tox = tox_new(options, NULL);
    tox_options_free(options);
}

void init_friends(void) {
    size_t sz = tox_self_get_friend_list_size(tox);
    uint32_t *friend_list = malloc(sizeof(uint32_t) * sz);
    tox_self_get_friend_list(tox, friend_list);

    size_t len;

    for (int i = 0;i<sz;i++) {
        uint32_t friend_num = friend_list[i];
        struct Friend *f = addfriend(friend_num);

        len = tox_friend_get_name_size(tox, friend_num, NULL) + 1;
        f->name = calloc(1, len);
        tox_friend_get_name(tox, friend_num, (uint8_t*)f->name, NULL);

        len = tox_friend_get_status_message_size(tox, friend_num, NULL) + 1;
        f->status_message = calloc(1, len);
        tox_friend_get_status_message(tox, friend_num, (uint8_t*)f->status_message, NULL);

        tox_friend_get_public_key(tox, friend_num, f->pubkey, NULL);
    }
    free(friend_list);

    // add self
    self.friend_num = TALK_TYPE_NULL;
    len = tox_self_get_name_size(tox) + 1;
    self.name = calloc(1, len);
    tox_self_get_name(tox, (uint8_t*)self.name);

    len = tox_self_get_status_message_size(tox) + 1;
    self.status_message = calloc(1, len);
    tox_self_get_status_message(tox, (uint8_t*)self.status_message);

    tox_self_get_public_key(tox, self.pubkey);
}

void update_savedata_file(void)
{
    if (!(savedata_filename && savedata_tmp_filename)) return;

    size_t size = tox_get_savedata_size(tox);
    char *savedata = malloc(size);
    tox_get_savedata(tox, (uint8_t*)savedata);

    FILE *f = fopen(savedata_tmp_filename, "wb");
    fwrite(savedata, size, 1, f);
    fclose(f);

    rename(savedata_tmp_filename, savedata_filename);

    free(savedata);
}



void setup_tox(void)
{
    create_tox();
    init_friends();


    ////// register callbacks

    // self
    tox_callback_self_connection_status(tox, self_connection_status_cb);

    // friend
    tox_callback_friend_request(tox, friend_request_cb);
    tox_callback_friend_message(tox, friend_message_cb);
    tox_callback_friend_name(tox, friend_name_cb);
    tox_callback_friend_status_message(tox, friend_status_message_cb);
    tox_callback_friend_connection_status(tox, friend_connection_status_cb);

    //savefile
    tox_callback_file_recv(tox, on_file_recv_cb);
    //tox_callback_file_chunk_request(tox, on_file_chunk_request);
    //tox_callback_file_recv_control(tox, on_file_recv_control);
    tox_callback_file_recv_chunk(tox, on_file_recv_chunk_cb);
}

/*******************************************************************************
 *
 * Commands
 *
 ******************************************************************************/

void command_help(int narg, char **args);

void command_guide(int narg, char **args) {
    PRINT("This program is an minimal workable implementation of Tox client.");
    PRINT("As it pursued simplicity at the cost of robustness and efficiency,");
    PRINT("It should only be used for learning or playing with, instead of daily use.\n");

    PRINT("Commands are any input lines with leading `/`,");
    PRINT("Command args are seprated by blanks,");
    PRINT("while some special commands may accept any-character string, like `/setname` and `/setstmsg`.\n");

    PRINT("Use `/setname <YOUR NAME>` to set your name");
    PRINT("Use `/info` to see your Name, Tox Id and Network Connection.");
    PRINT("Use `/contacts` to list friends and groups, and use `/go <TARGET>` to talk to one of them.");
    PRINT("Finally, use `/help` to get a list of available commands.\n");

    PRINT("HAVE FUN!\n")
}

void _print_friend_info(struct Friend *f, bool is_self) {
    PRINT("%-15s%s", "Name:", f->name);

    if (is_self) {
        uint8_t tox_id_bin[TOX_ADDRESS_SIZE];
        tox_self_get_address(tox, tox_id_bin);
        char *hex = bin2hex(tox_id_bin, sizeof(tox_id_bin));
        PRINT("%-15s%s","Tox ID:", hex);
        free(hex);
    }

    char *hex = bin2hex(f->pubkey, sizeof(f->pubkey));
    PRINT("%-15s%s","Public Key:", hex);
    free(hex);
    PRINT("%-15s%s", "Status Msg:",f->status_message);
    PRINT("%-15s%s", "Network:",connection_enum2text(f->connection));
}

void command_info(int narg, char **args) {
    if (narg == 0) { // self
        _print_friend_info(&self, true);
        return;
    }

    uint32_t contact_idx;
    if (!str2uint(args[0],&contact_idx)) goto FAIL;

    uint32_t num = INDEX_TO_NUM(contact_idx);
    switch (INDEX_TO_TYPE(contact_idx)) {
        case TALK_TYPE_FRIEND: {
            struct Friend *f = getfriend(num);
            if (f) {
                _print_friend_info(f, false);
                return;
            }
            break;
        }
       
    }
FAIL:
    WARN("^ Invalid contact index");
}

void command_setname(int narg, char **args) {
    char *name = args[0];
    size_t len = strlen(name);
    TOX_ERR_SET_INFO err;
    tox_self_set_name(tox, (uint8_t*)name, strlen(name), &err);

    if (err != TOX_ERR_SET_INFO_OK) {
        ERROR("! set name failed, errcode:%d", err);
        return;
    }

    self.name = realloc(self.name, len + 1);
    strcpy(self.name, name);
}

void command_setstmsg(int narg, char **args) {
    char *status = args[0];
    size_t len = strlen(status);
    TOX_ERR_SET_INFO err;
    tox_self_set_status_message(tox, (uint8_t*)status, strlen(status), &err);
    if (err != TOX_ERR_SET_INFO_OK) {
        ERROR("! set status message failed, errcode:%d", err);
        return;
    }

    self.status_message = realloc(self.status_message, len+1);
    strcpy(self.status_message, status);
}

void command_add(int narg, char **args) {
    char *hex_id = args[0];
    char *msg = "";
    if (narg > 1) msg = args[1];

    uint8_t *bin_id = hex2bin(hex_id);
    TOX_ERR_FRIEND_ADD err;
    uint32_t friend_num = tox_friend_add(tox, bin_id, (uint8_t*)msg, strlen(msg), &err);
    free(bin_id);

    if (err != TOX_ERR_FRIEND_ADD_OK) {
        ERROR("! add friend failed, errcode:%d",err);
        return;
    }

    addfriend(friend_num);
}

void command_del(int narg, char **args) {
    uint32_t contact_idx;
    if (!str2uint(args[0], &contact_idx)) goto FAIL;
    uint32_t num = INDEX_TO_NUM(contact_idx);
    switch (INDEX_TO_TYPE(contact_idx)) {
        case TALK_TYPE_FRIEND:
            if (delfriend(num)) {
                tox_friend_delete(tox, num, NULL);
                return;
            }
            break;
        
    }
FAIL:
    WARN("^ Invalid contact index");
}

void command_contacts(int narg, char **args) {
    struct Friend *f = friends;
    PRINT("#Friends(conctact_index|name|connection|status message):\n");
    for (;f != NULL; f = f->next) {
        PRINT("%3d  %15.15s  %12.12s  %s",GEN_INDEX(f->friend_num, TALK_TYPE_FRIEND), f->name, connection_enum2text(f->connection), f->status_message);
    }

}

void command_save(int narg, char **args) {
    update_savedata_file();
}



void cmd_savefile(Tox *m, struct Friend *f, int argc, char **argv);
void command_savefile(int narg, char **args) {
    PRINT("Da go savefile");
    if (narg == 0) {
        WARN("Invalid args");
        return;
    }
    
    uint32_t num = INDEX_TO_NUM(TalkingTo);
    struct Friend *f = getfriend(num);
    PRINT("FriendNum in savefile %d",f->friend_num);
    
    cmd_savefile(tox,f,narg,args);
    return;
FAIL:
    WARN("^ Invalid file index");
        
}



void _command_accept(int narg, char **args, bool is_accept) {
    if (narg == 0) {
        struct Request * req = requests;
        for (;req != NULL;req=req->next) {
            PRINT("%-9u%-12s%s", req->id, (req->is_friend_request ? "FRIEND" : "GROUP"), req->msg);
        }
        return;
    }

    uint32_t request_idx;
    if (!str2uint(args[0], &request_idx)) goto FAIL;
    struct Request **p = &requests;
    LIST_FIND(p, (*p)->id == request_idx);
    struct Request *req = *p;
    if (req) {
        *p = req->next;
        if (is_accept) {
            if (req->is_friend_request) {
                TOX_ERR_FRIEND_ADD err;
                uint32_t friend_num = tox_friend_add_norequest(tox, req->userdata.friend.pubkey, &err);
                if (err != TOX_ERR_FRIEND_ADD_OK) {
                    ERROR("! accept friend request failed, errcode:%d", err);
                } else {
                    addfriend(friend_num);
                }
            } 
        }
        free(req->msg);
        free(req);
        return;
    }
FAIL:
    WARN("Invalid request index");
}

void command_accept(int narg, char **args) {
    _command_accept(narg, args, true);
}

void command_deny(int narg, char **args) {
    _command_accept(narg, args, false);
}





#define COMMAND_ARGS_REST 10
#define COMMAND_LENGTH (sizeof(commands)/sizeof(struct Command))

struct Command commands[] = {
    {
        "guide",
        "- print the guide",
        0,
        command_guide,
    },
    {
        "help",
        "- print this message.",
        0,
        command_help,
    },
    {
        "save",
        "- save your data.",
        0,
        command_save,
    },
    {
        "info",
        "[<contact_index>] - show one contact's info, or yourself's info if <contact_index> is empty. ",
        0 + COMMAND_ARGS_REST,
        command_info,
    },
    {
        "setname",
        "<name> - set your name",
        1,
        command_setname,
    },
    {
        "setstmsg",
        "<status_message> - set your status message.",
        1,
        command_setstmsg,
    },
    {
        "add",
        "<toxid> <msg> - add friend",
        2,
        command_add,
    },
    {
        "del",
        "<contact_index> - del a contact.",
        1,
        command_del,
    },
    {
        "contacts",
        "- list your contacts(friends and groups).",
        0,
        command_contacts,
    },
    {
        "savefile",
        "[<file_index>] - save file.",
        0 + COMMAND_ARGS_REST,
        command_savefile,
    },
    {
        "accept",
        "[<request_index>] - accept or list(if no <request_index> was provided) friend/group requests.",
        0 + COMMAND_ARGS_REST,
        command_accept,
    },
    {
        "deny",
        "[<request_index>] - deny or list(if no <request_index> was provided) friend/group requests.",
        0 + COMMAND_ARGS_REST,
        command_deny,
    },


};

void command_help(int narg, char **args){
    for (int i=1;i<COMMAND_LENGTH;i++) {
        printf("%-16s%s\n", commands[i].name, commands[i].desc);
    }
}


/*******************************************************************************
 *
 * SaveFile
 *
 ******************************************************************************/
 
void onFileRecv(Tox *m, uint32_t friendnum, uint32_t filenumber, uint64_t file_size, const char *filename, size_t name_length)
{
    struct Friend *f = getfriend(friendnum); 
    struct FileTransfer *ft = new_file_transfer(f,friendnum, filenumber, FILE_TRANSFER_RECV, TOX_FILE_KIND_DATA);

    if (!ft) {
        tox_file_control(m, friendnum, filenumber, TOX_FILE_CONTROL_CANCEL, NULL);
        WARN("File transfer request failed: Too many concurrent file transfers.");
        return;
    }

    PRINT("Friend muon gui file la: %s %d", f->name, f->friend_num);
    PRINT("FileTransfer la: %ld %d %d", ft->index, ft->friendnumber, ft->filenumber);
    
    char sizestr[32];
    bytes_convert_str(sizestr, sizeof(sizestr), file_size);
    PRINT ("File transfer request for '%s' (%s)", filename, sizestr);
    
    if (!valid_file_name(filename, name_length)) {
        close_file_transfer(m, ft, TOX_FILE_CONTROL_CANCEL, "File transfer failed: Invalid file name.");
        return;
    }

    size_t file_path_buf_size = PATH_MAX + name_length + 1;
    char *file_path = malloc(file_path_buf_size);

    if (file_path == NULL) {
        close_file_transfer(m, ft, TOX_FILE_CONTROL_CANCEL, "File transfer failed: Out of memory.");
        return;
    }
    
    size_t path_len = name_length;

    snprintf(file_path, file_path_buf_size, "%s", filename);


    if (path_len >= file_path_buf_size || path_len >= sizeof(ft->file_path) || name_length >= sizeof(ft->file_name)) {
        close_file_transfer(m, ft, TOX_FILE_CONTROL_CANCEL, "File transfer failed: File path too long.");
        free(file_path);
        return;
    }
    
    PRINT("Type '/savefile %zu' to accept the file transfer.", ft->index);
    
    char *mypath="/home/dungnt/abc.png";
    memset(file_path,0,PATH_MAX+1);
    memcpy(file_path,mypath,strlen(mypath));
    
    ft->file_size = file_size;
    snprintf(ft->file_path, sizeof(ft->file_path), "%s", file_path);
    snprintf(ft->file_name, sizeof(ft->file_name), "%s", filename);
    tox_file_get_file_id(m, friendnum, filenumber, ft->file_id, NULL);

    free(file_path);
    
}

 
 
void cmd_savefile(Tox *m, struct Friend *f, int argc, char **argv)
{
    if (argc < 1) {
        WARN("File ID required.");
        return;
    }

    long int idx = strtol(argv[0], NULL, 10);

    if ((idx == 0 && strcmp(argv[0], "0")) || idx < 0 || idx >= MAX_FILES) {
    	WARN("No pending file transfers with that ID.");
        return;
    }
    
    
    struct FileTransfer *ft = get_file_transfer_struct_index(f, idx, FILE_TRANSFER_RECV);

    if (!ft) {
        WARN("No pending file transfers with that ID.");
        return;
    }

    if (ft->state != FILE_TRANSFER_PENDING) {
        WARN("No pending file transfers with that ID.");
        return;
    }

    
    if ((ft->file = fopen(ft->file_path, "a")) == NULL) {
        const char *msg =  "File transfer failed: Invalid download path.";
        close_file_transfer(m, ft, TOX_FILE_CONTROL_CANCEL, msg);
        return;
    }

    Tox_Err_File_Control err;
    tox_file_control(m, f->friend_num, ft->filenumber, TOX_FILE_CONTROL_RESUME, &err);

    if (err != TOX_ERR_FILE_CONTROL_OK) {
        goto on_recv_error;
    }
    
    PRINT("Saving file [%ld] as: '%s'", idx, ft->file_path);
    
    //ft->line_id = self->chatwin->hst->line_end->id + 2;
    ft->state = FILE_TRANSFER_STARTED;
    PRINT ("OK id of file %ld %s",idx,ft->file_name);
    
    return;

on_recv_error:

    switch (err) {
        case TOX_ERR_FILE_CONTROL_FRIEND_NOT_FOUND:
            PRINT("File transfer failed: Friend not found.");
            return;

        case TOX_ERR_FILE_CONTROL_FRIEND_NOT_CONNECTED:
            PRINT("File transfer failed: Friend is not online.");
            return;

        case TOX_ERR_FILE_CONTROL_NOT_FOUND:
            PRINT("File transfer failed: Invalid filenumber.");
            return;

        case TOX_ERR_FILE_CONTROL_SENDQ:
            PRINT("File transfer failed: Connection error.");
            return;

        default:
            PRINT("File transfer failed (error %d)\n", err);
            return;
    }
}


/*******************************************************************************************************
/                                         LOGS
/*******************************************************************************************************/

void writetologfile(char *msg){
	char* pathlogfile="/home/dungnt/logminitox.txt";
	FILE *fp = fopen(pathlogfile, "a");
	/*struct timeval tv;

	gettimeofday(&tv, NULL);

	unsigned long long millisecondsSinceEpoch =
    (unsigned long long)(tv.tv_sec) * 1000 +
    (unsigned long long)(tv.tv_usec) / 1000;
	*/
	//fprintf(fp, " %s %llu\n",msg,millisecondsSinceEpoch);
	fprintf(fp, " %s\n",msg);
       // close the file
       fclose(fp);
}

/*******************************************************************************
 *
 * Main
 *
 ******************************************************************************/

char *poptok(char **strp) {
    static const char *dem = " \t";
    char *save = *strp;
    *strp = strpbrk(*strp, dem);
    if (*strp == NULL) return save;

    *((*strp)++) = '\0';
    *strp += strspn(*strp,dem);
    return save;
}


int main(int argc, char **argv) {
    if (argc == 2 && strcmp(argv[1], "--help") == 0) {
        fputs("Usage: autotox\n", stdout);
        fputs("\n", stdout);
        fputs("Autotox does not take any arguments.\n", stdout);
        return 0;
    }

 
    setup_tox();

    INFO("* Waiting to be online ...");

    uint32_t msecs = 0;
    while (1) {

        tox_iterate(tox, NULL);
        uint32_t v = tox_iteration_interval(tox);


        struct timespec pause;
        pause.tv_sec = 0;
        pause.tv_nsec = v * 1000 * 1000;
        nanosleep(&pause, NULL);
    }

    return 0;
}
