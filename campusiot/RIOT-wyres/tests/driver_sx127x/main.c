/*
 * Copyright (C) 2016 Unwired Devices <info@unwds.com>
 *               2017 Inria Chile
 *               2017 Inria
 *
 * This file is subject to the terms and conditions of the GNU Lesser
 * General Public License v2.1. See the file LICENSE in the top level
 * directory for more details.
 */

/**
 * @ingroup     tests
 * @{
 * @file
 * @brief       Test application for SX127X modem driver
 *
 * @author      Eugene P. <ep@unwds.com>
 * @author      José Ignacio Alamos <jose.alamos@inria.cl>
 * @author      Alexandre Abadie <alexandre.abadie@inria.fr>
 * @}
 */

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <inttypes.h>

#include "thread.h"
#include "shell.h"
//#include "shell_commands.h"

#include "net/netdev.h"
#include "net/netdev/lora.h"
#include "net/lora.h"

#include "board.h"

#include "sx127x_internal.h"
#include "sx127x_params.h"
#include "sx127x_netdev.h"

#include "fmt.h"

#define SX127X_LORA_MSG_QUEUE   (16U)
#ifndef SX127X_STACKSIZE
#define SX127X_STACKSIZE        (THREAD_STACKSIZE_DEFAULT)
#endif

#define MSG_TYPE_ISR            (0x3456)

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

static char stack[SX127X_STACKSIZE];
static kernel_pid_t _recv_pid;

static char message[MESSAGE_BUF_SIZE];
static sx127x_t sx127x;

typedef struct {
    char src[NAME_BUF_SIZE];
    char dst[NAME_BUF_SIZE];
    char sep;
    int counter;
    int ttl;
    int has_ttl;
    const char *body;
} routed_message_t;

typedef struct {
    char src[NAME_BUF_SIZE];
    char dst[NAME_BUF_SIZE];
    char sep;
    int counter;
    int8_t best_snr;
    uint8_t relayed;
    uint8_t used;
} relay_entry_t;

static relay_entry_t relay_entries[MAX_RELAY_TRACKED];
static int8_t snr_threshold = 0;

static int _send_payload(char *payload);
static int _parse_routed_message(const char *msg, routed_message_t *parsed);
static relay_entry_t *_find_relay_entry(const routed_message_t *parsed);
static relay_entry_t *_get_or_create_relay_entry(const routed_message_t *parsed);
static void _maybe_relay_message(const char *rx_msg, int8_t snr);

int lora_setup_cmd(int argc, char **argv)
{

    if (argc < 4) {
        puts("usage: setup "
             "<bandwidth (125, 250, 500)> "
             "<spreading factor (7..12)> "
             "<code rate (5..8)>");
        return -1;
    }

    /* Check bandwidth value */
    int bw = atoi(argv[1]);
    uint8_t lora_bw;

    switch (bw) {
    case 125:
        puts("setup: setting 125KHz bandwidth");
        lora_bw = LORA_BW_125_KHZ;
        break;

    case 250:
        puts("setup: setting 250KHz bandwidth");
        lora_bw = LORA_BW_250_KHZ;
        break;

    case 500:
        puts("setup: setting 500KHz bandwidth");
        lora_bw = LORA_BW_500_KHZ;
        break;

    default:
        puts("[Error] setup: invalid bandwidth value given, "
             "only 125, 250 or 500 allowed.");
        return -1;
    }

    /* Check spreading factor value */
    uint8_t lora_sf = atoi(argv[2]);

    if (lora_sf < 7 || lora_sf > 12) {
        puts("[Error] setup: invalid spreading factor value given");
        return -1;
    }

    /* Check coding rate value */
    int cr = atoi(argv[3]);

    if (cr < 5 || cr > 8) {
        puts("[Error ]setup: invalid coding rate value given");
        return -1;
    }
    uint8_t lora_cr = (uint8_t)(cr - 4);

    /* Configure radio device */
    netdev_t *netdev = &sx127x.netdev;

    netdev->driver->set(netdev, NETOPT_BANDWIDTH,
                        &lora_bw, sizeof(lora_bw));
    netdev->driver->set(netdev, NETOPT_SPREADING_FACTOR,
                        &lora_sf, sizeof(lora_sf));
    netdev->driver->set(netdev, NETOPT_CODING_RATE,
                        &lora_cr, sizeof(lora_cr));

    puts("[Info] setup: configuration set with success");

    return 0;
}

int random_cmd(int argc, char **argv)
{
    (void)argc;
    (void)argv;

    netdev_t *netdev = &sx127x.netdev;
    uint32_t rand;

    netdev->driver->get(netdev, NETOPT_RANDOM, &rand, sizeof(rand));
    printf("random: number from sx127x: %u\n",
           (unsigned int)rand);

    /* reinit the transceiver to default values */
    sx127x_init_radio_settings(&sx127x);

    return 0;
}

