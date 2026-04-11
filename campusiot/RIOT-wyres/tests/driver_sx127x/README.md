# Simple test of driver

## Build

```bash
make
make BOARD=wyres-base DRIVER=sx1272 -j 16
```

## Usage

In term type help to see commands

type command to see how to use

basic use to check communication between 2 boards :

* On receving board :
```
> setup 125 12 5
> channel set 868000000
> listen
```

* On seending board :
```
> setup 125 12 5
> channel set 868000000
> send This\ is\ RIOT!
```

* On receiving board you should see :
```
{Payload: "This is RIOT!" (13 bytes), RSSI: 103, SNR: 240}
```

## Commande `memory`

La commande `memory` affiche tous les messages LoRa reçus et stockés en mémoire (jusqu'à 100 messages).

Les messages sont sauvegardés automatiquement à chaque réception. Ils sont conservés dans un buffer circulaire de 100 entrées.

**Usage :**
```
> memory
```

**Exemple de sortie :**
```
Stored messages:
[0] B2MG#M2GI:1:HELOOOOO
[1] B2MG@YANN:2:test
```

---

## Commande `filter`

La commande `filter` permet de rechercher dans les messages stockés en mémoire par source (`src`) ou destination (`dst`).

Le format attendu des messages est : `src[4]@dst[4]:compteur:contenu`
ou `src[4]#dst[4]:compteur:contenu`

**Lister toutes les sources connues :**
```
> filter src
```

**Lister toutes les destinations connues :**
```
> filter dst
```

**Afficher les messages d'une source précise :**
```
> filter src B2MG
```

**Afficher les messages vers une destination précise :**
```
> filter dst #M2GI
```

**Exemple de sortie :**
```
Messages with src=B2MG:
[0] B2MG#M2GI:1:HELOOOOO
[1] B2MG@YANN:2:test
```

---

## Commande `favoris`

La commande `favoris` permet de maintenir une petite liste de contacts favoris en RAM.
Un favori doit obligatoirement commencer par `@` pour une personne, ou `#` pour un salon.

**Afficher les favoris :**
```bash
> favoris list
```

**Ajouter un favori :**
```bash
> favoris add @YANN
> favoris add #M2GI
```

**Retirer un favori :**
```bash
> favoris remove @YANN
```

**Vider toute la liste :**
```bash
> favoris clear
```

---

## Écoute filtrée par favoris

La commande `listen` accepte maintenant le mot-clé `favoris`.
Dans ce mode, seuls les messages dont la source correspond à un favori de type `@personne`
ou dont la destination correspond à un favori de type `#salon` sont affichés.
Les identifiants source et destination font exactement 4 caractères.

**Exemple :**
```bash
> favoris add @YANN
> favoris add #M2GI
> listen favoris
```

Il reste possible de combiner `favoris` avec les filtres existants `@src` et `#dst`.

---

## Relais multi-sauts simple

Le programme accepte maintenant aussi les messages au format :
`src[4]@dst[4]:compteur,ttl:contenu`
ou
`src[4]#dst[4]:compteur,ttl:contenu`

Exemple :
```bash
B2MG#M2GI:18,3:Salut en multi-sauts
```

Si un message reçu contient un `ttl`, le noeud peut le relayer immédiatement avec
un `ttl` décrémenté, mais seulement si le `SNR` reçu est inférieur ou égal au seuil
configuré.

## Commande `threshold`

La commande `threshold` permet d'afficher ou modifier le seuil `SNRThreshold`
utilisé pour décider si un message doit être relayé.

**Afficher la valeur courante :**
```bash
> threshold
> threshold get
```

**Modifier le seuil :**
```bash
> threshold set 5
```










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