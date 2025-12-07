# CSFS - Custom Simple File System

Un système de fichiers personnalisé implémenté en C avec support des répertoires hiérarchiques et un shell interactif complet.

## 📋 Présentation

CSFS est un système de fichiers conteneurisé qui stocke des fichiers et répertoires dans un seul fichier image (`.img`). Il permet de créer, gérer et naviguer dans une arborescence complète via une interface en ligne de commande ou un shell interactif de type Unix.

### Caractéristiques principales

- **Architecture modulaire** : Code organisé en modules séparés (filesystem, shell, main)
- **Système hiérarchique** : Support complet des répertoires et sous-répertoires
- **Shell interactif** : REPL avec commandes familières (cd, ls, mkdir, cat, etc.)
- **CLI ergonomique** : Commandes simples pour opérations rapides
- **Ajout intelligent** : Détection automatique du basename et support des chemins avec `/`
- **Wildcards** : Support des motifs `*`/`?` pour add/extract (style shell)
- **Métadonnées** : Timestamps de création/modification pour chaque entrée
- **Format binaire** : Superblock + table d'inodes + zone de données

## 🚀 Installation

### Prérequis

- Compilateur C (gcc/clang)
- Make
- macOS ou Linux

### Compilation

```bash
git clone <votre-repo>
cd csfs
make
```

L'exécutable `csfs` sera créé à la racine du projet.

## 📖 Utilisation

### Mode ligne de commande

#### Créer un système de fichiers
```bash
./csfs myfs.img create
```

#### Créer des répertoires
```bash
./csfs myfs.img mkdir /documents
./csfs myfs.img mkdir /documents/projets
```

#### Ajouter des fichiers

**Syntaxe simplifiée** :
```bash
# Ajout à la racine (détection automatique du nom)
./csfs myfs.img add fichier.txt

# Ajout dans un répertoire (basename automatique)
./csfs myfs.img add rapport.pdf /documents/

# Ajout avec nom personnalisé
./csfs myfs.img add local.txt /documents/remote.txt
```

#### Lister le contenu
```bash
./csfs myfs.img list /
./csfs myfs.img list /documents
```

#### Extraire un fichier
```bash
# Extraction simple (vers le répertoire courant)
./csfs myfs.img extract /documents/rapport.pdf

# Extraction vers un répertoire spécifique (basename automatique)
./csfs myfs.img extract /documents/rapport.pdf /tmp/

# Extraction avec renommage personnalisé
./csfs myfs.img extract /documents/rapport.pdf ./mon_rapport.pdf
```

### Mode shell interactif

Lancez le shell :
```bash
./csfs myfs.img
# ou explicitement
./csfs myfs.img shell
```

#### Commandes disponibles

| Commande | Description | Exemples |
|----------|-------------|----------|
| `help` | Affiche l'aide | `help` |
| `pwd` | Répertoire courant | `pwd` |
| `ls [chemin]` | Liste le contenu | `ls`, `ls /docs` |
| `tree [options] [chemin]` | Affichage arborescent | `tree`, `tree -a`, `tree -d -L 2` |
| `find [chemin] [motif]` | Recherche par nom | `find log`, `find /docs report` |
| `cd <chemin>` | Change de répertoire | `cd /docs`, `cd ..`, `cd /` |
| `mkdir <chemin>` | Crée un répertoire | `mkdir projets` |
| `add <fichier> [dest]` | Ajoute un fichier (wildcards supportés) | `add *.txt /docs/` |
| `cat <chemin>` | Affiche un fichier | `cat /docs/readme.txt` |
| `stat <chemin>` | Métadonnées détaillées | `stat /docs/readme.txt` |
| `extract <src> [dest]` | Extrait fichier(s) (wildcards supportés) | `extract /docs/*.txt /tmp/` |
| `cp <src> <dest>` | Copie dans le FS | `cp /file.txt /backup/file.txt` |
| `mv <src> <dest>` | Déplace/renomme | `mv /old.txt /new.txt`, `mv /file.txt /docs/` |
| `rm <chemin>` | Supprime (répertoire vide ou fichier) | `rm old.txt` |
| `exit` | Quitte le shell | `exit` |

**Options de `tree`** :
- `-a` : Afficher les métadonnées (taille, date de modification)
- `-d` : Répertoires uniquement (masquer les fichiers)
- `-L <n>` : Profondeur maximale (ex: `tree -L 2` pour 2 niveaux)

#### Exemple de session

```bash
$ ./csfs demo.img
=== CSFS Shell v1.0 ===
Tapez 'help' pour la liste des commandes

fssh:/> mkdir /projects
Répertoire créé : /projects

fssh:/> cd projects
fssh:/projects> add ../mycode.c
Fichier ajouté : /projects/mycode.c (2048 octets)

fssh:/projects> ls
=== Contenu du système de fichiers ===
Répertoire : /projects

Nom                                            Taille                 Date
---------------------------------------------------------------------
mycode.c                                     2048 B      2025-12-07 10:30

fssh:/projects> cat mycode.c
[contenu du fichier]

fssh:/projects> cd ..
fssh:/> tree -a
/
├── projects/ [2025-12-07 10:30]
│   └── mycode.c (2048 B) [2025-12-07 10:30]

1 directories, 1 files
fssh:/> exit
Au revoir!
```

