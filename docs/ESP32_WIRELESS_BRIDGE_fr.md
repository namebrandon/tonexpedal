# Pont Sans Fil ESP32-S3 pour Pédale TONEX

## 1. Vue d'ensemble & Objectif

L'objectif de ce sous-projet est de transformer le **TONEX Pedal Controller** en un appareil autonome, sans fil, alimenté directement par le pédalier.

En connectant une carte microcontrôleur **ESP32-S3** à la pédale TONEX via USB, l'ESP32 agit comme un serveur web autonome, un pont WebSocket et un hôte USB (USB Host). Cela permet la navigation, l'édition, la synchronisation de la bibliothèque et le changement de presets en temps réel depuis n'importe quel appareil connecté au réseau local (iPad sur le canapé, smartphone sur scène, ou navigateur d'ordinateur) avec **une latence imperceptible** et **sans nécessiter d'ordinateur hôte**.

> **État de l'implémentation :** l'hébergement WLAN, la découverte du pont, les messages WebSocket, l'énumération physique USB et la sortie USB-MIDI sont implémentés. L'USB-MIDI doit encore être validé avec les descripteurs réels de la pédale. La synchronisation CDC reste indisponible tant que ses interfaces n'ont pas été revendiquées et validées.

```
┌────────────────────────────────┐
│   iPad / iPhone / PC           │
│   (Navigateur Safari / Chrome) │
│   http://tonex.local           │
└───────────────┬────────────────┘
                │ Wi-Fi (HTTP + WebSocket JSON / Binaire)
                ▼
┌─────────────────────────────────────────────────────────────┐
│   Contrôleur ESP32-S3-DevKit (N16R8)                        │
│                                                             │
│  • Serveur Web intégré (sert index.html depuis LittleFS)    │
│  • Serveur WebSocket (traite les commandes en ~1-2ms)       │
│  • Contrôleur USB Host (dialogue avec le hardware ToneX)    │
│  • Mode station Wi-Fi + mDNS (tonex.local)                  │
│  • LED RGB d'état (GPIO48)                                  │
└───────────────────────────────┬─────────────────────────────┘
                                │ Câble USB (Type-C vers Type-B/C)
                                │ USB Host natif (GPIO 19/20)
                                ▼
┌─────────────────────────────────────────────────────────────┐
│   Pédale IK Multimedia TONEX                                │
│   • USB-MIDI (Bank Select CC#0 + Program Change)            │
│   • USB-CDC Série (Protocole HDLC binaire pour presets)     │
└─────────────────────────────────────────────────────────────┘
```

---

## 2. Spécifications Matérielles

