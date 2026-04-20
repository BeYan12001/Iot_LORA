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
 * @file    message_store.c
 * @brief   Stockage des messages reçus, système de filtrage et gestion des favoris.
 *
 * Ce module est le cœur de la gestion applicative des messages LoRa.
 * Il maintient en RAM statique :
 *
 *  1. Un buffer circulaire (memory[]) de MAX_STORED_MESSAGES messages reçus.
 *     Quand il est plein, les anciens messages sont écrasés.
 *
 *  2. Des filtres d'écoute actifs (listen_filter_srcs[], listen_filter_dsts[])
 *     configurés par la commande listen. En mode filtré, seuls les messages
 *     correspondant à une source ou destination listée sont affichés.
 *
 *  3. Une liste de favoris (favoris[]) : la commande listen peut filtrer
 *     automatiquement en ne gardant que les messages des/vers les favoris.
 */

#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "app_defs.h"
#include "message_store.h"
#include "radio_commands.h"

/* --- Stockage des messages reçus --- */

/** Buffer circulaire de stockage des messages (tableau de chaînes) */
static char memory[MAX_STORED_MESSAGES][MESSAGE_BUF_SIZE];

/** Index d'écriture dans le buffer (avance en boucle modulo MAX_STORED_MESSAGES) */
static int mem_index = 0;

/* --- Filtres actifs de la commande listen --- */

/** Tableau des sources filtrées (arguments @SRC passés à listen) */
static char listen_filter_srcs[MAX_LISTEN_FILTERS][NAME_BUF_SIZE];
static int listen_filter_src_count = 0;

/** Tableau des destinations filtrées (arguments #DST passés à listen) */
static char listen_filter_dsts[MAX_LISTEN_FILTERS][NAME_BUF_SIZE];
static int listen_filter_dst_count = 0;

/** Mode favoris actif : 1 si "listen favoris" a été tapé */
static int listen_filter_favoris = 0;

/* --- Liste des favoris --- */

/** Tableau des contacts favoris (@personne ou #salon) */
static char favoris[MAX_FAVORIS][ADDRESS_BUF_SIZE];
static int favoris_count = 0;

/**
 * @brief Extrait la source, la destination et le séparateur d'un message.
 *
 * Le format attendu est : SSSS[@#]DDDD:...
 * Les 4 premiers chars = source, char[4] = séparateur, chars[5..8] = destination.
 * Le char[9] doit être ':' pour que le parsing soit valide.
 *
 * @param msg       Message brut reçu.
 * @param src_out   Buffer de sortie pour la source (peut être NULL).
 * @param src_size  Taille du buffer src_out (doit être >= NAME_BUF_SIZE).
 * @param dst_out   Buffer de sortie pour la destination (peut être NULL).
 * @param dst_size  Taille du buffer dst_out.
 * @param sep_out   Pointeur vers le caractère séparateur de sortie (peut être NULL).
 * @return 0 si parsing OK, -1 si le message ne respecte pas le format.
 */
static int _parse_message_header(const char *msg, char *src_out, size_t src_size,
                                 char *dst_out, size_t dst_size, char *sep_out)
{
    if (strlen(msg) < (size_t)(MAX_NAME_LEN * 2 + 2)) {
        return -1;
    }

    /* Le séparateur doit être '@' (personne) ou '#' (salon) */
    if (msg[MAX_NAME_LEN] != '@' && msg[MAX_NAME_LEN] != '#') {
        return -1;
    }

    /* Après src(4) + sep(1) + dst(4), il doit y avoir ':' */
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
        /* La destination commence juste après le séparateur */
        memcpy(dst_out, msg + MAX_NAME_LEN + 1, MAX_NAME_LEN);
        dst_out[MAX_NAME_LEN] = '\0';
    }

    if (sep_out != NULL) {
        *sep_out = msg[MAX_NAME_LEN];
    }

    return 0;
}