## 🏗️ Architecture technique

### Structure du projet

```
csfs/
├── include/
│   ├── fs.h          # API du système de fichiers
│   └── shell.h       # API du shell interactif
├── src/
│   ├── fs.c          # Implémentation du FS (create, open, add, extract, list)
│   ├── shell.c       # REPL et commandes interactives
│   └── main.c        # Point d'entrée et CLI
├── Makefile          # Build configuration
└── README.md
```

### Format du conteneur

```
┌─────────────────┐
│   SuperBlock    │  ← Magic, version, métadonnées globales
├─────────────────┤
│  Table Inodes   │  ← 1024 entrées max (filename, parent_path, size, offset, timestamps)
├─────────────────┤
│   Zone Données  │  ← Contenu binaire des fichiers
└─────────────────┘
```

### Limitations actuelles

- **1024 fichiers/répertoires** maximum (configurable via `MAX_FILES`)
- **Pas de fragmentation** : les données sont stockées séquentiellement
- **Pas de permissions** : pas de gestion d'utilisateurs/groupes
- **Suppression simple** : l'espace n'est pas récupéré (marquage comme libre uniquement)

## 🔮 Possibilités futures

### Fonctionnalités planifiées

#### Court terme
- [x] **Commande tree** : affichage arborescent avec options `-a`, `-d`, `-L`
- [x] **Commandes shell additionnelles** (partiellement)
  - [x] `cp` : copie de fichiers dans le FS
  - [x] `mv` : déplacement/renommage de fichiers
  - [x] `find` : recherche par nom/motif
  - [x] `stat` : métadonnées détaillées d'une entrée

- [ ] **Amélioration de l'ajout de fichiers**
  - Support de wildcards (`add *.txt /docs/`)
  - Import récursif de répertoires (`add -r ./monprojet /backup/`)
  - Barre de progression pour fichiers volumineux

- [ ] **Compression et optimisation**
  - Compression transparente (zlib/lz4) des données
  - Défragmentation du conteneur
  - Récupération de l'espace des fichiers supprimés

#### Moyen terme
- [ ] **Gestion avancée**
  - Permissions Unix-like (rwxr-xr-x)
  - Propriétaires et groupes
  - Liens symboliques et hard links
  - Attributs étendus (extended attributes)

- [ ] **Performance et scalabilité**
  - Index B-Tree pour recherche rapide
  - Cache des inodes en mémoire
  - Support de conteneurs > 4GB (offsets 64-bit)
  - Fragmentation intelligente pour optimiser l'espace

- [ ] **Intégrité et fiabilité**
  - Checksum MD5/SHA256 par fichier
  - Journal (journaling) pour transactions atomiques
  - Mode lecture seule
  - Snapshots et versioning

#### Long terme
- [ ] **Fonctionnalités avancées**
  - Chiffrement AES des données
  - Déduplication par hash
  - Montage FUSE (système de fichiers virtuel sous Linux/macOS)
  - Interface réseau (serveur NFS-like)
  - API REST pour accès distant

- [ ] **Outils complémentaires**
  - GUI avec Qt/GTK pour navigation visuelle
  - `fsck.csfs` : vérification et réparation
  - Conversion depuis/vers tar, zip, squashfs
  - Plugin pour gestionnaires de fichiers (Nautilus, Finder)

- [ ] **Extensions du format**
  - Métadonnées EXIF pour images
  - Support de streams (audio/vidéo)
  - Chunks pour fichiers > RAM
  - Multi-conteneurs avec liens inter-images

### Cas d'usage potentiels

- **Archivage portable** : alternative à tar/zip avec navigation
- **Packaging d'applications** : conteneur autonome pour distribuer des apps
- **Systèmes embarqués** : FS minimal pour IoT/microcontrôleurs
- **Éducation** : apprentissage des concepts de systèmes de fichiers
- **Backup incrémental** : versioning avec snapshots
- **Distribution de données** : datasets scientifiques avec métadonnées

## 🛠️ Développement

### Build & Clean

```bash
make          # Compilation
make clean    # Nettoyage des objets et binaire
```

### Tests

```bash
# Test CLI
./csfs test.img create
./csfs test.img mkdir /test
./csfs test.img add README.md /test/
./csfs test.img list /test

# Test shell (avec script)
echo -e "mkdir /demo\ncd demo\nls\nexit" | ./csfs test.img
```

### Contribution

Les contributions sont les bienvenues ! Pour contribuer :

1. Forkez le projet
2. Créez une branche (`git checkout -b feature/amazing-feature`)
3. Committez vos changements (`git commit -m 'Add amazing feature'`)
4. Pushez vers la branche (`git push origin feature/amazing-feature`)
5. Ouvrez une Pull Request

**Idées de contributions** :
- Implémentation des commandes `cp`/`mv`
- Ajout de tests unitaires
- Support de la compression
- Documentation des structures de données
- Portage Windows

## 📄 Licence

Ce projet est sous licence MIT. Voir `LICENSE` pour plus de détails.

## 🙏 Remerciements

Inspiré par les systèmes de fichiers classiques (ext2/3, FAT, minix) et les outils modernes de conteneurisation.

---

**Version** : 1.0.0  
**Auteur** : [Votre nom]  
**Contact** : [Votre email]