int register_cmd(int argc, char **argv)
{
    if (argc < 2) {
        puts("usage: register <get | set>");
        return -1;
    }

    if (strstr(argv[1], "get") != NULL) {
        if (argc < 3) {
            puts("usage: register get <all | allinline | regnum>");
            return -1;
        }

        if (strcmp(argv[2], "all") == 0) {
            puts("- listing all registers -");
            uint8_t reg = 0, data = 0;
            /* Listing registers map */
            puts("Reg   0  1  2  3  4  5  6  7  8  9  A  B  C  D  E  F");
            for (unsigned i = 0; i <= 7; i++) {
                printf("0x%02X ", i << 4);

                for (unsigned j = 0; j <= 15; j++, reg++) {
                    data = sx127x_reg_read(&sx127x, reg);
                    printf("%02X ", data);
                }
                puts("");
            }
            puts("-done-");
            return 0;
        }
        else if (strcmp(argv[2], "allinline") == 0) {
            puts("- listing all registers in one line -");
            /* Listing registers map */
            for (uint16_t reg = 0; reg < 256; reg++) {
                printf("%02X ", sx127x_reg_read(&sx127x, (uint8_t)reg));
            }
            puts("- done -");
            return 0;
        }
        else {
            long int num = 0;
            /* Register number in hex */
            if (strstr(argv[2], "0x") != NULL) {
                num = strtol(argv[2], NULL, 16);
            }
            else {
                num = atoi(argv[2]);
            }

            if (num >= 0 && num <= 255) {
                printf("[regs] 0x%02X = 0x%02X\n",
                       (uint8_t)num,
                       sx127x_reg_read(&sx127x, (uint8_t)num));
            }
            else {
                puts("regs: invalid register number specified");
                return -1;
            }
        }
    }
    else if (strstr(argv[1], "set") != NULL) {
        if (argc < 4) {
            puts("usage: register set <regnum> <value>");
            return -1;
        }

        long num, val;

        /* Register number in hex */
        if (strstr(argv[2], "0x") != NULL) {
            num = strtol(argv[2], NULL, 16);
        }
        else {
            num = atoi(argv[2]);
        }

        /* Register value in hex */
        if (strstr(argv[3], "0x") != NULL) {
            val = strtol(argv[3], NULL, 16);
        }
        else {
            val = atoi(argv[3]);
        }

        sx127x_reg_write(&sx127x, (uint8_t)num, (uint8_t)val);
    }
    else {
        puts("usage: register get <all | allinline | regnum>");
        return -1;
    }

    return 0;
}

char src[NAME_BUF_SIZE] = "B2MG";
char dst[ADDRESS_BUF_SIZE] = "#M2G";
int ttl = 5;

int compteur = 1;

static int _send_payload(char *payload)
{
    size_t payload_len = strlen(payload);

    printf("sending \"%s\" payload (%u bytes)\n", payload,
           (unsigned)(payload_len + 1));

    iolist_t iolist = {
        .iol_base = payload,
        .iol_len  = payload_len + 1
    };

    netdev_t *netdev = &sx127x.netdev;

    if (netdev->driver->send(netdev, &iolist) == -ENOTSUP) {
        puts("Cannot send: radio is still transmitting");
        return -1;
    }

    return 0;
}

int send_cmd(int argc, char **argv)
{
    if (argc <= 1) {
        puts("usage: send <payload>");
        return -1;
    }

    char payload[MESSAGE_BUF_SIZE];
    int payload_len = snprintf(payload, sizeof(payload), "%s%s:%d,%d:",
                               src, dst, compteur, ttl);
    if (payload_len < 0 || (size_t)payload_len >= sizeof(payload)) {
        puts("send: payload too long");
        return -1;
    }

    for (int i = 1; i < argc; i++) {
        int written = snprintf(payload + payload_len,
                               sizeof(payload) - (size_t)payload_len,
                               "%s%s", (i > 1) ? " " : "", argv[i]);
        if (written < 0 || (size_t)written >= (sizeof(payload) - (size_t)payload_len)) {
            puts("send: payload too long");
            return -1;
        }
        payload_len += written;
    }

    compteur++;

    return _send_payload(payload);
}

char memory[MAX_STORED_MESSAGES][MESSAGE_BUF_SIZE];
int mem_index = 0;

static char listen_filter_srcs[MAX_LISTEN_FILTERS][NAME_BUF_SIZE];
static int  listen_filter_src_count = 0;
static char listen_filter_dsts[MAX_LISTEN_FILTERS][NAME_BUF_SIZE];
static int  listen_filter_dst_count = 0;
static int  listen_filter_favoris = 0;

static char favoris[MAX_FAVORIS][ADDRESS_BUF_SIZE];
static int  favoris_count = 0;

