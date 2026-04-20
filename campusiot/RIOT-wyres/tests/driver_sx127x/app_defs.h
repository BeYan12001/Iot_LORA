/**
 * @file    app_defs.h
 * @brief   Constantes globales et types partagés par toute l'application LoRa.
 *
 * Ce fichier centralise les limites mémoire et le type de message routé
 * utilisés par les différents modules (radio, relais, stockage...).
 *
 * Format de message attendu :
 *   SSSS@DDDD:compteur[,ttl]:contenu   (message direct entre deux noeuds)
 *   SSSS#DDDD:compteur[,ttl]:contenu   (message de salon / broadcast)
 *
 * Exemple : B2MG#M2GI:5,3:Salut le groupe
 */

#ifndef APP_DEFS_H
#define APP_DEFS_H

#include <stdint.h>

/* --- Limites de l'application --- */

/** Longueur fixe des identifiants source et destination (ex: "B2MG", "YANN") */
#define MAX_NAME_LEN            (4)

/** Taille du buffer pour stocker un identifiant + terminateur nul */
#define NAME_BUF_SIZE           (MAX_NAME_LEN + 1)

/** Taille du buffer pour un identifiant préfixé par '@' ou '#' + nul */
#define ADDRESS_BUF_SIZE        (MAX_NAME_LEN + 2)

/** Nombre maximum de contacts distincts affichables par la commande filter */
#define MAX_CONTACTS            (40)

/** Nombre maximum d'entrées dans la liste des favoris */
#define MAX_FAVORIS             (10)

/** Nombre maximum de filtres src ou dst simultanés dans la commande listen */
#define MAX_LISTEN_FILTERS      (10)

/** Capacité du buffer circulaire de stockage des messages reçus */
#define MAX_STORED_MESSAGES     (100)

/** Taille maximale d'un message LoRa (payload complet header + contenu) */
#define MESSAGE_BUF_SIZE        (128)

/** Nombre de messages suivis simultanément par le mécanisme de relais */
#define MAX_RELAY_TRACKED       (32)

/**
 * @brief Structure représentant un message routé après parsing.
 *
 * Une fois un message brut reçu, on le décompose dans cette structure
 * pour faciliter l'accès à chaque champ sans reparser la chaîne en permanence.
 */
typedef struct {
    char src[NAME_BUF_SIZE];   /**< Identifiant de la source (4 chars + nul) */
    char dst[NAME_BUF_SIZE];   /**< Identifiant de la destination (4 chars + nul) */
    char sep;                  /**< Séparateur : '@' pour personne, '#' pour salon */
    int counter;               /**< Numéro de séquence du message */
    int ttl;                   /**< Durée de vie (Time To Live) pour le relais */
    int has_ttl;               /**< 1 si le champ ttl est présent dans le message, 0 sinon */
    const char *body;          /**< Pointeur vers le début du contenu (pas de copie) */
} routed_message_t;

#endif /* APP_DEFS_H */