### 2.1 Carte Cible : ESP32-S3-DevKit N16R8 (Double Type-C)
* **Matériel de Référence** : [Carte de Développement ESP32-S3-DevKit N16R8 Double Type-C (Amazon)](https://www.amazon.com/dp/B0GBT212KM) (également connue sous le nom YD-ESP32-S3).
* **Microcontrôleur** : Espressif ESP32-S3 (Dual-core 32-bit Xtensa LX7 @ 240 MHz).
* **Mémoire Flash** : **16 Mo (N16)** Quad-SPI Flash (stocke le firmware + LittleFS pour les fichiers web).
* **PSRAM** : **8 Mo (R8)** Octal-SPI PSRAM (mise en mémoire tampon ultra-rapide pour WebSockets et bibliothèque).
* **Sans Fil** : Wi-Fi 2.4 GHz (802.11 b/g/n) avec antenne PCB intégrée (portée de 15 à 30 m) + Bluetooth 5 (LE).
* **Consommation** : ~0,4W – 0,8W (5V @ 80–150 mA).

### 2.2 Ports et Brochage

| Interface | Connecteur Physique / Broches | Fonction |
| :--- | :--- | :--- |
| **Alimentation / Debug** | **Port Type-C Gauche** (USB-to-UART) | Entrée d'alimentation 5V + programmation série/logs. |
| **Hôte USB ToneX** | **Port Type-C Droit** (USB OTG Natif) | Hôte USB matériel connecté à la ToneX (`GPIO19 = D-`, `GPIO20 = D+`). |
| **LED d'état RGB** | `GPIO48` | WS2812 NeoPixel pour l'état de connexion et de synchronisation. |
| **Alimentation Aux.** | Broches `5V` & `GND` | Entrée DC alternative pour intégration directe d'un régulateur 9V→5V. |

---

## 3. Architecture d'Alimentation

La pédale TONEX est un périphérique USB esclave et **ne fournit pas d'alimentation 5V sur son port USB**. L'ESP32-S3 agit en tant qu'hôte USB et doit être alimenté en 5V DC.

### Options Recommandées :
1. **Dérivation 9V DC depuis l'alimentation ToneX (Zéro prise supplémentaire)** :
   * L'adaptateur secteur TONEX d'origine fournit du 9V DC sous 3,2A (3200mA). La pédale ne consomme que ~350mA.
   * Un câble en Y DC standard 2,1mm relié à un convertisseur abaisseur (buck) 9V→5V compact permet d'alimenter la pédale et l'ESP32 depuis une seule prise secteur.
2. **Alimentation de Pédalier** :
   * Connexion à une sortie 5V USB isolée (ex. Cioks Crux, Strymon Zuma/Ojai avec adaptateur 5V).
3. **Chargeur USB-C / Batterie Externe Standard** :
   * Branché sur le port UART/Power Type-C.

---

## 4. Architecture Logicielle & Firmware

### 4.1 Environnement de Développement
* **Plateforme** : PlatformIO (recommandé) ou Arduino IDE 2.x avec support ESP32 (v3.x / ESP-IDF 5.x).
* **Système de fichiers** : Partition `LittleFS` allouée sur la flash 16 Mo.

### 4.2 Configuration WLAN

Le pont rejoint normalement un WLAN existant et reçoit son adresse par DHCP. Il ne crée pas de point d'accès pendant le fonctionnement normal.

1. Copier `firmware/include/wifi_secrets.example.h` vers `firmware/include/wifi_secrets.h`.
2. Renseigner `TONEX_WIFI_SSID` et `TONEX_WIFI_PASS` dans le fichier copié.
3. Flasher le firmware et les données LittleFS.
4. Relever l'adresse IP dans le journal série, ou ouvrir `http://tonex.local` depuis un autre appareil connecté au même WLAN.

Le fichier d'identifiants est ignoré par Git. Le point d'accès de secours et le provisionnement par navigateur sont réservés à un futur mode de récupération.

### 4.3 Organisation des Fichiers Firmware

```
firmware/
├── include/
│   ├── config.h             # Configuration Wi-Fi, nom d'hôte mDNS, broches
│   ├── tonex_usb_host.h     # Pilote USB Host pour endpoints MIDI & CDC
│   ├── tonex_hdlc.h         # Découpage HDLC, byte stuffing, CRC-CCITT pour ESP32
│   ├── ws_bridge.h          # Serveur WebSocket et répartiteur JSON/Binaire
│   └── led_status.h         # Contrôleur d'indicateur d'état LED RGB WS2812
├── src/
│   ├── main.cpp             # Initialisation, Wi-Fi, mDNS, tâches principales
│   ├── tonex_usb_host.cpp   # Énumération USB Host, sortie MIDI, lecture/écriture CDC
│   ├── tonex_hdlc.cpp       # Portage CRC-CCITT et analyseur de paquets
│   ├── ws_bridge.cpp        # Gestionnaire de clients WebSocket
│   └── led_status.cpp       # Gestionnaire de retours visuels
├── data/                    # Fichiers web téléversés sur LittleFS
│   ├── index.html           # Application web avec adaptateur WS
│   └── favicon.svg          # Icône de l'application
└── platformio.ini           # Dépendances, drapeaux de compilation et partitions
```

---

## 5. Protocole de Communication (Schéma WebSocket)

L'iPad et l'ESP32 communiquent en temps réel via une connexion WebSocket persistante sur `ws://tonex.local/ws`.

Avant d'ouvrir le socket, le frontend partagé appelle `GET /api/bridge`. Le pont s'identifie avec :
```json
{
  "service": "tonex-bridge",
  "protocol_version": 1
}
```
Cela évite qu'une copie servie par un serveur HTTP ordinaire tente continuellement d'ouvrir un WebSocket.

### 5.1 Client -> ESP32 (Commandes)

#### 1. Bank Select MIDI & Program Change
Envoyé lors d'un clic sur un preset :
```json
{
  "action": "midi_send",
  "bank": 0,
  "slot": "A",
  "pc": 0,
  "channel": 0
}
```

#### 2. Lancer la Synchronisation USB Complète
Envoyé lors du clic sur "Sync USB" :
```json
{
  "action": "sync_start"
}
```

#### 3. Annuler la Synchronisation
Envoyé lors d'un second clic sur le bouton pendant une synchronisation active :
```json
{
  "action": "sync_cancel"
}
```

---

### 5.2 ESP32 -> Client (Événements & Mises à Jour)

#### 1. État de Connexion
Diffusé lors de la connexion du client ou de la pédale :
```json
{
  "event": "status",
  "tonex_connected": true,
  "active_pc": 0,
  "active_bank": 0,
  "active_slot": "A"
}
```

#### 2. Progression de la Synchronisation
Diffusé pendant la lecture des 150 presets :
```json
{
  "event": "sync_progress",
  "loaded": 45,
  "total": 150,
  "percent": 30
}
```

#### 3. Données des Presets
Envoie chaque preset découvert pour peupler la bibliothèque :
```json
{
  "event": "preset_update",
  "bank": 0,
  "slot": "A",
  "name": "Clean Crunch",
  "amp": true,
  "cab": true
}
```

#### 4. Fin, Annulation et Erreurs
Le pont émet `sync_complete` après tous les presets, `sync_cancelled` après une annulation, ou un événement `error` structuré lorsqu'une commande échoue :
```json
{ "event": "sync_complete", "total": 150 }
{ "event": "sync_cancelled" }
{ "event": "error", "code": "sync_unavailable", "message": "Preset sync is already active or the TONEX is disconnected" }
```

---

## 6. Intégration Frontend (`index.html`)

L'application `index.html` utilise des adaptateurs explicites pour le matériel local et le WebSocket. Elle n'active le transport du pont qu'après identification positive du serveur via `GET /api/bridge`.

- **Servie depuis l'ESP32** : elle utilise le pont WebSocket pour piloter la TONEX sans fil.
- **Ouverte localement ou servie par un autre serveur** : elle conserve les API Web MIDI et Web Serial/WebUSB natives.

---

## 7. Feuille de Route d'Implémentation

| Phase | Étape Clé | Livrables |
| :--- | :--- | :--- |
| **Phase 1** | **Structure & Documentation** | `docs/ESP32_WIRELESS_BRIDGE_fr.md`, structure `firmware/`. |
| **Phase 2** | **Cœur Firmware & Hôte USB** | Pilote ESP32-S3 USB Host pour TONEX MIDI & lecteur série CDC HDLC. |
| **Phase 3** | **Serveur Web & WebSocket** | AsyncWebServer servant `index.html` via LittleFS, mDNS `tonex.local`. |
| **Phase 4** | **Adaptateur Frontend** | Couche transport WebSocket dans `index.html` pour contrôle temps réel. |
| **Phase 5** | **Boîtier & Tests Terrain** | Boîtier imprimé en 3D, validation de l'alimentation intégrée. |

---

## 8. Guide du Boîtier Imprimable en 3D

* **Modèle 3D Prêt à Imprimer** : [Boîtier pour YD-ESP32-S3 N16R8 sur Printables](https://www.printables.com/model/1774744-case-for-yd-esp32-s3-n16r8) (boîtier clipsable testé avec accès aux deux ports Type-C et boutons).
* **Dimensions** : ~65 mm (L) × 32 mm (l) × 14 mm (H).
* **Ouvertures** :
  * Découpes pour double port Type-C sur la face inférieure.
  * Trou de diffusion ou guide optique de 2 mm pour la LED RGB `GPIO48`.
  * Micro-fentes d'aération sur les capots supérieur/inférieur.
* **Fixation** : Couvercle clipsable avec empreinte Velcro Dual-Lock optionnelle pour montage sous ou derrière la pédale TONEX.
