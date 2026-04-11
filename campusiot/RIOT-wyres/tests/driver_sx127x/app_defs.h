#ifndef APP_DEFS_H
#define APP_DEFS_H

#include <stdint.h>

/* Application limits */
#define MAX_NAME_LEN            (4)
#define NAME_BUF_SIZE           (MAX_NAME_LEN + 1)
#define ADDRESS_BUF_SIZE        (MAX_NAME_LEN + 2)
#define MAX_CONTACTS            (40)
#define MAX_FAVORIS             (10)
#define MAX_LISTEN_FILTERS      (10)
#define MAX_STORED_MESSAGES     (100)
#define MESSAGE_BUF_SIZE        (128)
#define MAX_RELAY_TRACKED       (32)

typedef struct {
    char src[NAME_BUF_SIZE];
    char dst[NAME_BUF_SIZE];
    char sep;
    int counter;
    int ttl;
    int has_ttl;
    const char *body;
} routed_message_t;

#endif /* APP_DEFS_H */
