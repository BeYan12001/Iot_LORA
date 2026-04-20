/**
 * @file    relay.h
 * @brief   Mécanisme de relais multi-sauts basé sur le SNR et le TTL.
 *
 * Lorsqu'un message contient un champ TTL, ce module décide s'il doit être
 * retransmis en fonction du SNR mesuré à la réception.
 *
 * Logique de décision :
 *  - SNR <= seuil  → le noeud est « loin » de l'émetteur → il relaie
 *  - SNR >  seuil  → le noeud est « proche »             → il ne relaie pas
 *
 * Cela évite que plusieurs noeuds proches de l'émetteur ne relaie tous
 * en même temps. Seuls les noeuds en bordure de couverture relaieront.
 *
 * Un message ne peut être relayé qu'une seule fois par noeud (anti-boucle).
 * Le TTL est décrémenté à chaque saut. Quand TTL == 0, le message n'est plus relayé.
 */

#ifndef RELAY_H
#define RELAY_H

#include <stdint.h>

/**
 * @brief Affiche ou modifie le seuil SNR de déclenchement du relais.
 *
 * Usage : threshold [get | set <valeur>]
 */
int threshold_cmd(int argc, char **argv);

/**
 * @brief Évalue si un message reçu doit être relayé et l'envoie si nécessaire.
 *
 * Appelée à chaque réception par receiver.c. Si le message ne contient pas
 * de TTL, la fonction retourne immédiatement sans rien faire.
 *
 * @param rx_msg Le message brut reçu (null-terminé).
 * @param snr    Le SNR mesuré à la réception du paquet (en dB, signé).
 */
void app_maybe_relay_message(const char *rx_msg, int8_t snr);

#endif /* RELAY_H */