/**
 * @brief Extrait la source ou la destination d'un message sous forme de chaîne.
 *
 * Pour la destination, le séparateur ('@' ou '#') est préfixé au résultat
 * afin de distinguer les destinations "personne" des destinations "salon".
 *
 * @param msg       Message brut.
 * @param want_src  1 pour extraire la source, 0 pour la destination.
 * @param out       Buffer de sortie.
 * @param out_size  Taille du buffer.
 * @return 0 si OK, -1 si le message ne respecte pas le format.
 */
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
        /* Pour dst, on préfixe avec le séparateur : "@YANN" ou "#M2GI" */
        int len = snprintf(out, out_size, "%c%s", sep, dst_name);
        if (len < 0 || (size_t)len >= out_size) {
            return -1;
        }
        out[out_size - 1] = '\0';
    }

    return 0;
}

/**
 * @brief Vérifie si un identifiant est dans la liste des favoris.
 *
 * @param name Identifiant à rechercher (ex: "@YANN" ou "#M2GI").
 * @return 1 si trouvé, 0 sinon.
 */
static int _is_favori(const char *name)
{
    for (int i = 0; i < favoris_count; i++) {
        if (strcmp(favoris[i], name) == 0) {
            return 1;
        }
    }
    return 0;
}

/**
 * @brief Vérifie si la source d'un message correspond à un favori de type @personne.
 *
 * On construit "@src_name" et on cherche dans la liste des favoris.
 *
 * @param src_name Source brute (4 chars, ex: "YANN").
 * @return 1 si la source est un favori @personne, 0 sinon.
 */
static int _match_favori_src(const char *src_name)
{
    char prefixed_src[ADDRESS_BUF_SIZE];
    int len = snprintf(prefixed_src, sizeof(prefixed_src), "@%s", src_name);

    if (len < 0 || (size_t)len >= sizeof(prefixed_src)) {
        return 0;
    }

    return _is_favori(prefixed_src);
}

/**
 * @brief Vérifie si la destination d'un message correspond à un favori.
 *
 * On reconstruit "sep+dst_name" (ex: "#M2GI") et on cherche dans la liste.
 *
 * @param sep      Séparateur du message ('@' ou '#').
 * @param dst_name Destination brute (4 chars).
 * @return 1 si la destination est un favori, 0 sinon.
 */
static int _match_favori_dst(char sep, const char *dst_name)
{
    char prefixed_dst[ADDRESS_BUF_SIZE];
    int len = snprintf(prefixed_dst, sizeof(prefixed_dst), "%c%s", sep, dst_name);

    if (len < 0 || (size_t)len >= sizeof(prefixed_dst)) {
        return 0;
    }

    return _is_favori(prefixed_dst);
}

/**
 * @brief Détermine si un message doit être affiché selon les filtres actifs.
 *
 * Logique de filtrage :
 *  - Si aucun filtre n'est actif → tous les messages passent.
 *  - Filtre src : le message passe si sa source est dans listen_filter_srcs[].
 *  - Filtre dst : le message passe si sa destination est dans listen_filter_dsts[]
 *                 ET que le séparateur est '#' (salon seulement).
 *  - Filtre favoris : le message passe si sa source est un favori @personne
 *                     OU sa destination est un favori #salon.
 *  - Les filtres sont combinés en OR : un match sur n'importe lequel suffit.
 *
 * @param msg Message brut reçu.
 * @return 1 si le message doit être affiché, 0 sinon.
 */
