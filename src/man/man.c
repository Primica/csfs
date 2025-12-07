#include "man.h"
#include <stdio.h>
#include <string.h>

// Man page definitions
static const ManPage man_pages[] = {
    {
        .name = "cd",
        .synopsis = "cd <chemin>",
        .description = 
            "Change le répertoire courant du shell.\n"
            "\n"
            "Le chemin peut être absolu (commençant par /) ou relatif.\n"
            "Les chemins spéciaux '.' (répertoire courant) et '..' (répertoire parent)\n"
            "sont supportés.",
        .options = NULL,
        .examples =
            "cd /docs                 Aller dans /docs\n"
            "cd projets               Aller dans projets/ (relatif)\n"
            "cd ..                    Remonter au parent\n"
            "cd /                     Retour à la racine",
        .see_also = "pwd, ls, tree"
    },
    {
        .name = "ls",
        .synopsis = "ls [chemin]",
        .description =
            "Liste le contenu d'un répertoire.\n"
            "\n"
            "Sans argument, liste le répertoire courant. Avec un chemin,\n"
            "liste le contenu du répertoire spécifié. Affiche le nom,\n"
            "la taille et la date de modification de chaque entrée.\n"
            "Supporte les wildcards '*' et '?' (ex: ls *.txt).",
        .options = NULL,
        .examples =
            "ls                       Liste le répertoire courant\n"
            "ls /docs                 Liste /docs\n"
            "ls projets               Liste projets/ (relatif)",
        .see_also = "tree, cd, pwd"
    },
    {
        .name = "tree",
        .synopsis = "tree [options] [chemin]",
        .description =
            "Affiche une arborescence du système de fichiers.\n"
            "\n"
            "Présente une vue hiérarchique complète des répertoires et fichiers\n"
            "avec des caractères graphiques pour représenter la structure.",
        .options =
            "-a              Afficher les métadonnées (taille, date)\n"
            "-d              Répertoires uniquement (masquer les fichiers)\n"
            "-L <niveau>     Limiter la profondeur d'affichage",
        .examples =
            "tree                     Arbre complet depuis la racine\n"
            "tree -a                  Avec métadonnées détaillées\n"
            "tree -d                  Seulement les répertoires\n"
            "tree -L 2                Maximum 2 niveaux de profondeur\n"
            "tree /docs               Arbre du répertoire /docs\n"
            "tree -a -d -L 1          Combinaison d'options",
        .see_also = "ls, cd, find"
    },
    {
        .name = "find",
        .synopsis = "find [chemin] [motif]",
        .description =
            "Recherche récursivement des fichiers ou répertoires à partir d'un chemin donné.\n"
            "\n"
            "Si aucun chemin n'est fourni, la recherche démarre depuis le répertoire courant.\n"
            "Le motif est une sous-chaîne du nom de fichier (recherche simple, sensible à la casse).\n"
            "Lorsque le motif est omis, tous les éléments rencontrés sont listés.",
        .options = NULL,
        .examples =
            "find                     Liste tout depuis le répertoire courant\n"
            "find /docs               Liste tout sous /docs\n"
            "find notes               Recherche tous les noms contenant 'notes' depuis le cwd\n"
            "find /projects log       Recherche 'log' sous /projects",
        .see_also = "ls, tree, stat"
    },
    {
        .name = "pwd",
        .synopsis = "pwd",
        .description =
            "Affiche le répertoire de travail courant (Print Working Directory).\n"
            "\n"
            "Cette commande affiche le chemin absolu du répertoire dans lequel\n"
            "vous vous trouvez actuellement.",
        .options = NULL,
        .examples =
            "pwd                      Affiche le chemin courant",
        .see_also = "cd, ls"
    },
    {
        .name = "mkdir",
        .synopsis = "mkdir <chemin>",
        .description =
            "Crée un nouveau répertoire.\n"
            "\n"
            "Le répertoire parent doit exister. Le chemin peut être absolu\n"
            "ou relatif au répertoire courant.",
        .options = NULL,
        .examples =
            "mkdir projets            Créer projets/ dans le répertoire courant\n"
            "mkdir /docs/archives     Créer /docs/archives (parent doit exister)\n"
            "mkdir ./temp             Créer temp/ (chemin relatif explicite)",
        .see_also = "rm, cd, ls"
    },
    {
        .name = "add",
        .synopsis = "add <fichier_source> [destination]",
        .description =
            "Ajoute un fichier externe dans le système de fichiers.\n"
            "\n"
            "Sans destination, le fichier est ajouté dans le répertoire courant\n"
            "avec son nom d'origine (basename). Si la destination se termine par '/',\n"
            "le basename est automatiquement ajouté. Les wildcards '*' et '?' sont\n"
            "supportés sur le chemin source (ex: add *.txt /docs/).",
        .options = NULL,
        .examples =
            "add file.txt             Ajoute file.txt dans le répertoire courant\n"
            "add /tmp/data.csv        Ajoute data.csv depuis /tmp (système hôte)\n"
            "add doc.pdf /docs/       Ajoute comme /docs/doc.pdf\n"
            "add a.txt /docs/b.txt    Ajoute a.txt en le renommant b.txt",
        .see_also = "extract, cat, rm"
    },
    {
        .name = "cat",
        .synopsis = "cat <chemin>",
        .description =
            "Affiche le contenu d'un fichier sur la sortie standard.\n"
            "\n"
            "Lit et affiche l'intégralité du contenu du fichier spécifié.\n"
            "Utile pour visualiser des fichiers texte. Supporte les wildcards\n"
            "'*' et '?' sur le chemin (ex: cat /docs/*.txt).",
        .options = NULL,
        .examples =
            "cat README.md            Affiche le contenu de README.md\n"
            "cat /docs/notes.txt      Affiche /docs/notes.txt",
        .see_also = "add, extract, ls"
    },
    {
        .name = "extract",
        .synopsis = "extract <source> <destination>",
        .description =
            "Extrait un fichier du système de fichiers vers le système hôte.\n"
            "\n"
            "Le fichier source doit exister dans le FS. La destination est\n"
            "un chemin sur le système de fichiers de l'hôte (externe au conteneur).\n"
            "Les wildcards '*' et '?' sont supportés sur le chemin source\n"
            "(ex: extract /docs/*.txt /tmp/).",
        .options = NULL,
        .examples =
            "extract /data.csv ./out.csv    Extrait data.csv vers ./out.csv\n"
            "extract notes.txt /tmp/n.txt   Extrait vers /tmp (système hôte)",
        .see_also = "add, cat, cp"
    },
    {
        .name = "stat",
        .synopsis = "stat <chemin>",
        .description =
            "Affiche les métadonnées détaillées d'un fichier ou répertoire.\n"
            "\n"
            "Montre le type (fichier/répertoire), la taille, les dates de création et\n"
            "de modification ainsi que le chemin parent. Fonctionne aussi sur la racine\n"
            "du système de fichiers. Supporte les wildcards '*' et '?' (ex: stat *.txt).",
        .options = NULL,
        .examples =
            "stat /docs/report.txt    Affiche les métadonnées de report.txt\n"
            "stat /                   Métadonnées de la racine\n"
            "stat ./note.md           Chemin relatif supporté",
        .see_also = "ls, find, tree"
    },
    {
        .name = "cp",
        .synopsis = "cp <source> <destination>",
        .description =
            "Copie un fichier à l'intérieur du système de fichiers.\n"
            "\n"
            "Crée une copie du fichier source avec le chemin destination spécifié.\n"
            "La destination ne doit pas exister. Le répertoire parent de la destination\n"
            "doit exister. La taille et le contenu du fichier original sont préservés.\n"
            "Supporte les wildcards '*' et '?' sur la source (ex: cp /src/*.txt /bak/).",
        .options = NULL,
        .examples =
            "cp notes.txt notes_backup.txt    Copie notes.txt en notes_backup.txt\n"
            "cp file.txt /docs/file.txt       Copie file.txt dans /docs/\n"
            "cp /src/data.csv /backup/data.csv    Copie entre répertoires",
        .see_also = "extract, add, rm"
    },
    {
        .name = "rm",
        .synopsis = "rm [-r] [-f] <chemin>",
        .description =
            "Supprime des fichiers ou répertoires.\n"
            "\n"
            "Supporte les wildcards '*' et '?' sur le chemin. Les répertoires peuvent\n"
            "être supprimés récursivement avec -r. L'option -f ignore les erreurs\n"
            "(entrée manquante, répertoires non vides sans -r). La suppression est\n"
            "définitive (pas de corbeille).",
        .options =
            "-r, -R    Suppression récursive des répertoires\n"
            "-f        Forcer (ignorer les erreurs et les fichiers manquants)",
        .examples =
            "rm old.txt               Supprime old.txt\n"
            "rm -r /temp/logs         Supprime récursivement /temp/logs\n"
            "rm -rf /temp/logs        Force la suppression même si des entrées manquent",
        .see_also = "mkdir, add, cp"
    },
    {
        .name = "help",
        .synopsis = "help",
        .description =
            "Affiche la liste des commandes disponibles.\n"
            "\n"
            "Pour obtenir de l'aide détaillée sur une commande spécifique,\n"
            "utilisez 'man <commande>'.",
        .options = NULL,
        .examples =
            "help                     Liste toutes les commandes\n"
            "man ls                   Manuel détaillé de 'ls'",
        .see_also = "man"
    },
    {
        .name = "man",
        .synopsis = "man <commande>",
        .description =
            "Affiche le manuel d'une commande.\n"
            "\n"
            "Fournit une documentation détaillée incluant la syntaxe, la description,\n"
            "les options disponibles et des exemples d'utilisation.\n"
            "\n"
            "Utilisez 'man -l' ou 'man --list' pour lister toutes les pages disponibles.",
        .options =
            "-l, --list      Lister toutes les pages de manuel disponibles",
        .examples =
            "man ls                   Affiche le manuel de 'ls'\n"
            "man tree                 Affiche le manuel de 'tree'\n"
            "man -l                   Liste toutes les commandes documentées",
        .see_also = "help"
    },
    {
        .name = "mv",
        .synopsis = "mv <source> <destination>",
        .description =
            "Déplace ou renomme un fichier à l'intérieur du système de fichiers.\n"
            "\n"
            "Déplace le fichier source vers la destination spécifiée.\n"
            "La destination ne doit pas exister. Le répertoire parent de la destination\n"
            "doit exister. Le contenu et la taille du fichier sont préservés.\n"
            "Peut être utilisé pour renommer un fichier ou le déplacer vers un autre répertoire.\n"
            "Supporte les wildcards '*' et '?' sur la source (ex: mv /tmp/*.log /logs/).",
        .options = NULL,
        .examples =
            "mv old.txt new.txt       Renomme old.txt en new.txt\n"
            "mv file.txt /docs/       Déplace file.txt vers /docs/\n"
            "mv /src/data.csv /backup/data.csv    Déplace vers un nouveau répertoire",
        .see_also = "cp, rm, cd"
    },
    {
        .name = "clear",
        .synopsis = "clear",
        .description =
            "Efface l'écran du terminal.\n"
            "\n"
            "Nettoie l'affichage du terminal et repositionne le curseur en haut\n"
            "de l'écran. Utile pour améliorer la lisibilité lors de sessions\n"
            "interactives prolongées.",
        .options = NULL,
        .examples =
            "clear                    Efface l'écran du terminal",
        .see_also = "help"
    },
    {
        .name = "exit",
        .synopsis = "exit",
        .description =
            "Quitte le shell interactif.\n"
            "\n"
            "Ferme la session et sauvegarde toutes les modifications\n"
            "apportées au système de fichiers.",
        .options = NULL,
        .examples =
            "exit                     Quitter le shell\n"
            "quit                     Alternative (alias)",
        .see_also = NULL
    }
};

