#ifndef EDITOR_H
#define EDITOR_H

#include "shell.h"

// Ouvre un éditeur de texte pour un fichier du système de fichiers
// Retourne 0 si succès, -1 si erreur
int editor_open(Shell *shell, const char *fs_path);

#endif // EDITOR_H
