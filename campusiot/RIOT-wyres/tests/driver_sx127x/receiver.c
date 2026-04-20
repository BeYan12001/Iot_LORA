/*
 * Copyright (C) 2016 Unwired Devices <info@unwds.com>
 *               2017 Inria Chile
 *               2017 Inria
 *
 * This file is subject to the terms and conditions of the GNU Lesser
 * General Public License v2.1. See the file LICENSE in the top level
 * directory for more details.
 */

#include <inttypes.h>
#include <stdio.h>

#include "msg.h"
#include "net/netdev.h"
#include "net/netdev/lora.h"
#include "thread.h"

#include "sx127x_internal.h"

#include "app_defs.h"
#include "message_store.h"
#include "radio_commands.h"
#include "receiver.h"
#include "relay.h"

/** Taille de la file de messages IPC du thread de réception */
#define SX127X_LORA_MSG_QUEUE   (16U)

/** Taille de la pile du thread de réception (valeur par défaut RIOT) */
#ifndef SX127X_STACKSIZE
#define SX127X_STACKSIZE        (THREAD_STACKSIZE_DEFAULT)
#endif

/** Type de message IPC signalant une interruption hardware à traiter */
#define MSG_TYPE_ISR            (0x3456)

/** Pile statique allouée pour le thread de réception */
static char stack[SX127X_STACKSIZE];

/** PID du thread de réception (initialisé lors de app_start_recv_thread) */
static kernel_pid_t recv_pid = KERNEL_PID_UNDEF;

/** Buffer de réception partagé pour le payload du dernier paquet reçu */
static char message[MESSAGE_BUF_SIZE];

static void *_recv_thread(void *arg)
{
    (void)arg;

    /* File de messages IPC locale au thread (allouée sur la pile du thread) */
    static msg_t msg_q[SX127X_LORA_MSG_QUEUE];

    msg_init_queue(msg_q, SX127X_LORA_MSG_QUEUE);

    while (1) {
        msg_t msg;
        msg_receive(&msg);  /* Bloquant : le thread dort jusqu'au prochain message */
        if (msg.type == MSG_TYPE_ISR) {
            netdev_t *dev = msg.content.ptr;
            dev->driver->isr(dev);  /* Demande au driver de traiter l'interruption */
        }
        else {
            puts("Unexpected msg type");
        }
    }

    return NULL;
}

kernel_pid_t app_start_recv_thread(void)
{
    recv_pid = thread_create(stack, sizeof(stack), THREAD_PRIORITY_MAIN - 1,
                             THREAD_CREATE_STACKTEST, _recv_thread, NULL,
                             "recv_thread");
    return recv_pid;
}

void app_event_cb(netdev_t *dev, netdev_event_t event)
{
    if (event == NETDEV_EVENT_ISR) {
        /* On est dans l'ISR : on délègue au thread de réception via IPC */
        msg_t msg;

        msg.type = MSG_TYPE_ISR;
        msg.content.ptr = dev;

        if (msg_send(&msg, recv_pid) <= 0) {
            puts("gnrc_netdev: possibly lost interrupt.");
        }
    }
    else {
        /* On est dans le _recv_thread : traitement normal des événements */
        size_t len;
        netdev_lora_rx_info_t packet_info;  /* RSSI, SNR et autres métriques radio */

        switch (event) {
        case NETDEV_EVENT_RX_STARTED:
            /* Signal de début de réception (avant d'avoir tout le paquet) */
            puts("Data reception started");
            break;

        case NETDEV_EVENT_RX_COMPLETE:
            /* Paquet complètement reçu : on le lit depuis le buffer interne du SX127x */
            len = dev->driver->recv(dev, NULL, 0, 0);            /* 1er appel : taille */
            dev->driver->recv(dev, message, len, &packet_info); /* 2ème appel : data */

            /* Affichage uniquement si le message passe les filtres actifs */
            if (!app_message_passes_filters(message)) {
                break;
            }

            printf("{Payload: \"%s\" (%d bytes), RSSI: %i, SNR: %i, TOA: %" PRIu32 "}\n",
                   message, (int)len, packet_info.rssi, (int)packet_info.snr,
                   sx127x_get_time_on_air(app_radio(), len));

            /* Stockage dans le buffer circulaire pour la commande memory/filter */
            app_store_message(message);

            /* Évaluation du relais multi-sauts */
            app_maybe_relay_message(message, packet_info.snr);
            break;

        case NETDEV_EVENT_TX_COMPLETE:
            /* Après envoi, on remet le modem en veille pour économiser l'énergie */
            sx127x_set_sleep(app_radio());
            puts("Transmission completed");
            break;

        case NETDEV_EVENT_CAD_DONE:
            /* Channel Activity Detection terminée (non utilisée ici) */
            break;

        case NETDEV_EVENT_TX_TIMEOUT:
            /* Timeout d'émission : on remet quand même le modem en veille */
            sx127x_set_sleep(app_radio());
            break;

        default:
            printf("Unexpected netdev event received: %d\n", event);
            break;
        }
    }
}
