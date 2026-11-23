# TP — Piloter une LED RGB par requêtes HTTP (M5Stack Tab5 + Node-RED)

Travaux pratiques sur le **protocole HTTP** et les **API REST** à partir d'un objet connecté simple : une LED RGB raccordée à une tablette **M5Stack Tab5** et commandée de trois façons :

1. **localement**, avec les boutons tactiles de l'écran du Tab5 ;
2. depuis un **site web embarqué** dans le Tab5 (`http://IP_du_Tab5/`) ;
3. depuis **Node-RED**, qui envoie des requêtes HTTP au Tab5 et publie sa propre page web de commande.

Les trois moyens de commande restent synchronisés : la page web et Node-RED interrogent régulièrement l'état de la LED.

| Public | Durée | Particularités |
|---|---|---|
| **Terminale STI2D – SIN** | ≈ 3 h | LED à cathode commune, calculs avec la loi d'Ohm U = R × I |
| **BTS CIEL ER – 1re année** | ≈ 4 h | Cathode commune **et** anode commune, dimensionnement complet, ajout d'une route `PUT` |

---

## Contenu du dépôt

```
.
├── README.md
├── TP_HTTP_LED_RGB_Tab5/
│   └── TP_HTTP_LED_RGB_Tab5.ino            # Programme Arduino du Tab5
├── nodered/
│   └── flow_nodered_TP_LED_RGB.json        # Flow Node-RED à importer
├── presentation/
│   └── TP_HTTP_LED_RGB_Tab5.pptx           # Présentation (16 diapositives, FR)
└── guides/
    ├── Guide_TP_HTTP_LED_RGB_STI2D_SIN.docx     # Guide élève Terminale STI2D SIN
    └── Guide_TP_HTTP_LED_RGB_BTS_CIEL_ER1.docx  # Guide élève BTS CIEL ER 1
```

---

## Objectifs pédagogiques

