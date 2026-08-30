# Pont Sans Fil ESP32-S3 pour Pédale TONEX

## 1. Vue d'ensemble & Objectif

L'objectif de ce sous-projet est de transformer le **TONEX Pedal Controller** en un appareil autonome, sans fil, alimenté directement par le pédalier.

En connectant une carte microcontrôleur **ESP32-S3** à la pédale TONEX via USB, l'ESP32 agit comme un serveur web autonome, un pont WebSocket et un hôte USB (USB Host). Cela permet la navigation, l'édition, la synchronisation de la bibliothèque et le changement de presets en temps réel depuis n'importe quel appareil connecté au réseau local (iPad sur le canapé, smartphone sur scène, ou navigateur d'ordinateur) avec **une latence imperceptible** et **sans nécessiter d'ordinateur hôte**.

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
│  • Wi-Fi AP + Mode Station + mDNS (tonex.local)             │
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

### 4.2 Organisation des Fichiers Firmware

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
Envoie les presets découverts pour peupler la bibliothèque :
```json
{
  "event": "preset_data",
  "presets": {
    "0_A": { "name": "Clean Crunch", "amp": true, "cab": true },
    "0_B": { "name": "Heavy Lead", "amp": true, "cab": false }
  }
}
```

---

## 6. Intégration Frontend (`index.html`)

L'application [`index.html`](file:///Users/brandon/Documents/repos/tonexpedal/index.html) intègre une couche d'abstraction qui détecte automatiquement son environnement :
- **Servie depuis l'ESP32** : Elle utilise le pont WebSocket pour piloter la ToneX sans fil.
- **Ouverte localement (`file:///` ou PC local)** : Elle utilise les API Web MIDI et Web Serial natives sans aucune régression.
