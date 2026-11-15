# Constructeur PC autonome : WWHD Switch 2 0.0.1

La version actuelle du programme PC est en anglais (interface, étapes de
progression, messages et instructions de copie). Le ZIP anglais porte le suffixe
`-EN.zip`. La langue du jeu et sa
version 0.0.1 ne sont pas modifiées.

## Utilisation (Windows 10/11 x64)

1. Extraire **tout** le ZIP du constructeur dans un dossier du PC.
2. Ouvrir `WWHD-Switch2-Builder.exe` (pas directement depuis le ZIP).
3. Choisir son dump Loadiine personnel, non modifié, USA/EUR version 0.
   Le dossier choisi doit contenir `code/cking.rpx`, `content/` et `meta/meta.xml`.
4. Choisir un dossier de sortie disposant de l'espace nécessaire : taille du
   dump plus au moins 2 Gio pour la construction temporaire.
5. Cliquer sur **Build NRO and romfs folder**.

**Aucun CMake, Python, Visual Studio, MSYS ou devkitPro à installer côté
utilisateur.** L'interpréteur, l'interface graphique, Clang/LLD 19.1.5, les
en-têtes, les bibliothèques du runtime et le convertisseur Windows sont inclus.
Il n'y a aucun téléchargement pendant la construction et aucun droit
administrateur n'est demandé. Les DLL restantes sont celles de Windows ; le
runtime Visual C++ utilisé par Python est fourni avec l'application.

La progression indique le pourcentage de l'étape, pas une durée restante.
Le panneau inférieur affiche un journal horodaté en direct : vérification des
ressources, analyses PowerPC, fichiers C en cours de compilation, liaison du
NRO et fichiers du dump copiés. L'historique reste visible en cas d'erreur ; une
barre de défilement permet de le consulter. Le bandeau promotionnel et le texte
fixe de construction ont été retirés de la fenêtre.
Les analyses PowerPC et la compilation peuvent prendre plusieurs minutes.
Annuler conserve les diagnostics ; le dump original n'est pas supprimé ni modifié.

## Résultat et copie sur microSD

Chaque construction réussie crée un nouveau dossier `WWHD-Switch2-0.0.1-*` :

```text
wwhd_switch2.nro
romfs/
    code/cking.rpx
    content/
    meta/meta.xml
COPY_TO_MICROSD.txt
build-info.json
build.log
recompiler.log
```

L'application affiche les instructions à la fin et propose d'ouvrir le résultat :

- Copier **`wwhd_switch2.nro` dans `/switch/`**.
- Copier **le contenu du dossier `romfs` dans `/switch/wwhd-switch2/game/`**.
  Il faut obtenir `game/code`, `game/content`, `game/meta`, **pas** `game/romfs`.
- Ne pas écraser les sauvegardes existantes. Démarrer via hbmenu en mode
  application, avec la mémoire complète.

Le « romfs » de sortie désigne les ressources externes du dump à copier sur la
SD, pas une image RomFS intégrée au NRO. Le runtime existant charge le contenu
depuis les chemins ci-dessus. Le jeu n'est plus recompilé sur la console.

L'homebrew conserve **0.0.1** et le logo demandé. Son crédit hbmenu et À propos
est **SwitchWakerHD / Zoria** ; le lien du dépôt original
**https://github.com/centollOS/SwitchWakerHD** reste présent dans À propos.
Le même logo sert d'icône au fichier EXE, à sa fenêtre et à sa barre des tâches.
Le NRO généré est privé et contient du code dérivé du dump de son propriétaire.
Ne pas le redistribuer ni inclure le dump dans le ZIP du constructeur.

### Réglage FPS par défaut

Les nouvelles installations ciblent **30 FPS natifs**, avec interpolation et
True 60 désactivés. `interpFps=60` est uniquement la fréquence mémorisée pour
une éventuelle activation ultérieure de l'interpolation ; le mode actif
`fps60=0` reste bien à 30 FPS. Aucune fréquence CPU/GPU n'est modifiée.
Les choix déjà sauvegardés sur la microSD restent prioritaires : pour repasser
une installation existante à 30 FPS, utiliser le menu **L + R + Minus**.

## Architecture et vérifications

Le constructeur réutilise le frontend RPX → C original, puis appelle ses
compilateurs Windows embarqués par chemin absolu, sans shell ni outils trouvés
via `PATH`. Le runtime original est précompilé sans jeu ; seules les fonctions
issues du dump de l'utilisateur sont compilées localement, puis liées à ce
runtime. La conversion ELF → NRO et le contrôle des imports/relocations utilisent
le même validateur que l'installateur expérimental, compilé en exécutable Windows
natif avec runtime statique. Aucun binaire de jeu ni corps placeholder n'entre
dans le paquet du constructeur.

La construction s'effectue dans une transaction privée. Le résultat n'est
annoncé prêt qu'après compilation, liaison, validation du NRO, copie du dump et
vérification de son RPX. Les sorties précédentes et les sauvegardes restent
intactes. En cas d'erreur, `.wwhd-build-*` contient les diagnostics.

L'autotest de paquet est lancé avec `PATH=C:\Windows\System32`, sans accès aux
outils du développeur. Le convertisseur natif reproduit l'ancien NRO jouable
octet pour octet, y compris avec un nom de fichier Unicode. Le démarrage de
l'interface graphique autonome a également été vérifié avec ce `PATH` limité.
Une **construction complète avec le dump personnel depuis l'EXE autonome a
réussi**, sans variables d'environnement Python/devkitPro/CMake et avec un
dossier de sortie Unicode. Le NRO passe le validateur natif ; les métadonnées,
le logo et les ressources copiées font l'objet d'un contrôle séparé.
Cela ne constitue pas encore une validation sur console de ce nouveau NRO.
Les 51 tests Python passent (deux tests Pillow optionnels ignorés dans
l'environnement Python principal).

L'audit du runtime distingue deux fallbacks faibles **originaux** d'amont
(`language_region.cpp`, `model_probe.cpp`) dont le nom ressemble à une fonction
de jeu. Ils sont explicitement autorisés par origine ; tout autre symbole
défini de fonction de jeu fait échouer l'emballage. Les vraies traductions du
dump remplacent ces fallbacks à la liaison.

## Préparation côté développeur seulement

Les dépendances ci-dessous servent à fabriquer le ZIP ; elles ne sont pas
nécessaires pour son utilisateur :

```powershell
python -m venv build/pc-builder-venv
build/pc-builder-venv/Scripts/python.exe -m pip install pyinstaller==6.16.0 pillow==12.0.0
python tools/switch2/build_runtime_sdk.py --jobs 2
python tools/switch2/package_link_sdk.py
build/pc-builder-venv/Scripts/python.exe tools/switch2/package_pc_builder.py
```

`build/pc-builder-release/current.json` donne le chemin du ZIP et son SHA-256.
Le script produit `WWHD-Switch2-PC-Builder-0.0.1-Windows-x64-EN.zip`
dans le répertoire de paquet indiqué par `current.json` ; la copie de livraison
peut être placée directement dans `build/`.
Les en-têtes et bibliothèques proviennent du SDK local validé. Les sources du
constructeur et du convertisseur, les licences et les références amont sont
incluses. Une publication publique nécessite encore la revue des obligations de
redistribution/source de toutes les dépendances et des droits sur le logo
Nintendo : son téléchargement/redimensionnement ne confère aucun droit.