static int _parse_message_header(const char *msg, char *src_out, size_t src_size,
                                 char *dst_out, size_t dst_size, char *sep_out);
static int _parse_field(const char *msg, int want_src, char *out, size_t out_size);
static int _is_favori(const char *name);
static int _match_favori_src(const char *src_name);
static int _match_favori_dst(char sep, const char *dst_name);
int favoris_cmd(int argc, char **argv);

int listen_cmd(int argc, char **argv)
{
    /* Reset filters */
    listen_filter_src_count = 0;
    listen_filter_dst_count = 0;
    listen_filter_favoris = 0;
    memset(listen_filter_srcs, 0, sizeof(listen_filter_srcs));
    memset(listen_filter_dsts, 0, sizeof(listen_filter_dsts));

    /* Parse @src, #dst and favoris filter arguments */
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "favoris") == 0) {
            listen_filter_favoris = 1;
        }
        else if (argv[i][0] == '@' && listen_filter_src_count < MAX_LISTEN_FILTERS) {
            strncpy(listen_filter_srcs[listen_filter_src_count], argv[i] + 1,
                    sizeof(listen_filter_srcs[0]) - 1);
            listen_filter_srcs[listen_filter_src_count][sizeof(listen_filter_srcs[0]) - 1] = '\0';
            listen_filter_src_count++;
        }
        else if (argv[i][0] == '#' && listen_filter_dst_count < MAX_LISTEN_FILTERS) {
            strncpy(listen_filter_dsts[listen_filter_dst_count], argv[i] + 1,
                    sizeof(listen_filter_dsts[0]) - 1);
            listen_filter_dsts[listen_filter_dst_count][sizeof(listen_filter_dsts[0]) - 1] = '\0';
            listen_filter_dst_count++;
        }
    }

    if (listen_filter_src_count == 0 && listen_filter_dst_count == 0 &&
        !listen_filter_favoris) {
        puts("Listening to all messages");
    }
    else {
        if (listen_filter_src_count > 0) {
            printf("Sources  : ");
            for (int i = 0; i < listen_filter_src_count; i++) {
                printf("@%s%s", listen_filter_srcs[i],
                       i < listen_filter_src_count - 1 ? ", " : "");
            }
            puts("");
        }
        if (listen_filter_dst_count > 0) {
            printf("Channels : ");
            for (int i = 0; i < listen_filter_dst_count; i++) {
                printf("#%s%s", listen_filter_dsts[i],
                       i < listen_filter_dst_count - 1 ? ", " : "");
            }
            puts("");
        }
        if (listen_filter_favoris) {
            puts("Filter   : favoris");
        }
    }

    netdev_t *netdev = &sx127x.netdev;
    /* Switch to continuous listen mode */
    const netopt_enable_t single = false;

    netdev->driver->set(netdev, NETOPT_SINGLE_RECEIVE, &single, sizeof(single));
    const uint32_t timeout = 0;

    netdev->driver->set(netdev, NETOPT_RX_TIMEOUT, &timeout, sizeof(timeout));

    /* Switch to RX state */
    netopt_state_t state = NETOPT_STATE_RX;

    netdev->driver->set(netdev, NETOPT_STATE, &state, sizeof(state));

    printf("Listen mode set\n");

    return 0;
}

int syncword_cmd(int argc, char **argv)
{
    if (argc < 2) {
        puts("usage: syncword <get|set>");
        return -1;
    }

    netdev_t *netdev = &sx127x.netdev;
    uint8_t syncword;

    if (strstr(argv[1], "get") != NULL) {
        netdev->driver->get(netdev, NETOPT_SYNCWORD, &syncword,
                            sizeof(syncword));
        printf("Syncword: 0x%02x\n", syncword);
        return 0;
    }

    if (strstr(argv[1], "set") != NULL) {
        if (argc < 3) {
            puts("usage: syncword set <syncword>");
            return -1;
        }
        syncword = fmt_hex_byte(argv[2]);
        netdev->driver->set(netdev, NETOPT_SYNCWORD, &syncword,
                            sizeof(syncword));
        printf("Syncword set to %02x\n", syncword);
    }
    else {
        puts("usage: syncword <get|set>");
        return -1;
    }

    return 0;
}
int channel_cmd(int argc, char **argv)
{
    if (argc < 2) {
        puts("usage: channel <get|set>");
        return -1;
    }

    netdev_t *netdev = &sx127x.netdev;
    uint32_t chan;

    if (strstr(argv[1], "get") != NULL) {
        netdev->driver->get(netdev, NETOPT_CHANNEL_FREQUENCY, &chan,
                            sizeof(chan));
        printf("Channel: %i\n", (int)chan);
        return 0;
    }

    if (strstr(argv[1], "set") != NULL) {
        if (argc < 3) {
            puts("usage: channel set <channel>");
            return -1;
        }
        chan = atoi(argv[2]);
        netdev->driver->set(netdev, NETOPT_CHANNEL_FREQUENCY, &chan,
                            sizeof(chan));
        printf("New channel set\n");
    }
    else {
        puts("usage: channel <get|set>");
        return -1;
    }

    return 0;
}

