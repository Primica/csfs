#ifndef COMPLETION_H
#define COMPLETION_H

#include "shell.h"

// Structure pour stocker les suggestions de complétion
typedef struct {
    char **suggestions;
    int count;
} Completions;

// Libère la mémoire allouée pour les complétions
void completions_free(Completions *comp);

// Complète le buffer basé sur le contenu actuel
// Retourne le nombre de complétions trouvées
int shell_complete(Shell *shell, char *buffer, int *pos, int show_all);

#endif // COMPLETION_H
