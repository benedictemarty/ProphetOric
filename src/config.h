#ifndef CONFIG_H
#define CONFIG_H
/* PROPHET.CFG sur le LOCI (dossier courant du volume de démarrage) : 4 lignes
 * hôte / port / dossier cible / mot de passe (les deux dernières optionnelles). */
unsigned char config_load(void);
unsigned char config_save(void);
#endif