int rx_timeout_cmd(int argc, char **argv)
{
    if (argc < 2) {
        puts("usage: channel <get|set>");
        return -1;
    }

    netdev_t *netdev = &sx127x.netdev;
    uint16_t rx_timeout;

    if (strstr(argv[1], "set") != NULL) {
        if (argc < 3) {
            puts("usage: rx_timeout set <rx_timeout>");
            return -1;
        }
        rx_timeout = atoi(argv[2]);
        netdev->driver->set(netdev, NETOPT_RX_SYMBOL_TIMEOUT, &rx_timeout,
                            sizeof(rx_timeout));
        printf("rx_timeout set to %i\n", rx_timeout);
    }
    else {
        puts("usage: rx_timeout set");
        return -1;
    }

    return 0;
}

int reset_cmd(int argc, char **argv)
{
    (void)argc;
    (void)argv;
    netdev_t *netdev = &sx127x.netdev;

    puts("resetting sx127x...");
    netopt_state_t state = NETOPT_STATE_RESET;

    netdev->driver->set(netdev, NETOPT_STATE, &state, sizeof(netopt_state_t));
    return 0;
}

static void _set_opt(netdev_t *netdev, netopt_t opt, bool val, char *str_help)
{
    netopt_enable_t en = val ? NETOPT_ENABLE : NETOPT_DISABLE;

    netdev->driver->set(netdev, opt, &en, sizeof(en));
    printf("Successfully ");
    if (val) {
        printf("enabled ");
    }
    else {
        printf("disabled ");
    }
    printf("%s\n", str_help);
}

int crc_cmd(int argc, char **argv)
{
    netdev_t *netdev = &sx127x.netdev;

    if (argc < 3 || strcmp(argv[1], "set") != 0) {
        printf("usage: %s set <1|0>\n", argv[0]);
        return 1;
    }

    int tmp = atoi(argv[2]);

    _set_opt(netdev, NETOPT_INTEGRITY_CHECK, tmp, "CRC check");
    return 0;
}

int implicit_cmd(int argc, char **argv)
{
    netdev_t *netdev = &sx127x.netdev;

    if (argc < 3 || strcmp(argv[1], "set") != 0) {
        printf("usage: %s set <1|0>\n", argv[0]);
        return 1;
    }

    int tmp = atoi(argv[2]);

    _set_opt(netdev, NETOPT_FIXED_HEADER, tmp, "implicit header");
    return 0;
}

int payload_cmd(int argc, char **argv)
{
    netdev_t *netdev = &sx127x.netdev;

    if (argc < 3 || strcmp(argv[1], "set") != 0) {
        printf("usage: %s set <payload length>\n", argv[0]);
        return 1;
    }

    uint16_t tmp = atoi(argv[2]);

    netdev->driver->set(netdev, NETOPT_PDU_SIZE, &tmp, sizeof(tmp));
    printf("Successfully set payload to %i\n", tmp);
    return 0;
}

int threshold_cmd(int argc, char **argv)
{
    if (argc == 1 || strcmp(argv[1], "get") == 0) {
        printf("SNR threshold: %d\n", snr_threshold);
        return 0;
    }

    if (strcmp(argv[1], "set") != 0 || argc < 3) {
        puts("usage: threshold [get|set <snr>]");
        return -1;
    }

    char *endptr;
    long value = strtol(argv[2], &endptr, 10);
    if (*argv[2] == '\0' || *endptr != '\0' || value < -128 || value > 127) {
        puts("threshold: invalid SNR value");
        return -1;
    }

    snr_threshold = (int8_t)value;
    printf("SNR threshold set to %d\n", snr_threshold);
    return 0;
}

int test_cmd(int argc, char **argv)
{
    for (int i = 1; i < argc; i++) {
        if (i > 1) {
            printf(" ");
        }
        printf("%s", argv[i]);
    }
    if (argc > 1) {
        printf("\n");
    }
    return 0;
}