static const int num_pages = sizeof(man_pages) / sizeof(ManPage);

const ManPage *man_get_page(const char *command) {
    for (int i = 0; i < num_pages; i++) {
        if (strcmp(man_pages[i].name, command) == 0) {
            return &man_pages[i];
        }
    }
    return NULL;
}

void man_display(const char *command) {
    const ManPage *page = man_get_page(command);
    
    if (!page) {
        printf("Aucune page de manuel pour '%s'\n", command);
        printf("Essayez 'man --list' pour voir toutes les pages disponibles.\n");
        return;
    }

    // Header
    printf("\n");
    printf("\033[1m%s\033[0m(%d)                  CSFS Shell Manual                  \033[1m%s\033[0m(%d)\n", 
           page->name, 1, page->name, 1);
    printf("\n");

    // NAME section
    printf("\033[1mNOM\033[0m\n");
    printf("       %s - %s\n", page->name, 
           page->description ? (strchr(page->description, '\n') ? 
           strndup(page->description, strchr(page->description, '\n') - page->description) : 
           page->description) : "commande du shell");
    printf("\n");

    // SYNOPSIS section
    printf("\033[1mSYNOPSIS\033[0m\n");
    printf("       \033[1m%s\033[0m\n", page->synopsis);
    printf("\n");

    // DESCRIPTION section
    if (page->description) {
        printf("\033[1mDESCRIPTION\033[0m\n");
        printf("       ");
        const char *p = page->description;
        while (*p) {
            if (*p == '\n') {
                printf("\n       ");
            } else {
                putchar(*p);
            }
            p++;
        }
        printf("\n\n");
    }

    // OPTIONS section
    if (page->options) {
        printf("\033[1mOPTIONS\033[0m\n");
        const char *p = page->options;
        printf("       ");
        while (*p) {
            if (*p == '\n') {
                printf("\n       ");
            } else {
                putchar(*p);
            }
            p++;
        }
        printf("\n\n");
    }

    // EXAMPLES section
    if (page->examples) {
        printf("\033[1mEXEMPLES\033[0m\n");
        const char *p = page->examples;
        printf("       ");
        while (*p) {
            if (*p == '\n') {
                printf("\n       ");
            } else {
                putchar(*p);
            }
            p++;
        }
        printf("\n\n");
    }

    // SEE ALSO section
    if (page->see_also) {
        printf("\033[1mVOIR AUSSI\033[0m\n");
        printf("       %s\n", page->see_also);
        printf("\n");
    }

    // Footer
    printf("CSFS 1.0                          %s                                %s(1)\n",
           "Décembre 2025", page->name);
    printf("\n");
}

void man_list_all(void) {
    printf("\nPages de manuel disponibles:\n\n");
    for (int i = 0; i < num_pages; i++) {
        printf("  \033[1m%-12s\033[0m - ", man_pages[i].name);
        
        // Extract first line of description
        const char *desc = man_pages[i].description;
        if (desc) {
            const char *newline = strchr(desc, '\n');
            if (newline) {
                printf("%.*s\n", (int)(newline - desc), desc);
            } else {
                printf("%s\n", desc);
            }
        } else {
            printf("\n");
        }
    }
    printf("\nUtilisez 'man <commande>' pour afficher une page spécifique.\n\n");
}
