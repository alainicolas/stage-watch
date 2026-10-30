## Télécharger

| Ton ordinateur | Fichier |
|---|---|
| Mac récent (puce **Apple M1, M2, M3, M4…**) | **StageWatch-Mac-Apple-Silicon.zip** |
| Mac plus ancien (processeur **Intel**) | StageWatch-Mac-Intel.zip |
| **Windows** 10 / 11 | StageWatch-Windows.zip |

Pas sûr pour le Mac ? Menu  › *À propos de ce Mac* : si tu lis « Puce Apple », prends Apple Silicon. Si tu lis « Processeur Intel », prends Intel.

Rien à installer, pas besoin d'Internet.

## Lancer

### Mac
1. Double-clic sur le `.zip`, puis glisse **StageWatch** dans *Applications* (ou laisse-le où tu veux).
2. **La première fois seulement** : macOS bloque les apps téléchargées.
   - Double-clic sur StageWatch. Une alerte dit que l'app ne peut pas être vérifiée : clique sur **Terminé** (ou OK).
   - Ouvre **Réglages Système › Confidentialité et sécurité**, descends en bas, et clique sur **Ouvrir quand même** à côté de « StageWatch ».
   - Confirme. Les fois suivantes, un simple double-clic suffit.
3. Une fenêtre Terminal s'ouvre (c'est le serveur, **laisse-la ouverte**), puis le navigateur s'ouvre sur l'interface du dirigeant.
4. Si le Mac demande d'autoriser les connexions réseau, clique sur **Autoriser** (sinon les montres ne pourront pas se connecter).

Pour arrêter : ferme la fenêtre Terminal.

### Windows
1. Clic droit sur le `.zip` › **Extraire tout**, puis double-clic sur **StageWatch.exe**.
2. **La première fois seulement** : Windows affiche « Windows a protégé votre ordinateur ». Clique sur **Informations complémentaires**, puis **Exécuter quand même**.
3. Le pare-feu demande l'accès au réseau : coche **Réseaux privés** et clique sur **Autoriser**.
4. Une fenêtre noire s'ouvre (c'est le serveur, **laisse-la ouverte**), puis le navigateur s'ouvre.

Pour arrêter : ferme la fenêtre noire.

## Utiliser

| Page | À quoi ça sert |
|---|---|
| **Dirigeant** (s'ouvre tout seul) | Choisir un ou plusieurs participants, taper le message (96 caractères max), Entrée pour envoyer. ✎ sur une case pour changer le nom ou la photo. |
| **Public** (lien en haut de la page Dirigeant) | À mettre sur l'écran ou le projecteur de la salle. **Double-clic = plein écran.** |
| **Simulateur** (lien en haut de la page Dirigeant) | Des montres virtuelles, **pour faire une démo sans les vraies montres**. |

### Démo rapide sans montres
1. Lance StageWatch.
2. Ouvre **Simulateur** dans un onglet (choisis le nombre de montres en haut), et **Public** dans un autre.
3. Depuis **Dirigeant**, envoie un message et regarde-le arriver sur l'écran public puis sur la montre simulée.

### Options (sous la zone de message)
- **Animation public** : le message apparaît en grand avec les photos des destinataires, puis vole vers leurs cases.
- **Révélation public** : la montre reçoit le message seulement quand il atterrit sur l'écran public.

Les noms, les photos et l'historique sont enregistrés automatiquement : dans `Documents/StageWatch` sur Mac, et à côté de `StageWatch.exe` sur Windows.

## Avec les vraies montres
Le laptop et les montres doivent être sur **le même Wi-Fi en 2,4 GHz**. Les montres trouvent le laptop toutes seules. Préparation des montres : voir [`firmware/README.md`](https://github.com/alainicolas/stage-watch/blob/main/firmware/README.md).