static void _event_cb(netdev_t *dev, netdev_event_t event)
{
    if (event == NETDEV_EVENT_ISR) {
        msg_t msg;

        msg.type = MSG_TYPE_ISR;
        msg.content.ptr = dev;

        if (msg_send(&msg, _recv_pid) <= 0) {
            puts("gnrc_netdev: possibly lost interrupt.");
        }
    }
    else {
        size_t len;
        netdev_lora_rx_info_t packet_info;
        switch (event) {
        case NETDEV_EVENT_RX_STARTED:
            puts("Data reception started");
            break;

        case NETDEV_EVENT_RX_COMPLETE:
            len = dev->driver->recv(dev, NULL, 0, 0);
            dev->driver->recv(dev, message, len, &packet_info);

            /* Apply listen filters (@src / #dst / favoris) if any are set */
            if (listen_filter_src_count > 0 || listen_filter_dst_count > 0 ||
                listen_filter_favoris) {
                char msg_src[NAME_BUF_SIZE], msg_dst[NAME_BUF_SIZE];
                char msg_sep = '\0';
                int header_ok = (_parse_message_header(message,
                                  msg_src, sizeof(msg_src),
                                  msg_dst, sizeof(msg_dst),
                                  &msg_sep) == 0);
                int match = 0;

                if (header_ok) {
                    for (int fi = 0; fi < listen_filter_src_count && !match; fi++) {
                        if (strcmp(msg_src, listen_filter_srcs[fi]) == 0) {
                            match = 1;
                        }
                    }
                }
                if (!match && header_ok && msg_sep == '#') {
                    for (int fi = 0; fi < listen_filter_dst_count && !match; fi++) {
                        if (strcmp(msg_dst, listen_filter_dsts[fi]) == 0) {
                            match = 1;
                        }
                    }
                }
                if (!match && listen_filter_favoris) {
                    if ((header_ok && _match_favori_src(msg_src)) ||
                        (header_ok && _match_favori_dst(msg_sep, msg_dst))) {
                        match = 1;
                    }
                }
                if (!match) {
                    break; /* filtered out */
                }
            }

            printf(
                "{Payload: \"%s\" (%d bytes), RSSI: %i, SNR: %i, TOA: %" PRIu32 "}\n",
                message, (int)len,
                packet_info.rssi, (int)packet_info.snr,
                sx127x_get_time_on_air((const sx127x_t *)dev, len));

            
            strncpy(memory[mem_index], message, sizeof(memory[mem_index]) - 1);
            memory[mem_index][sizeof(memory[mem_index]) - 1] = '\0';
            mem_index = (mem_index + 1) % MAX_STORED_MESSAGES;

            _maybe_relay_message(message, packet_info.snr);

            break;

        case NETDEV_EVENT_TX_COMPLETE:
            sx127x_set_sleep(&sx127x);
            puts("Transmission completed");
            break;

        case NETDEV_EVENT_CAD_DONE:
            break;

        case NETDEV_EVENT_TX_TIMEOUT:
            sx127x_set_sleep(&sx127x);
            break;

        default:
            printf("Unexpected netdev event received: %d\n", event);
            break;
        }
    }
}

void *_recv_thread(void *arg)
{
    (void)arg;

    static msg_t _msg_q[SX127X_LORA_MSG_QUEUE];

    msg_init_queue(_msg_q, SX127X_LORA_MSG_QUEUE);

    while (1) {
        msg_t msg;
        msg_receive(&msg);
        if (msg.type == MSG_TYPE_ISR) {
            netdev_t *dev = msg.content.ptr;
            dev->driver->isr(dev);
        }
        else {
            puts("Unexpected msg type");
        }
    }
}


int init_sx1272_cmd(int argc, char **argv)
{
    (void)argc;
    (void)argv;
	    sx127x.params = sx127x_params[0];
	    netdev_t *netdev = &sx127x.netdev;

	    netdev->driver = &sx127x_driver;

        netdev->event_callback = _event_cb;

//        printf("%8x\n", (unsigned int)netdev->driver);
//        printf("%8x\n", (unsigned int)netdev->driver->init);

	    if (netdev->driver->init(netdev) < 0) {
	        puts("Failed to initialize SX127x device, exiting");
	        return 1;
	    }

	    _recv_pid = thread_create(stack, sizeof(stack), THREAD_PRIORITY_MAIN - 1,
	                              THREAD_CREATE_STACKTEST, _recv_thread, NULL,
	                              "recv_thread");

	    if (_recv_pid <= KERNEL_PID_UNDEF) {
	        puts("Creation of receiver thread failed");
	        return 1;
	    }
        puts("5");

        return 0;
}


int memory_cmd(int argc, char **argv)
{
    (void)argc;
    (void)argv;

    puts("Stored messages:");
    int count = 0;
    for (int i = 0; i < MAX_STORED_MESSAGES; i++) {
        if (memory[i][0] != '\0') {
            printf("[%d] %s\n", i, memory[i]);
            count++;
        }
    }
    if (count == 0) {
        puts("No messages stored.");
    }
    return 0;
}

/* Extract the fixed-size header from a message of the form
 * "src[4]@dst[4]:compteur:msg" or "src[4]#dst[4]:compteur:msg".
 * Returns 0 on success, -1 if the format is not recognized. */
