# Application de test du driver SX127x — Communication LoRa multi-sauts

Application de test du driver SX127x (modem LoRa) sur carte **Wyres-Base** sous
[RIOT-OS](https://riot-os.org/). Elle implémente une messagerie LoRa simple avec
format d'adressage structuré, stockage en mémoire, filtrage, gestion de favoris,
et relais automatique multi-sauts basé sur le SNR.

---

## Format des messages

Chaque message suit le format :

```
SSSS@DDDD:compteur[,ttl]:contenu    (message personnel)
SSSS#DDDD:compteur[,ttl]:contenu    (message de salon)
```

| Champ      | Taille   | Description                                          |
|------------|----------|------------------------------------------------------|
| `SSSS`     | 4 cars.  | Identifiant source (ex: `B2MG`)                      |
| `@` ou `#` | 1 car.   | Séparateur : `@` personne, `#` salon (broadcast)     |
| `DDDD`     | 4 cars.  | Identifiant destination (ex: `YANN`, `M2GI`, `FFFF`) |
| `compteur` | entier   | Numéro de séquence du message (auto-incrémenté)      |
| `ttl`      | entier   | Durée de vie pour le relais multi-sauts (optionnel)  |
| `contenu`  | libre    | Texte du message                                     |

> Utiliser `FFFF` comme destination pour un message broadcast (tous les noeuds).

Exemples :
```
B2MG#M2GI:1:Bonjour tout le monde
B2MG@YANN:2,3:Message avec 3 sauts max
```

---

## Compilation et flash

```bash
export BOARD=wyres-base
export EXTERNAL_BOARD_DIRS=~/Documents/M2/Iot_LORA/campusiot/RIOT-wyres/boards

cd ~/Documents/M2/Iot_LORA/campusiot/RIOT-wyres/tests/driver_sx127x

# Compiler
make -j4

# Compiler et flasher
make -j4 flash
```

### Ouvrir le terminal série

```bash
tio -b 115200 -m INLCRNL /dev/serial/by-id/usb-STMicroelectronics_STLINK-V3_<ID>-if01
```

Pour trouver le port :
```bash
lsusb
tio -L
```

---

## Test rapide — communication entre 2 cartes

### Carte réceptrice
```
> init
> setup 125 7 5
> syncword set 34
> crc set 1
> implicit set 0
> channel set 868000000
> listen
```

### Carte émettrice
```
> init
> setup 125 7 5
> syncword set 34
> crc set 1
> implicit set 0
> channel set 868000000
> send Bonjour
```

### Résultat sur la carte réceptrice
```
{Payload: "B2MG#M2G:1,5:Bonjour" (21 bytes), RSSI: -45, SNR: 9, TOA: 123456}
```

---

## Commandes disponibles


### `send`
Envoyer un message. La source, la destination et le compteur sont gérés
automatiquement. Le format envoyé est `SSSS#DDDD:compteur,ttl:contenu`.
```
> send <contenu du message>
```
Exemple :
```
> send Bonjour le groupe
```

### `listen`
Passer en mode écoute. Accepte des filtres optionnels.
```
> listen                        # Tous les messages
> listen @B2MG                  # Seulement les messages de B2MG
> listen #M2GI                  # Seulement les messages vers #M2GI
> listen favoris                # Seulement les messages des/vers favoris
> listen favoris @B2MG #M2GI   # Combinaison
```

---

## Stockage et recherche de messages

### `memory`
Affiche tous les messages reçus stockés en RAM (buffer circulaire de 100 entrées).
```
> memory
```
Sortie :
```
Stored messages:
[0] B2MG#M2GI:1,5:Bonjour
[1] B2MG@YANN:2:test
```

### `filter`
Cherche dans les messages stockés par source ou destination.
```
> filter src              # Lister toutes les sources connues
> filter dst              # Lister toutes les destinations connues
> filter src B2MG         # Messages de la source B2MG
> filter dst #M2GI        # Messages vers le salon #M2GI
```

---

## Gestion des favoris

La commande `favoris` maintient une liste de contacts en RAM (max 10 entrées).
Le préfixe `@` désigne une personne, `#` un salon.

```
> favoris list             # Afficher la liste
> favoris add @YANN        # Ajouter une personne
> favoris add #M2GI        # Ajouter un salon
> favoris remove @YANN     # Retirer un favori
> favoris clear            # Vider toute la liste
```

---

## Relais multi-sauts

Le noeud peut relayer automatiquement les messages qui contiennent un champ `ttl`.
Le relais se déclenche uniquement si le SNR reçu est **inférieur ou égal** au seuil
configuré (message « lointain »), évitant que les noeuds proches de l'émetteur
ne relaie inutilement.

À chaque relais, le `ttl` est décrémenté. Le message n'est plus relayé quand
`ttl == 0`.

### `threshold`
Afficher ou modifier le seuil SNR de déclenchement du relais.
```
> threshold          # Afficher le seuil actuel
> threshold get
> threshold set 5    # Définir le seuil à 5
```

---






### Brouillon

## Connecter la carte

Chercher la carte:
lsusb
tio -L



export BOARD=wyres-base
export EXTERNAL_BOARD_DIRS=~/Documents/M2/Iot_LORA/campusiot/RIOT-wyres/boards

cd ~/Documents/M2/Iot_LORA/campusiot/RIOT-wyres/tests/driver_sx127x

make -j 4 flash

tio -b 115200 -m INLCRNL /dev/serial/by-id/usb-STMicroelectronics_STLINK-V3_002E00223232511939353236-if01


init
setup 125 7 5
syncword set 34
crc set 1
implicit set 0
send HELOOOOO
listen


Repeter un message pluiseurs fois et filtrer pour ne pas prendre ceux repeter plus de 5 messages.
Utiliser un protocole de Diffi Helleman
ON a une valeur de SNR, on repete le message avec les messages qui ont loin avec un tirage aleatoire tout en regardant la qualité du (SNR eleve = proche ) 

message : @src@dst:[0-100]:keypublique: message
[0-100] nombre de fois de la repitition du message.

Pseudo special FFFF pour envoyer a tous le monde
Les salon #M2GI au lieu de @dest
nombre de saut 9 max
message formater
key_publique en base64, library dans RIOT
Key_salon : AES128 c'est un PSK


On peut mettre en place un mecanisme de TraceRoute
Lister les contact quon a decouvert. on nettoie on virant les moins recent (plus ancien) (table de 40 ) 
send



Soutenance : 

10 min par groupe : 
- 4 min de presentation avec slides ( max 4)
- 3 min de démo

Il faut déposer le repo ( ou le rendre accesible ) avec un petit readme et les fonctions commentées. Pas de ChatGPT

Présenter les commandes / fonctionnalité. 



A FAIRE :
- EEPROM
- Relay avec SNR et ttl
- Favoris
- Memory
- Filter