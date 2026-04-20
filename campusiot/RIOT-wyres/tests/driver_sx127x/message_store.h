/**
 * @file    message_store.h
 * @brief   Stockage en RAM des messages reçus, filtrage et gestion des favoris.
 *
 * Ce module maintient :
 *  - Un buffer circulaire de MAX_STORED_MESSAGES messages reçus.
 *  - Des filtres de source/destination actifs pour la commande listen.
 *  - Une liste de favoris (@personne ou #salon).
 *
 * Les messages reçus sont automatiquement enregistrés par receiver.c
 * via app_store_message(). La commande memory les affiche, filter les
 * recherche par src/dst, et listen applique les filtres en temps réel.
 */

#ifndef MESSAGE_STORE_H
#define MESSAGE_STORE_H

#include <stddef.h>

/* --- Commandes shell --- */

/**
 * @brief Passe en mode écoute radio, avec filtres optionnels.
 *
 * Arguments possibles : "favoris", "@SRC", "#DST" (combinables).
 * Sans argument, tous les messages sont affichés.
 */
int listen_cmd(int argc, char **argv);

/** Affiche tous les messages stockés dans le buffer circulaire. */
int memory_cmd(int argc, char **argv);

/**
 * @brief Recherche dans les messages stockés par source ou destination.
 *
 * Usage : filter src [valeur] | filter dst [valeur]
 */
int filter_cmd(int argc, char **argv);

/**
 * @brief Gère la liste des favoris en RAM.
 *
 * Sous-commandes : list | add <@nom|#salon> | remove <@nom|#salon> | clear
 */
int favoris_cmd(int argc, char **argv);

/* --- Fonctions utilitaires appelées par receiver.c --- */

/**
 * @brief Vérifie si un message passe les filtres actifs de la commande listen.
 * @param msg Le message brut reçu.
 * @return 1 si le message doit être affiché, 0 sinon.
 */
int app_message_passes_filters(const char *msg);

/**
 * @brief Enregistre un message dans le buffer circulaire.
 * @param msg Le message brut reçu (null-terminé).
 */
void app_store_message(const char *msg);

#endif /* MESSAGE_STORE_H */