static int _parse_message_header(const char *msg, char *src_out, size_t src_size,
                                 char *dst_out, size_t dst_size, char *sep_out)
{
    if (strlen(msg) < (size_t)(MAX_NAME_LEN * 2 + 2)) {
        return -1;
    }

    if (msg[MAX_NAME_LEN] != '@' && msg[MAX_NAME_LEN] != '#') {
        return -1;
    }

    if (msg[(MAX_NAME_LEN * 2) + 1] != ':') {
        return -1;
    }

    if (src_out != NULL) {
        if (src_size < NAME_BUF_SIZE) {
            return -1;
        }
        memcpy(src_out, msg, MAX_NAME_LEN);
        src_out[MAX_NAME_LEN] = '\0';
    }

    if (dst_out != NULL) {
        if (dst_size < NAME_BUF_SIZE) {
            return -1;
        }
        memcpy(dst_out, msg + MAX_NAME_LEN + 1, MAX_NAME_LEN);
        dst_out[MAX_NAME_LEN] = '\0';
    }

    if (sep_out != NULL) {
        *sep_out = msg[MAX_NAME_LEN];
    }

    return 0;
}

/* Extract src or dst from a message.
 * For src, the returned value is "src".
 * For dst, the returned value is "@dst" or "#dst". */
static int _parse_field(const char *msg, int want_src, char *out, size_t out_size)
{
    char src_name[NAME_BUF_SIZE];
    char dst_name[NAME_BUF_SIZE];
    char sep;

    if (_parse_message_header(msg, src_name, sizeof(src_name),
                              dst_name, sizeof(dst_name), &sep) != 0) {
        return -1;
    }

    if (want_src) {
        strncpy(out, src_name, out_size - 1);
        out[out_size - 1] = '\0';
    }
    else {
        int len = snprintf(out, out_size, "%c%s", sep, dst_name);
        if (len < 0 || (size_t)len >= out_size) {
            return -1;
        }
        out[out_size - 1] = '\0';
    }

    return 0;
}

static int _parse_routed_message(const char *msg, routed_message_t *parsed)
{
    const char *meta;
    const char *body;
    long counter;
    long ttl = 0;
    char *endptr;

    if (_parse_message_header(msg, parsed->src, sizeof(parsed->src),
                              parsed->dst, sizeof(parsed->dst),
                              &parsed->sep) != 0) {
        return -1;
    }

    meta = msg + (MAX_NAME_LEN * 2) + 2;
    body = strchr(meta, ':');
    if (body == NULL || body == meta) {
        return -1;
    }

    counter = strtol(meta, &endptr, 10);
    if (endptr == meta || counter < 0) {
        return -1;
    }

    parsed->counter = (int)counter;
    parsed->ttl = 0;
    parsed->has_ttl = 0;

    if (*endptr == ',') {
        ttl = strtol(endptr + 1, &endptr, 10);
        if (ttl < 0) {
            return -1;
        }
        parsed->ttl = (int)ttl;
        parsed->has_ttl = 1;
    }

    if (endptr != body) {
        return -1;
    }

    parsed->body = body + 1;
    return 0;
}

static relay_entry_t *_find_relay_entry(const routed_message_t *parsed)
{
    for (int i = 0; i < MAX_RELAY_TRACKED; i++) {
        if (!relay_entries[i].used) {
            continue;
        }
        if (relay_entries[i].sep == parsed->sep &&
            relay_entries[i].counter == parsed->counter &&
            strcmp(relay_entries[i].src, parsed->src) == 0 &&
            strcmp(relay_entries[i].dst, parsed->dst) == 0) {
            return &relay_entries[i];
        }
    }

    return NULL;
}

static relay_entry_t *_get_or_create_relay_entry(const routed_message_t *parsed)
{
    relay_entry_t *slot = _find_relay_entry(parsed);

    if (slot != NULL) {
        return slot;
    }

    for (int i = 0; i < MAX_RELAY_TRACKED; i++) {
        if (relay_entries[i].used) {
            continue;
        }

        relay_entries[i].used = 1;
        relay_entries[i].relayed = 0;
        relay_entries[i].best_snr = -128;
        relay_entries[i].sep = parsed->sep;
        relay_entries[i].counter = parsed->counter;
        strncpy(relay_entries[i].src, parsed->src, sizeof(relay_entries[i].src) - 1);
        relay_entries[i].src[sizeof(relay_entries[i].src) - 1] = '\0';
        strncpy(relay_entries[i].dst, parsed->dst, sizeof(relay_entries[i].dst) - 1);
        relay_entries[i].dst[sizeof(relay_entries[i].dst) - 1] = '\0';
        return &relay_entries[i];
    }

    relay_entries[0].used = 1;
    relay_entries[0].relayed = 0;
    relay_entries[0].best_snr = -128;
    relay_entries[0].sep = parsed->sep;
    relay_entries[0].counter = parsed->counter;
    strncpy(relay_entries[0].src, parsed->src, sizeof(relay_entries[0].src) - 1);
    relay_entries[0].src[sizeof(relay_entries[0].src) - 1] = '\0';
    strncpy(relay_entries[0].dst, parsed->dst, sizeof(relay_entries[0].dst) - 1);
    relay_entries[0].dst[sizeof(relay_entries[0].dst) - 1] = '\0';
    return &relay_entries[0];
}

