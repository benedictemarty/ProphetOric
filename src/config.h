#ifndef CONFIG_H
#define CONFIG_H
/* PROPHET.CFG sur le LOCI (dossier courant du volume de démarrage) : 5 lignes
 * hôte / port / dossier cible / mot de passe / langue (fr, en, es) ; les trois dernières
 * sont optionnelles (fichiers des versions < 0.9.0 : 4 lignes). */
extern char cfg_lang[3];            /* langue lue ("" = absente) */
unsigned char config_load(void);
unsigned char config_save(void);
#endif
