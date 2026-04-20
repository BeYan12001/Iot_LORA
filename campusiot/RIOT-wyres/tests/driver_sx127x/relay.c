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
 * @file    relay.c
 * @brief   Mécanisme de relais multi-sauts LoRa basé sur le SNR et le TTL.
 *
 * Principe de fonctionnement :
 *  Lorsqu'un message est reçu avec un champ TTL > 0, chaque noeud du réseau
 *  peut décider de le retransmettre pour étendre sa portée.
 *
 *  Pour éviter que tout le monde relaie en même temps (collision radio),
 *  seuls les noeuds « loin » de l'émetteur original relaie. On mesure
 *  la distance indirectement via le SNR :
 *   - SNR élevé  → signal fort → noeud proche → il ne relaie PAS
 *   - SNR faible → signal faible → noeud loin  → il RELAIE
 *
 *  Le seuil est configurable via la commande `threshold`.
*/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "app_defs.h"
#include "radio_commands.h"
#include "relay.h"

/**
 * @brief Structure interne de suivi pour l'anti-boucle.
 *
 * Pour chaque message (src+dst+sep+counter) on retient :
 *  - le meilleur SNR observé (utile si le même message arrive par plusieurs chemins)
 *  - si on l'a déjà relayé
 */
typedef struct {
    char src[NAME_BUF_SIZE];  /**< Source du message */
    char dst[NAME_BUF_SIZE];  /**< Destination du message */
    char sep;                 /**< Séparateur '@' ou '#' */
    int counter;              /**< Numéro de séquence du message */
    int8_t best_snr;          /**< Meilleur SNR observé pour ce message */
    uint8_t relayed;          /**< 1 si ce noeud a déjà relayé ce message */
    uint8_t used;             /**< 1 si cette entrée du tableau est utilisée */
} relay_entry_t;

/** Table de suivi des messages en cours (anti-boucle) */
static relay_entry_t relay_entries[MAX_RELAY_TRACKED];

/** Seuil SNR en dessous duquel le relais est déclenché (configurable via threshold) */
static int8_t snr_threshold = 0;

/**
 * @brief Parse un message brut et remplit une structure routed_message_t.
 *
 * Vérifie le format SSSS[@#]DDDD:compteur[,ttl]:contenu et extrait chaque champ.
 * Si le champ ttl est absent, has_ttl est mis à 0 et le message ne sera pas relayé.
 *
 * @param msg    Message brut reçu (null-terminé).
 * @param parsed Structure de sortie à remplir.
 * @return 0 si le parsing réussit, -1 si le format est invalide.
 */
static int _parse_routed_message(const char *msg, routed_message_t *parsed)
{
    const char *meta; 
    const char *body;   
    long counter;
    long ttl = 0;
    char *endptr;

    if (strlen(msg) < (size_t)(MAX_NAME_LEN * 2 + 2)) {
        return -1;
    }

    /* Vérification du séparateur entre src et dst */
    if (msg[MAX_NAME_LEN] != '@' && msg[MAX_NAME_LEN] != '#') {
        return -1;
    }

    /* Vérification du ':' après dst */
    if (msg[(MAX_NAME_LEN * 2) + 1] != ':') {
        return -1;
    }

    /* Extraction de src, sep, dst */
    memcpy(parsed->src, msg, MAX_NAME_LEN);
    parsed->src[MAX_NAME_LEN] = '\0';
    parsed->sep = msg[MAX_NAME_LEN];
    memcpy(parsed->dst, msg + MAX_NAME_LEN + 1, MAX_NAME_LEN);
    parsed->dst[MAX_NAME_LEN] = '\0';

    /* meta pointe sur "compteur[,ttl]:contenu" */
    meta = msg + (MAX_NAME_LEN * 2) + 2;
    body = strchr(meta, ':');  /* Cherche le ':' qui précède le contenu */
    if (body == NULL || body == meta) {
        return -1;
    }

    /* Parse le compteur (nombre décimal) */
    counter = strtol(meta, &endptr, 10);
    if (endptr == meta || counter < 0) {
        return -1;
    }

    parsed->counter = (int)counter;
    parsed->ttl = 0;
    parsed->has_ttl = 0;

    /* Si un ',' suit le compteur, on parse le TTL */
    if (*endptr == ',') {
        ttl = strtol(endptr + 1, &endptr, 10);
        if (ttl < 0) {
            return -1;
        }
        parsed->ttl = (int)ttl;
        parsed->has_ttl = 1;
    }

    /* endptr doit pointer exactement sur le ':' du body */
    if (endptr != body) {
        return -1;
    }

    parsed->body = body + 1;  /* Contenu (pas de copie, juste un pointeur) */
    return 0;
}

/**
 * @brief Cherche une entrée existante dans relay_entries[] pour ce message.
 *
 * L'identité d'un message est définie par (src, dst, sep, counter).
 * Si ce tuple est déjà connu, on retourne l'entrée existante pour mettre
 * à jour le best_snr ou vérifier si on a déjà relayé.
 *
 * @param parsed Message parsé à rechercher.
 * @return Pointeur vers l'entrée existante, ou NULL si non trouvée.
 */
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

