#ifndef MESSAGE_STORE_H
#define MESSAGE_STORE_H

#include <stddef.h>

int listen_cmd(int argc, char **argv);
int memory_cmd(int argc, char **argv);
int filter_cmd(int argc, char **argv);
int favoris_cmd(int argc, char **argv);

int app_message_passes_filters(const char *msg);
void app_store_message(const char *msg);

#endif /* MESSAGE_STORE_H */
