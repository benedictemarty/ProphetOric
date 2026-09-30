/*
 * lang.h — textes de l'interface (src/strings.def) : T(S_xxx). Français compilé ;
 * anglais / espagnol lus sur le LOCI (EN.LNG, ES.LNG, générés par tools/mklng.c) et
 * gardés en $A000-$A9FF (himem.h : zone HIRES libre en mode TEXT, la RAM du programme est pleine).
 * Fichier .LNG : enregistrements terminés par NUL, le premier = "PLNG <version>",
 * puis S_COUNT textes dans l'ordre de strings.def ; autre version ou compte faux → refusé.
 */
#ifndef LANG_H
#define LANG_H

#define S(id, max, fr, en, es) id,
enum { 
#include "strings.def"
S_COUNT };
#undef S

extern char lang_code[3];                  /* "fr", "en", "es" */
const char *T(unsigned char id);
/* "fr" : textes compilés ; "en"/"es" : charge <CODE>.LNG (à côté de PROPHET.CFG, sinon dans
 * le dossier de téléchargement) ; retour 0 si
 * absent ou refusé (on reste alors en français, lang_code = "fr") */
unsigned char lang_set(const char *code);

#endif
