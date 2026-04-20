/*
 * Copyright (C) 2016 Unwired Devices <info@unwds.com>
 *               2017 Inria Chile
 *               2017 Inria
 *
 * This file is subject to the terms and conditions of the GNU Lesser
 * General Public License v2.1. See the file LICENSE in the top level
 * directory for more details.
 */

#include <errno.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "net/netdev.h"
#include "net/netdev/lora.h"
#include "net/lora.h"

#include "fmt.h"
#include "sx127x_params.h"
#include "sx127x_netdev.h"

#include "app_defs.h"
#include "radio_commands.h"
#include "receiver.h"

/* Instance unique du modem SX127x pour toute l'application */
static sx127x_t sx127x;

/* Identifiant source par défaut de ce noeud (4 caractères fixes) */
static char src[NAME_BUF_SIZE] = "B2MG";

/* Destination par défaut : le salon #M2G */
static char dst[ADDRESS_BUF_SIZE] = "#M2G";

/* TTL initial des messages envoyés (nombre de sauts maximum) */
static int ttl = 5;

/* Compteur de séquence, incrémenté à chaque envoi pour identifier les messages */
static int compteur = 1;

netdev_t *app_netdev(void)
{
    return &sx127x.netdev;
}

sx127x_t *app_radio(void)
{
    return &sx127x;
}

/**
 * @brief Envoie un payload brut via le modem LoRa.
 *
 * Construit un iolist (liste de buffers) et appelle le driver netdev.
 * On envoie payload_len + 1 octets pour inclure le terminateur nul,
 * ce qui permet au récepteur de traiter directement la chaîne reçue.
 *
 * @param payload Chaîne null-terminée à envoyer.
 * @return 0 si l'envoi est lancé, -1 si la radio est déjà en train d'émettre.
 */
int app_send_payload(char *payload)
{
    size_t payload_len = strlen(payload);

    printf("sending \"%s\" payload (%u bytes)\n", payload,
           (unsigned)(payload_len + 1));

    iolist_t iolist = {
        .iol_base = payload,
        .iol_len  = payload_len + 1  /* +1 pour le '\0' terminal */
    };

    netdev_t *netdev = app_netdev();

    if (netdev->driver->send(netdev, &iolist) == -ENOTSUP) {
        puts("Cannot send: radio is still transmitting");
        return -1;
    }

    return 0;
}


int lora_setup_cmd(int argc, char **argv)
{
    if (argc < 4) {
        puts("usage: setup "
             "<bandwidth (125, 250, 500)> "
             "<spreading factor (7..12)> "
             "<code rate (5..8)>");
        return -1;
    }

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

    uint8_t lora_sf = atoi(argv[2]);
    if (lora_sf < 7 || lora_sf > 12) {
        puts("[Error] setup: invalid spreading factor value given");
        return -1;
    }

    int cr = atoi(argv[3]);
    if (cr < 5 || cr > 8) {
        puts("[Error ]setup: invalid coding rate value given");
        return -1;
    }
    /* RIOT encode le CR comme un offset de 4 : CR=5 → lora_cr=1, CR=8 → lora_cr=4 */
    uint8_t lora_cr = (uint8_t)(cr - 4);

    netdev_t *netdev = app_netdev();
    netdev->driver->set(netdev, NETOPT_BANDWIDTH,       &lora_bw, sizeof(lora_bw));
    netdev->driver->set(netdev, NETOPT_SPREADING_FACTOR, &lora_sf, sizeof(lora_sf));
    netdev->driver->set(netdev, NETOPT_CODING_RATE,     &lora_cr, sizeof(lora_cr));

    puts("[Info] setup: configuration set with success");
    return 0;
}