static void _maybe_relay_message(const char *rx_msg, int8_t snr)
{
    routed_message_t parsed;
    relay_entry_t *entry;
    char relay_payload[MESSAGE_BUF_SIZE];
    int next_ttl;
    int written;

    if (_parse_routed_message(rx_msg, &parsed) != 0 || !parsed.has_ttl) {
        return;
    }

    entry = _get_or_create_relay_entry(&parsed);
    if (entry == NULL) {
        puts("relay: no slot available");
        return;
    }

    if (snr > entry->best_snr) {
        entry->best_snr = snr;
    }

    if (entry->relayed) {
        return;
    }

    if (entry->best_snr > snr_threshold) {
        printf("relay: skip %s%c%s:%d because SNR %d > threshold %d\n",
               parsed.src, parsed.sep, parsed.dst, parsed.counter,
               entry->best_snr, snr_threshold);
        return;
    }

    if (parsed.ttl <= 0) {
        return;
    }

    next_ttl = parsed.ttl - 1;
    written = snprintf(relay_payload, sizeof(relay_payload),
                       "%s%c%s:%d,%d:%s",
                       parsed.src, parsed.sep, parsed.dst,
                       parsed.counter, next_ttl, parsed.body);
    if (written < 0 || (size_t)written >= sizeof(relay_payload)) {
        puts("relay: payload too long");
        return;
    }

    if (_send_payload(relay_payload) == 0) {
        entry->relayed = 1;
        printf("relay: forwarded with ttl %d -> %d\n", parsed.ttl, next_ttl);
    }
}

static int _is_favori(const char *name)
{
    for (int i = 0; i < favoris_count; i++) {
        if (strcmp(favoris[i], name) == 0) {
            return 1;
        }
    }
    return 0;
}

static int _match_favori_src(const char *src_name)
{
    char prefixed_src[ADDRESS_BUF_SIZE];
    int len = snprintf(prefixed_src, sizeof(prefixed_src), "@%s", src_name);

    if (len < 0 || (size_t)len >= sizeof(prefixed_src)) {
        return 0;
    }

    return _is_favori(prefixed_src);
}

static int _match_favori_dst(char sep, const char *dst_name)
{
    char prefixed_dst[ADDRESS_BUF_SIZE];
    int len = snprintf(prefixed_dst, sizeof(prefixed_dst), "%c%s", sep, dst_name);

    if (len < 0 || (size_t)len >= sizeof(prefixed_dst)) {
        return 0;
    }

    return _is_favori(prefixed_dst);
}

int favoris_cmd(int argc, char **argv)
{
    if (argc < 2) {
        puts("usage: favoris <list|add|remove|clear> [name]");
        return -1;
    }

    if (strcmp(argv[1], "list") == 0) {
        if (favoris_count == 0) {
            puts("No favorites configured.");
            return 0;
        }

        puts("Favorites:");
        for (int i = 0; i < favoris_count; i++) {
            printf("[%d] %s\n", i, favoris[i]);
        }
        return 0;
    }

    if (strcmp(argv[1], "add") == 0) {
        if (argc < 3) {
            puts("usage: favoris add <name>");
            return -1;
        }
        if ((argv[2][0] != '@' && argv[2][0] != '#') || argv[2][1] == '\0') {
            puts("favoris add: use @name for a person or #name for a channel.");
            return -1;
        }
        if (strlen(argv[2] + 1) != MAX_NAME_LEN) {
            printf("favoris add: name must be exactly %d characters.\n", MAX_NAME_LEN);
            return -1;
        }
        if (_is_favori(argv[2])) {
            printf("%s is already in favorites.\n", argv[2]);
            return 0;
        }
        if (favoris_count >= MAX_FAVORIS) {
            puts("Favorites list is full.");
            return -1;
        }

        strncpy(favoris[favoris_count], argv[2], sizeof(favoris[0]) - 1);
        favoris[favoris_count][sizeof(favoris[0]) - 1] = '\0';
        favoris_count++;
        printf("%s added to favorites.\n", argv[2]);
        return 0;
    }

    if (strcmp(argv[1], "remove") == 0) {
        if (argc < 3) {
            puts("usage: favoris remove <name>");
            return -1;
        }

        for (int i = 0; i < favoris_count; i++) {
            if (strcmp(favoris[i], argv[2]) == 0) {
                for (int j = i; j < favoris_count - 1; j++) {
                    strncpy(favoris[j], favoris[j + 1], sizeof(favoris[j]) - 1);
                    favoris[j][sizeof(favoris[j]) - 1] = '\0';
                }
                memset(favoris[favoris_count - 1], 0, sizeof(favoris[favoris_count - 1]));
                favoris_count--;
                printf("%s removed from favorites.\n", argv[2]);
                return 0;
            }
        }

        printf("%s not found in favorites.\n", argv[2]);
        return -1;
    }

    if (strcmp(argv[1], "clear") == 0) {
        memset(favoris, 0, sizeof(favoris));
        favoris_count = 0;
        puts("Favorites cleared.");
        return 0;
    }

    puts("usage: favoris <list|add|remove|clear> [name]");
    return -1;
}

