# StageWatch

Envoyer des messages courts à des participants sur scène (montres LilyGO T-Watch S3) et les afficher au public.

## Télécharger (sans rien installer)
👉 **[Dernière version : Mac et Windows](https://github.com/alainicolas/stage-watch/releases/latest)**. Le mode d'emploi pas à pas est sur la page de téléchargement.

## Lancer
Double-clic sur `StageWatch-mac-arm64` (Mac M1/M2/M3…), `StageWatch-mac-intel` ou `StageWatch.exe` (Windows).
Le navigateur s'ouvre sur l'interface du dirigeant.

Le port est **47800** (ou le suivant libre, jusqu’à 47809, s’il est déjà pris : l’adresse exacte s’affiche au lancement). Forcer un port : `-port 9000`.

| Page | Rôle |
|---|---|
| `http://localhost:47800/admin` | Dirigeant : choisir les destinataires, envoyer, voir l'état des montres |
| `http://localhost:47800/public` | Écran de la salle (double-clic = plein écran) |
| `http://localhost:47800/sim` | Simulateur de montres, pour tester sans matériel |

Premier lancement : macOS → clic droit › Ouvrir (app non signée). Windows → « Informations complémentaires › Exécuter quand même ». Autoriser l'accès réseau quand le pare-feu le demande.

Jusqu’à 8 montres (IDs 1 à 8). Les noms et photos (bouton ✎ dans l’admin) sont enregistrés dans `stagewatch-config.json` et `photos/` et l'historique dans `logs/`, à côté de l'exécutable.

## Protocole (JSON sur WebSocket `/ws?role=watch|admin|public`)
- montre → serveur : `{type:"hello",id,battery}` · `{type:"hb",battery}` · `{type:"ack",msgId}`
- serveur → montre : `{type:"msg",msgId,text}` (96 caractères max, affichés sur une grille fixe de 12 × 8) · `{type:"clear"}`
- admin → serveur : `{type:"send",targets:[1,3]|"all",text}` · `{type:"clear",targets}` · `{type:"rename",id,name}` ; photo : `POST|DELETE /api/photo?id=N`
- serveur → admin/public : `state`, `message`, `ack`, `cleared`

Un message non confirmé est renvoyé automatiquement quand la montre se reconnecte.

## Animation de la page public (options dans l'admin, sous le message)
- **Animation public** (activée par défaut) : à chaque envoi, le message apparaît en grand au centre avec les photos des destinataires (durée selon la longueur : 2,5 s + 0,45 s par mot, de 3,5 à 12 s), puis vole vers leurs cases et s’y pose. Chaque message a sa couleur (palette de 8 qui tourne). La case est allumée à cette couleur pendant 20 s, puis en garde un contour atténué jusqu’à 1 min, puis repasse au gris (ou dès « Effacer l’écran »). Les envois rapprochés passent en file, et la révélation est raccourcie quand il y a du retard.
- **Révélation public** (désactivée par défaut) : la montre reçoit le message seulement quand il atterrit sur l'écran public. Dans l'admin, la case affiche « ⏳ révélation » en attendant. « Effacer l'écran » annule une révélation en cours.
- Le serveur planifie la file et envoie à la page public `{type:"send",batch:{id,to,text,delay,dur,reveal}}`. Réglages côté admin : `{type:"settings",animate,reveal}` (enregistrés dans `stagewatch-config.json`).

## Firmware des montres
Voir `firmware/README.md`.

## Développer
`go run .` · build : `./build.sh` (Go ≥ 1.22).
Nouvelle release : `./release/make-release.sh 0.2.0` (mode d'emploi publié : `release/NOTES.md`).