int app_message_passes_filters(const char *msg)
{
    /* Sans filtre actif, tout passe */
    if (listen_filter_src_count == 0 && listen_filter_dst_count == 0 &&
        !listen_filter_favoris) {
        return 1;
    }

    char msg_src[NAME_BUF_SIZE];
    char msg_dst[NAME_BUF_SIZE];
    char msg_sep = '\0';
    int header_ok = (_parse_message_header(msg,
                                           msg_src, sizeof(msg_src),
                                           msg_dst, sizeof(msg_dst),
                                           &msg_sep) == 0);
    int match = 0;

    /* Vérification du filtre source */
    if (header_ok) {
        for (int fi = 0; fi < listen_filter_src_count && !match; fi++) {
            if (strcmp(msg_src, listen_filter_srcs[fi]) == 0) {
                match = 1;
            }
        }
    }

    /* Vérification du filtre destination (uniquement pour les messages de salon '#') */
    if (!match && header_ok && msg_sep == '#') {
        for (int fi = 0; fi < listen_filter_dst_count && !match; fi++) {
            if (strcmp(msg_dst, listen_filter_dsts[fi]) == 0) {
                match = 1;
            }
        }
    }

    /* Vérification du filtre favoris : source @personne ou destination #salon */
    if (!match && listen_filter_favoris) {
        if ((header_ok && _match_favori_src(msg_src)) ||
            (header_ok && _match_favori_dst(msg_sep, msg_dst))) {
            match = 1;
        }
    }

    return match;
}

/**
 * @brief Enregistre un message dans le buffer circulaire.
 *
 * Le buffer avance en mode circulaire : une fois plein, le plus ancien
 * message est écrasé. La chaîne est copiée et toujours null-terminée.
 *
 * @param msg Message brut reçu (null-terminé).
 */
void app_store_message(const char *msg)
{
    strncpy(memory[mem_index], msg, sizeof(memory[mem_index]) - 1);
    memory[mem_index][sizeof(memory[mem_index]) - 1] = '\0';
    mem_index = (mem_index + 1) % MAX_STORED_MESSAGES;
}

/**
 * @brief Commande shell `listen` : active l'écoute radio avec filtres optionnels.
 *
 * Réinitialise les filtres précédents puis parse les nouveaux arguments :
 *  - "favoris" → active le filtre par liste de favoris
 *  - "@SRC"   → ajoute SRC au filtre source (sans le '@')
 *  - "#DST"   → ajoute DST au filtre destination (sans le '#')
 *
 * Ensuite, configure le modem en mode réception continue (pas de timeout,
 * pas de single receive) via l'API netdev.
 */
int listen_cmd(int argc, char **argv)
{
    /* Reset de tous les filtres existants */
    listen_filter_src_count = 0;
    listen_filter_dst_count = 0;
    listen_filter_favoris = 0;
    memset(listen_filter_srcs, 0, sizeof(listen_filter_srcs));
    memset(listen_filter_dsts, 0, sizeof(listen_filter_dsts));

    /* Parsing des arguments de filtrage */
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "favoris") == 0) {
            listen_filter_favoris = 1;
        }
        else if (argv[i][0] == '@' && listen_filter_src_count < MAX_LISTEN_FILTERS) {
            /* Stockage du nom sans le préfixe '@' */
            strncpy(listen_filter_srcs[listen_filter_src_count], argv[i] + 1,
                    sizeof(listen_filter_srcs[0]) - 1);
            listen_filter_srcs[listen_filter_src_count][sizeof(listen_filter_srcs[0]) - 1] = '\0';
            listen_filter_src_count++;
        }
        else if (argv[i][0] == '#' && listen_filter_dst_count < MAX_LISTEN_FILTERS) {
            /* Stockage du nom sans le préfixe '#' */
            strncpy(listen_filter_dsts[listen_filter_dst_count], argv[i] + 1,
                    sizeof(listen_filter_dsts[0]) - 1);
            listen_filter_dsts[listen_filter_dst_count][sizeof(listen_filter_dsts[0]) - 1] = '\0';
            listen_filter_dst_count++;
        }
    }

    /* Affichage du résumé des filtres actifs */
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

    /* Configuration du modem en mode réception continue */
    netdev_t *netdev = app_netdev();
    const netopt_enable_t single = false;  /* Réception répétée (pas single shot) */
    const uint32_t timeout = 0;            /* Pas de timeout → écoute infinie */
    netopt_state_t state = NETOPT_STATE_RX;

    netdev->driver->set(netdev, NETOPT_SINGLE_RECEIVE, &single, sizeof(single));
    netdev->driver->set(netdev, NETOPT_RX_TIMEOUT, &timeout, sizeof(timeout));
    netdev->driver->set(netdev, NETOPT_STATE, &state, sizeof(state));

    printf("Listen mode set\n");
    return 0;
}

