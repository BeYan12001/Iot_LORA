#ifndef RADIO_COMMANDS_H
#define RADIO_COMMANDS_H

#include "net/netdev.h"
#include "sx127x_internal.h"

int init_sx1272_cmd(int argc, char **argv);
int lora_setup_cmd(int argc, char **argv);
int random_cmd(int argc, char **argv);
int register_cmd(int argc, char **argv);
int send_cmd(int argc, char **argv);
int syncword_cmd(int argc, char **argv);
int channel_cmd(int argc, char **argv);
int rx_timeout_cmd(int argc, char **argv);
int reset_cmd(int argc, char **argv);
int crc_cmd(int argc, char **argv);
int implicit_cmd(int argc, char **argv);
int payload_cmd(int argc, char **argv);

netdev_t *app_netdev(void);
sx127x_t *app_radio(void);
int app_send_payload(char *payload);

#endif /* RADIO_COMMANDS_H */