int random_cmd(int argc, char **argv)
{
    (void)argc;
    (void)argv;

    netdev_t *netdev = app_netdev();
    uint32_t rand;

    netdev->driver->get(netdev, NETOPT_RANDOM, &rand, sizeof(rand));
    printf("random: number from sx127x: %u\n", (unsigned int)rand);

    sx127x_init_radio_settings(app_radio());
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
            /* Affichage tabulaire : 16 registres par ligne, adresses en colonne */
            puts("- listing all registers -");
            uint8_t reg = 0;
            uint8_t data = 0;

            puts("Reg   0  1  2  3  4  5  6  7  8  9  A  B  C  D  E  F");
            for (unsigned i = 0; i <= 7; i++) {
                printf("0x%02X ", i << 4);

                for (unsigned j = 0; j <= 15; j++, reg++) {
                    data = sx127x_reg_read(app_radio(), reg);
                    printf("%02X ", data);
                }
                puts("");
            }
            puts("-done-");
            return 0;
        }
        else if (strcmp(argv[2], "allinline") == 0) {
            puts("- listing all registers in one line -");
            for (uint16_t reg = 0; reg < 256; reg++) {
                printf("%02X ", sx127x_reg_read(app_radio(), (uint8_t)reg));
            }
            puts("- done -");
            return 0;
        }
        else {
            /* Lecture d'un seul registre : supporte la notation décimale et 0xNN */
            long int num = 0;

            if (strstr(argv[2], "0x") != NULL) {
                num = strtol(argv[2], NULL, 16);
            }
            else {
                num = atoi(argv[2]);
            }

            if (num >= 0 && num <= 255) {
                printf("[regs] 0x%02X = 0x%02X\n",
                       (uint8_t)num,
                       sx127x_reg_read(app_radio(), (uint8_t)num));
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

        long num;
        long val;

        /* Support notation hex (0x..) et décimale pour le numéro de registre */
        if (strstr(argv[2], "0x") != NULL) {
            num = strtol(argv[2], NULL, 16);
        }
        else {
            num = atoi(argv[2]);
        }

        /* Support notation hex (0x..) et décimale pour la valeur */
        if (strstr(argv[3], "0x") != NULL) {
            val = strtol(argv[3], NULL, 16);
        }
        else {
            val = atoi(argv[3]);
        }

        sx127x_reg_write(app_radio(), (uint8_t)num, (uint8_t)val);
    }
    else {
        puts("usage: register get <all | allinline | regnum>");
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

    /* Construction de l'en-tête : "SSSS#DDDD:compteur,ttl:" */
    int payload_len = snprintf(payload, sizeof(payload), "%s%s:%d,%d:",
                               src, dst, compteur, ttl);
    if (payload_len < 0 || (size_t)payload_len >= sizeof(payload)) {
        puts("send: payload too long");
        return -1;
    }

    /* Ajout du contenu du message argument par argument */
    for (int i = 1; i < argc; i++) {
        int written = snprintf(payload + payload_len,
                               sizeof(payload) - (size_t)payload_len,
                               "%s%s", (i > 1) ? " " : "", argv[i]);
        if (written < 0 ||
            (size_t)written >= (sizeof(payload) - (size_t)payload_len)) {
            puts("send: payload too long");
            return -1;
        }
        payload_len += written;
    }

    compteur++;  /* Incrémente après construction pour le prochain message */
    return app_send_payload(payload);
}

int syncword_cmd(int argc, char **argv)
{
    if (argc < 2) {
        puts("usage: syncword <get|set>");
        return -1;
    }

    netdev_t *netdev = app_netdev();
    uint8_t syncword;

    if (strstr(argv[1], "get") != NULL) {
        netdev->driver->get(netdev, NETOPT_SYNCWORD, &syncword, sizeof(syncword));
        printf("Syncword: 0x%02x\n", syncword);
        return 0;
    }

    if (strstr(argv[1], "set") != NULL) {
        if (argc < 3) {
            puts("usage: syncword set <syncword>");
            return -1;
        }
        syncword = fmt_hex_byte(argv[2]);
        netdev->driver->set(netdev, NETOPT_SYNCWORD, &syncword, sizeof(syncword));
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

    netdev_t *netdev = app_netdev();
    uint32_t chan;

    if (strstr(argv[1], "get") != NULL) {
        netdev->driver->get(netdev, NETOPT_CHANNEL_FREQUENCY, &chan, sizeof(chan));
        printf("Channel: %i\n", (int)chan);
        return 0;
    }

    if (strstr(argv[1], "set") != NULL) {
        if (argc < 3) {
            puts("usage: channel set <channel>");
            return -1;
        }
        chan = atoi(argv[2]);
        netdev->driver->set(netdev, NETOPT_CHANNEL_FREQUENCY, &chan, sizeof(chan));
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

    netdev_t *netdev = app_netdev();
    uint16_t rx_timeout;

    if (strstr(argv[1], "set") != NULL) {
        if (argc < 3) {
            puts("usage: rx_timeout set <rx_timeout>");
            return -1;
        }
        rx_timeout = atoi(argv[2]);
        netdev->driver->set(netdev, NETOPT_RX_SYMBOL_TIMEOUT,
                            &rx_timeout, sizeof(rx_timeout));
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

    netdev_t *netdev = app_netdev();

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
    netdev_t *netdev = app_netdev();

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
    netdev_t *netdev = app_netdev();

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
    netdev_t *netdev = app_netdev();

    if (argc < 3 || strcmp(argv[1], "set") != 0) {
        printf("usage: %s set <payload length>\n", argv[0]);
        return 1;
    }

    uint16_t tmp = atoi(argv[2]);
    netdev->driver->set(netdev, NETOPT_PDU_SIZE, &tmp, sizeof(tmp));
    printf("Successfully set payload to %i\n", tmp);
    return 0;
}

int init_sx1272_cmd(int argc, char **argv)
{
    (void)argc;
    (void)argv;

    /* Paramètres hardware définis dans sx127x_params.h (selon le board) */
    sx127x.params = sx127x_params[0];

    netdev_t *netdev = app_netdev();
    netdev->driver = &sx127x_driver;
    netdev->event_callback = app_event_cb;  /* Callback appelé à chaque événement radio */

    if (netdev->driver->init(netdev) < 0) {
        puts("Failed to initialize SX127x device, exiting");
        return 1;
    }

    /* Démarrage du thread RIOT dédié au traitement des événements de réception */
    kernel_pid_t recv_pid = app_start_recv_thread();
    if (recv_pid <= KERNEL_PID_UNDEF) {
        puts("Creation of receiver thread failed");
        return 1;
    }

    puts("5");
    return 0;
}
