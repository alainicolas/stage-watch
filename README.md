# StageWatch

Envoyer des messages courts à des participants sur scène (montres LilyGO T-Watch S3) et les afficher au public.

## Lancer
Double-clic sur `StageWatch-mac-arm64` (Mac M1/M2/M3…), `StageWatch-mac-intel` ou `StageWatch.exe` (Windows).
Le navigateur s'ouvre sur l'interface du dirigeant.

| Page | Rôle |
|---|---|
| `http://localhost:8080/admin` | Dirigeant : choisir les destinataires, envoyer, voir l'état des montres |
| `http://localhost:8080/public` | Écran de la salle (double-clic = plein écran) |
| `http://localhost:8080/sim` | Simulateur de montres, pour tester sans matériel |

Premier lancement : macOS → clic droit › Ouvrir (app non signée). Windows → « Informations complémentaires › Exécuter quand même ». Autoriser l'accès réseau quand le pare-feu le demande.

Les noms des participants sont enregistrés dans `stagewatch-config.json` et l'historique dans `logs/`, à côté de l'exécutable.

## Protocole (JSON sur WebSocket `/ws?role=watch|admin|public`)
- montre → serveur : `{type:"hello",id,battery}` · `{type:"hb",battery}` · `{type:"ack",msgId}`
- serveur → montre : `{type:"msg",msgId,text}` · `{type:"clear"}`
- admin → serveur : `{type:"send",targets:[1,3]|"all",text}` · `{type:"clear",targets}` · `{type:"rename",id,name}`
- serveur → admin/public : `state`, `message`, `ack`, `cleared`

Un message non confirmé est renvoyé automatiquement quand la montre se reconnecte.

## Développer
`go run .` · build : `./build.sh` (Go ≥ 1.22).