/**
 * @brief Trouve ou crée une entrée dans relay_entries[] pour ce message.
 *
 * Si le message est déjà connu, retourne l'entrée existante.
 * Sinon, cherche un slot libre. Si le tableau est plein, écrase le slot 0
 * (comportement dégradé acceptable dans notre contexte embarqué).
 *
 * @param parsed Message parsé.
 * @return Pointeur vers l'entrée allouée (jamais NULL).
 */
static relay_entry_t *_get_or_create_relay_entry(const routed_message_t *parsed)
{
    relay_entry_t *slot = _find_relay_entry(parsed);

    if (slot != NULL) {
        return slot;  /* Message déjà connu, on retourne l'entrée existante */
    }

    /* Recherche d'un slot libre dans la table */
    for (int i = 0; i < MAX_RELAY_TRACKED; i++) {
        if (relay_entries[i].used) {
            continue;
        }

        relay_entries[i].used = 1;
        relay_entries[i].relayed = 0;
        relay_entries[i].best_snr = -128;  /* Pire SNR possible au départ */
        relay_entries[i].sep = parsed->sep;
        relay_entries[i].counter = parsed->counter;
        strncpy(relay_entries[i].src, parsed->src, sizeof(relay_entries[i].src) - 1);
        relay_entries[i].src[sizeof(relay_entries[i].src) - 1] = '\0';
        strncpy(relay_entries[i].dst, parsed->dst, sizeof(relay_entries[i].dst) - 1);
        relay_entries[i].dst[sizeof(relay_entries[i].dst) - 1] = '\0';
        return &relay_entries[i];
    }

    /* Table pleine : on écrase le slot 0 (évite un blocage complet) */
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

/**
 * @brief Commande shell `threshold` : affiche ou modifie le seuil SNR de relais.
 *
 * Le seuil est un entier signé 8 bits (range -128..127).
 * Usage :
 *   threshold        → affiche le seuil actuel
 *   threshold get    → identique
 *   threshold set 5  → définit le seuil à 5
 */
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

    /* Conversion en entier avec vérification de la plage int8_t */
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

/**
 * @brief Évalue si un message reçu doit être relayé et l'envoie si nécessaire.
 *
 * Appelée par receiver.c à chaque réception. Algorithme :
 *  1. Parse le message — si pas de TTL, on sort immédiatement.
 *  2. Cherche/crée une entrée dans relay_entries[] pour ce message.
 *  3. Met à jour best_snr si le SNR courant est meilleur.
 *  4. Si déjà relayé par ce noeud → sortie.
 *  5. Si best_snr > seuil → signal trop fort → on est proche → on ne relaie pas.
 *  6. Si TTL <= 0 → plus de sauts disponibles → on ne relaie pas.
 *  7. Sinon : construit le nouveau message (TTL décrémenté) et l'envoie.
 *
 * @param rx_msg Message brut reçu.
 * @param snr    SNR mesuré à la réception (en dB, signé).
 */
void app_maybe_relay_message(const char *rx_msg, int8_t snr)
{
    routed_message_t parsed;
    relay_entry_t *entry;
    char relay_payload[MESSAGE_BUF_SIZE];
    int next_ttl;
    int written;

    /* Étape 1 : parsing du message — on ignore les messages sans TTL */
    if (_parse_routed_message(rx_msg, &parsed) != 0 || !parsed.has_ttl) {
        return;
    }

    /* Étape 2 : récupération de l'entrée de suivi */
    entry = _get_or_create_relay_entry(&parsed);
    if (entry == NULL) {
        puts("relay: no slot available");
        return;
    }

    /* Étape 3 : mise à jour du meilleur SNR observé pour ce message */
    if (snr > entry->best_snr) {
        entry->best_snr = snr;
    }

    /* Étape 4 : anti-boucle — on ne relaie qu'une seule fois par message */
    if (entry->relayed) {
        return;
    }

    /* Étape 5 : si SNR trop bon, on est trop proche de l'émetteur → pas de relais */
    if (entry->best_snr > snr_threshold) {
        printf("relay: skip %s%c%s:%d because SNR %d > threshold %d\n",
               parsed.src, parsed.sep, parsed.dst, parsed.counter,
               entry->best_snr, snr_threshold);
        return;
    }

    /* Étape 6 : TTL épuisé → on ne peut plus relayer */
    if (parsed.ttl <= 0) {
        return;
    }

    /* Étape 7 : construction du message relayé avec TTL décrémenté */
    next_ttl = parsed.ttl - 1;
    written = snprintf(relay_payload, sizeof(relay_payload),
                       "%s%c%s:%d,%d:%s",
                       parsed.src, parsed.sep, parsed.dst,
                       parsed.counter, next_ttl, parsed.body);
    if (written < 0 || (size_t)written >= sizeof(relay_payload)) {
        puts("relay: payload too long");
        return;
    }

    if (app_send_payload(relay_payload) == 0) {
        entry->relayed = 1;
        printf("relay: forwarded with ttl %d -> %d\n", parsed.ttl, next_ttl);
    }
}
