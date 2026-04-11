#ifndef RELAY_H
#define RELAY_H

#include <stdint.h>

int threshold_cmd(int argc, char **argv);
void app_maybe_relay_message(const char *rx_msg, int8_t snr);

#endif /* RELAY_H */