/**
 * @brief Commande shell `memory` : affiche tous les messages stockés.
 *
 * Parcourt le buffer circulaire et affiche les entrées non vides.
 * Note : les messages sont affichés dans l'ordre d'indice du tableau,
 * pas nécessairement dans l'ordre chronologique de réception.
 */
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

/**
 * @brief Commande shell `favoris` : gestion de la liste de contacts favoris.
 *
 * La liste est stockée en RAM statique (perdue au reboot).
 * Chaque favori est une chaîne "@XXXX" (personne) ou "#XXXX" (salon),
 * avec exactement MAX_NAME_LEN caractères après le préfixe.
 *
 * Sous-commandes :
 *  - list   : affiche la liste actuelle
 *  - add    : ajoute un favori (vérifie le format et les doublons)
 *  - remove : retire un favori par décalage du tableau
 *  - clear  : efface toute la liste
 */
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
        /* Le favori doit commencer par '@' ou '#' suivi d'au moins un char */
        if ((argv[2][0] != '@' && argv[2][0] != '#') || argv[2][1] == '\0') {
            puts("favoris add: use @name for a person or #name for a channel.");
            return -1;
        }
        /* Le nom doit faire exactement MAX_NAME_LEN caractères */
        if (strlen(argv[2] + 1) != MAX_NAME_LEN) {
            printf("favoris add: name must be exactly %d characters.\n", MAX_NAME_LEN);
            return -1;
        }
        /* Vérifie que le favori n'est pas déjà dans la liste */
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

        /* Recherche linéaire puis décalage du tableau pour combler le trou */
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

/**
 * @brief Commande shell `filter` : recherche dans les messages stockés.
 *
 * Permet deux usages :
 *  1. Sans valeur : liste toutes les sources (ou destinations) distinctes
 *     trouvées dans le buffer. Utilise un sous-tableau "seen" pour la
 *     déduplication.
 *  2. Avec valeur : affiche tous les messages dont la source (ou destination)
 *     correspond à la valeur fournie.
 *
 * Usage : filter src [valeur] | filter dst [valeur]
 */
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
        /* Mode listing : on collecte toutes les valeurs uniques */
        static char seen[MAX_CONTACTS][ADDRESS_BUF_SIZE];
        int seen_count = 0;
        memset(seen, 0, sizeof(seen));

        for (int i = 0; i < MAX_STORED_MESSAGES; i++) {
            if (memory[i][0] == '\0') {
                continue;
            }
            if (_parse_field(memory[i], want_src, field, sizeof(field)) != 0) {
                continue;
            }

            /* Déduplication : on n'ajoute la valeur que si elle n'est pas déjà vue */
            int found = 0;
            for (int j = 0; j < seen_count; j++) {
                if (strcmp(seen[j], field) == 0) {
                    found = 1;
                    break;
                }
            }
            if (!found && seen_count < MAX_CONTACTS) {
                strncpy(seen[seen_count], field, sizeof(seen[seen_count]) - 1);
                seen[seen_count][sizeof(seen[seen_count]) - 1] = '\0';
                seen_count++;
            }
        }

        if (seen_count == 0) {
            printf("No %s found.\n", argv[1]);
        }
        else {
            printf("Known %s:\n", argv[1]);
            for (int i = 0; i < seen_count; i++) {
                printf("  %s\n", seen[i]);
            }
        }
    }
    else {
        /* Mode recherche : affiche les messages correspondant à la valeur demandée */
        const char *filter_val = argv[2];
        int count = 0;

        printf("Messages with %s=%s:\n", argv[1], filter_val);
        for (int i = 0; i < MAX_STORED_MESSAGES; i++) {
            if (memory[i][0] == '\0') {
                continue;
            }
            if (_parse_field(memory[i], want_src, field, sizeof(field)) != 0) {
                continue;
            }
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