- Câbler une LED RGB et **limiter le courant** avec une résistance (loi d'Ohm, loi des mailles).
- Comprendre le modèle **client / serveur** et la structure d'une requête et d'une réponse HTTP.
- Lire une **URL** : protocole, hôte, port, chemin, paramètres.
- Interpréter les **codes de statut** (200, 400, 404) et une réponse **JSON**.
- Découvrir les principes d'une **API REST** et évaluer si un serveur les respecte.
- Envoyer des requêtes HTTP depuis **Node-RED** (`http request`) et publier une page web (`http in` / `http response`).

---

## Matériel

- 1 × M5Stack **Tab5** (ESP32-P4 + ESP32-C6 pour le WiFi) et un câble USB-C
- 1 × LED RGB 5 mm à **cathode commune** (BTS : prévoir aussi des LED à **anode commune**)
- 3 × résistances 220 Ω (BTS : série E12 de 100 à 330 Ω)
- Plaque d'essai, fils Dupont, multimètre
- Un point d'accès WiFi : le Tab5 et le PC doivent être sur **le même sous-réseau**

### Câblage (Port 3 du Tab5)

| Broche du Tab5 | Résistance | Patte de la LED |
|---|---|---|
| GPIO 49 | 220 Ω | Rouge |
| GPIO 50 | 220 Ω | Vert |
| GPIO 1  | 220 Ω | Bleu |
| GND | — | Cathode commune (patte la plus longue) |

> **Cathode commune** : une sortie à `HIGH` (3,3 V) **allume** la couleur.
> **Anode commune** : la patte commune va au **+3,3 V** (jamais au 5 V) et la logique est inversée (`LOW` = allumé). Il faut alors adapter la fonction `applyLeds()` du programme.

Ordre de grandeur avec 220 Ω sous 3,3 V : environ 6 mA pour le rouge (Vf ≈ 2 V), mais seulement 1 à 2 mA pour le vert et le bleu (Vf ≈ 3 V). Ces deux couleurs paraîtront donc moins lumineuses ; une résistance de 100 Ω sur ces deux voies permet de rééquilibrer. Les tensions de seuil sont des valeurs typiques, à vérifier dans la documentation de vos LED.

---

## Mise en route

### 1. Programme du Tab5

1. Dans l'Arduino IDE, installez le paquet de cartes **M5Stack** (ESP32 3.x) et la bibliothèque **M5Unified** (avec M5GFX).
2. Ouvrez `TP_HTTP_LED_RGB_Tab5/TP_HTTP_LED_RGB_Tab5.ino`.
3. Renseignez votre réseau en haut du fichier :
   ```cpp
   const char* WIFI_SSID     = "SSID_DE_LA_SALLE";
   const char* WIFI_PASSWORD = "MOT_DE_PASSE";
   ```
4. Sélectionnez la carte **M5Tab5**, téléversez, puis ouvrez le moniteur série à 115 200 bauds.
5. L'adresse IP du Tab5 s'affiche à l'écran et dans le moniteur série.

> Sur le Tab5, le WiFi passe par le coprocesseur ESP32-C6 : l'appel `WiFi.setPins(...)` présent dans le programme est indispensable.

### 2. Site web embarqué

Ouvrez `http://IP_du_Tab5/` dans un navigateur (PC ou smartphone). La page affiche une LED virtuelle, des boutons de commande et le détail de chaque requête envoyée (URL, code, temps de réponse, JSON reçu).

### 3. Flow Node-RED

1. Menu ☰ → **Importer** → collez le contenu de `nodered/flow_nodered_TP_LED_RGB.json` → **Importer**.
2. Double-cliquez sur le nœud **« IP du Tab5 = 192.168.1.50 »** et remplacez l'adresse par celle de votre Tab5.
3. Cliquez sur **Déployer**.

Le flow est organisé en quatre parties :

| Partie | Rôle |
|---|---|
| ① Configuration | Stocke l'adresse IP du Tab5 dans la variable de flow `tab5_ip` |
| ② Client HTTP | 10 boutons `inject` : couleurs, extinction, lecture de l'état, test 404 |
| ③ Surveillance | Lecture de `/api/state` toutes les 2 s, état affiché sous le nœud |
| ④ Serveur HTTP | Page `http://IP_du_PC:1880/led-rgb` qui relaie les commandes vers le Tab5 |

Aucun nœud supplémentaire n'est nécessaire : le flow n'utilise que les nœuds de base de Node-RED.

---

## API HTTP du Tab5

Toutes les routes utilisent la méthode **GET** et répondent en JSON (sauf `/`).

| Requête | Effet | Réponse |
|---|---|---|
| `GET /` | Site web embarqué | Page HTML |
| `GET /api/state` | Lit l'état de la LED | `200` + JSON |
| `GET /api/led?color=red&state=on` | `color` = `red` \| `green` \| `blue`, `state` = `on` \| `off` \| `toggle` | `200`, ou `400` si paramètre manquant ou invalide |
| `GET /api/rgb?r=1&g=0&b=1` | Règle les trois couleurs d'un coup | `200` + JSON |
| `GET /api/off` | Éteint tout | `200` + JSON |
| toute autre route | — | `404` + JSON |

Exemple de réponse :

```json
{"red":1,"green":0,"blue":1,"requests":12}
```

Test rapide depuis un terminal :

```bash
curl "http://192.168.1.50/api/led?color=red&state=on"
curl "http://192.168.1.50/api/state"
```

---

## Ce serveur est-il RESTful ?

C'est l'une des questions centrales du TP. Le serveur du Tab5 est une **API HTTP** qui reprend plusieurs principes REST :

- séparation **client / serveur** ;
- fonctionnement **sans état** (chaque requête est autonome) ;
- échanges en **JSON** ;
- **codes de statut** significatifs (200, 400, 404).

Il n'est cependant **pas strictement RESTful** :

- la méthode **GET modifie l'état** de la LED, alors qu'elle devrait être *sûre* (sans effet sur le serveur) ;
- certaines URL décrivent des **actions** (`/api/off`, `state=toggle`) au lieu de désigner des **ressources** ;
- `toggle` n'est **pas idempotent** : deux appels ne donnent pas le même résultat qu'un seul.

Ce choix est volontaire : avec GET, tout se teste directement depuis la barre d'adresse d'un navigateur. Une version RESTful ressemblerait à :

| Action | Requête RESTful |
|---|---|
| Lire l'état | `GET /api/led` |
| Régler les 3 couleurs | `PUT /api/led` avec `{"red":1,"green":0,"blue":1}` |
| Lire une couleur | `GET /api/led/red` |
| Modifier une couleur | `PUT /api/led/red` avec `{"on":false}` |

Les étudiants de BTS ajoutent la route `PUT /api/led` dans la partie 4.3 de leur guide.

---

## Déroulé proposé

| Étape | STI2D SIN | BTS CIEL ER 1 |
|---|---|---|
| 1. LED et limite de courant | Loi d'Ohm, tableau des courants | Expression littérale de R, série E12, puissance, documentation de l'ESP32-P4, CC/AC |
| 2. Mise en service du Tab5 | WiFi, adresse IP | + rôle de `WiFi.setPins()` |
| 3. Protocole HTTP | Outils de développement (F12), URL, codes de statut | Identique |
| 4. API REST | Analyse du serveur, tableau méthode / URI | + implémentation de `PUT /api/led` |
| 5. Node-RED | Import du flow, `msg.url`, `msg.statusCode` | Identique + requête PUT |
| 6. Évolution | Bouton « cyan », bouton « blanc » | Route `/api/blink`, passage en PWM |

---

## Pistes pour aller plus loin

- **PWM** : remplacer `digitalWrite()` par `analogWrite()` pour obtenir des nuances de couleur.
- **POST / PUT + JSON** : faire passer les données dans le corps de la requête plutôt que dans l'URL (bibliothèque ArduinoJson).
- **Sécurité** : HTTP circule en clair et sans authentification ; discuter du HTTPS, des jetons d'accès et du cloisonnement réseau.
- **HTTP ou MQTT** : comparer l'interrogation périodique (polling) au modèle publication / abonnement.

---

## Auteur

Kiiway — enseignant en systèmes embarqués, IoT et cybersécurité.
