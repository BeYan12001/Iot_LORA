/**
 * @file    radio_commands.h
 * @brief   Déclaration des commandes shell liées à la radio SX127x.
 *
*/

#ifndef RADIO_COMMANDS_H
#define RADIO_COMMANDS_H

#include "net/netdev.h"
#include "sx127x_internal.h"

/* --- Commandes shell --- */

/** Initialise le SX127x, enregistre le callback et démarre le thread de réception. */
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

/* --- Accesseurs utilitaires --- */

/** Retourne un pointeur vers le netdev générique (utilisé par receiver et relay). */
netdev_t *app_netdev(void);

/** Retourne un pointeur vers le contexte SX127x (pour accéder aux fonctions internes). */
sx127x_t *app_radio(void);

/**
 * @brief Envoie un payload brut via la radio.
 * @param payload Chaîne null-terminée à envoyer (max MESSAGE_BUF_SIZE).
 * @return 0 en cas de succès, -1 si la radio est occupée.
 */
int app_send_payload(char *payload);

#endif /* RADIO_COMMANDS_H */