int filter_cmd(int argc, char **argv)
{
    if (argc < 2 ||
        (strcmp(argv[1], "src") != 0 && strcmp(argv[1], "dst") != 0)) {
        puts("usage: filter <src|dst> [value]");
        return -1;
    }

    int want_src = (strcmp(argv[1], "src") == 0);
    char field[ADDRESS_BUF_SIZE];

    if (argc == 2) {
        /* List all unique src or dst values */
        static char seen[MAX_CONTACTS][ADDRESS_BUF_SIZE];
        int seen_count = 0;
        memset(seen, 0, sizeof(seen));

        for (int i = 0; i < MAX_STORED_MESSAGES; i++) {
            if (memory[i][0] == '\0') continue;
            if (_parse_field(memory[i], want_src, field, sizeof(field)) != 0) continue;

            int found = 0;
            for (int j = 0; j < seen_count; j++) {
                if (strcmp(seen[j], field) == 0) { found = 1; break; }
            }
            if (!found && seen_count < MAX_CONTACTS) {
                strncpy(seen[seen_count], field, sizeof(seen[seen_count]) - 1);
                seen[seen_count][sizeof(seen[seen_count]) - 1] = '\0';
                seen_count++;
            }
        }

        if (seen_count == 0) {
            printf("No %s found.\n", argv[1]);
        } else {
            printf("Known %s:\n", argv[1]);
            for (int i = 0; i < seen_count; i++) {
                printf("  %s\n", seen[i]);
            }
        }
    } else {
        /* Show all messages matching the given src or dst value */
        const char *filter_val = argv[2];
        int count = 0;

        printf("Messages with %s=%s:\n", argv[1], filter_val);
        for (int i = 0; i < MAX_STORED_MESSAGES; i++) {
            if (memory[i][0] == '\0') continue;
            if (_parse_field(memory[i], want_src, field, sizeof(field)) != 0) continue;
            if (strcmp(field, filter_val) == 0) {
                printf("[%d] %s\n", i, memory[i]);
                count++;
            }
        }

        if (count == 0) {
            printf("No messages found for %s=%s.\n", argv[1], filter_val);
        }
    }

    return 0;
}

static const shell_command_t shell_commands[] = {
	{ "init",    "Initialize SX1272",     					init_sx1272_cmd },
	{ "setup",    "Initialize LoRa modulation settings",     lora_setup_cmd },
    { "implicit", "Enable implicit header",                  implicit_cmd },
    { "crc",      "Enable CRC",                              crc_cmd },
    { "payload",  "Set payload length (implicit header)",    payload_cmd },
    { "random",   "Get random number from sx127x",           random_cmd },
    { "syncword", "Get/Set the syncword",                    syncword_cmd },
    { "rx_timeout", "Set the RX timeout",                    rx_timeout_cmd },
    { "channel",  "Get/Set channel frequency (in Hz)",       channel_cmd },
    { "register", "Get/Set value(s) of registers of sx127x", register_cmd },
    { "send",     "Send raw payload string",                 send_cmd },
    { "threshold","Get/Set relay SNR threshold",             threshold_cmd },
    { "listen",   "Listen: [favoris] [@src...] [#dst...] (no args = all)", listen_cmd },
    { "reset",    "Reset the sx127x device",                 reset_cmd },
    { "test",    "Test the sx127x device",                 test_cmd },
    { "memory",    "Memory all messages",                 memory_cmd },
    { "filter",    "Filter messages by src/dst",           filter_cmd },
    { "favoris",    "Manage favorites: list | add | remove | clear", favoris_cmd },

    { NULL, NULL, NULL }
};

int main(void) {

    //init_sx1272_cmd(0,NULL);

    /* start the shell */
    puts("Initialization successful - starting the shell now");
    char line_buf[SHELL_DEFAULT_BUFSIZE];

    shell_run(shell_commands, line_buf, SHELL_DEFAULT_BUFSIZE);

    return 0;
}
