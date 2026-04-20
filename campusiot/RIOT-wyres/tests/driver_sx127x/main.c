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
 * @file        main.c
 * @brief       Point d'entrée de l'application de test du driver SX127x.
 *
 * Architecture du projet :
 *  - radio_commands.c : initialisation et configuration de la radio
 *  - receiver.c       : thread de réception asynchrone + callback netdev
 *  - message_store.c  : stockage, filtrage, favoris
 *  - relay.c          : logique de relais multi-sauts basée sur le SNR
 *
 * @author      Eugene P. <ep@unwds.com>
 * @author      José Ignacio Alamos <jose.alamos@inria.cl>
 * @author      Alexandre Abadie <alexandre.abadie@inria.fr>
 * @}
 */

#include <stdio.h>

#include "shell.h"

#include "message_store.h"
#include "radio_commands.h"
#include "relay.h"

/**
 * @brief Table des commandes shell enregistrées.
*/
static const shell_command_t shell_commands[] = {
    { "init",      "Initialise le SX127x et démarre la radio",        init_sx1272_cmd },
    { "setup",     "Configure BW / SF / CR du modem LoRa",            lora_setup_cmd },
    { "implicit",  "Active/désactive le header implicite",            implicit_cmd },
    { "crc",       "Active/désactive la vérification CRC",            crc_cmd },
    { "payload",   "Définit la taille payload (header implicite)",    payload_cmd },
    { "random",    "Génère un nombre aléatoire via le SX127x",        random_cmd },
    { "syncword",  "Lit/écrit le syncword LoRa",                      syncword_cmd },
    { "rx_timeout","Configure le timeout de réception",               rx_timeout_cmd },
    { "channel",   "Lit/écrit la fréquence du canal (Hz)",            channel_cmd },
    { "register",  "Lit/écrit les registres internes du SX127x",      register_cmd },
    { "send",      "Envoie un message LoRa formaté",                  send_cmd },
    { "threshold", "Lit/écrit le seuil SNR de relais",                threshold_cmd },
    { "listen",    "Écoute : [favoris] [@src...] [#dst...] (défaut: tout)", listen_cmd },
    { "reset",     "Réinitialise le SX127x",                          reset_cmd },
    { "memory",    "Affiche les messages stockés en mémoire",         memory_cmd },
    { "filter",    "Filtre les messages par src ou dst",              filter_cmd },
    { "favoris",   "Gère les favoris : list | add | remove | clear",  favoris_cmd },
    { NULL, NULL, NULL }
};

/**
 * @brief Point d'entrée principal de l'application.
*/
int main(void)
{
    puts("Initialization successful - starting the shell now");

    char line_buf[SHELL_DEFAULT_BUFSIZE];
    shell_run(shell_commands, line_buf, SHELL_DEFAULT_BUFSIZE);

    return 0;
}
