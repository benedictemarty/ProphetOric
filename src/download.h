#ifndef DOWNLOAD_H
#define DOWNLOAD_H
#include "cli.h"
extern char dl_dir[32];             /* dossier cible sur le LOCI ("" = dossier courant, "1:JEUX" = clé USB 1) */

/* Métadonnées du paquet `id` (fiche) : /files (obligatoire), puis /crc32 et /requires
 * (facultatifs : 404 = absent). Retour 0 avec dl_error si /files échoue. */
#define DL_MAX_REQ 4
unsigned char dl_fetch_meta(const char *id);
extern unsigned char dl_nfiles;
extern struct cli_file dl_files[CLI_MAX_FILES];
extern unsigned char dl_ncrc;       /* empreintes CRC-32 (une par fichier), 0 = pas de vérification */
extern char *dl_crc[CLI_MAX_FILES];
extern unsigned char dl_nreq;       /* composants minimums déclarés (informatif) */
extern char *dl_req[DL_MAX_REQ];

/* 1 si le marqueur <dossier>/.prophet/<id>.crc a les empreintes actuelles du serveur et que
 * le fichier à lancer est présent : dl_last_tap / dl_last_dsk sont alors posés (après dl_fetch_meta). */
unsigned char dl_installed(const char *id);

/* Télécharge tous les fichiers du paquet (après dl_fetch_meta) dans dl_dir, vérifie chaque
 * fichier par CRC-32 et écrit le marqueur. `progress(nom, octets)` est appelé au début, entre
 * les tranches et à la fin de chaque fichier (dl_phase = 1 : vérification).
 * Retour : nombre de fichiers écrits, 0 ou moins que prévu avec un message dans dl_error. */
typedef void (*dl_progress)(const char *name, unsigned long bytes);
extern unsigned long dl_base;       /* octets déjà reçus des tranches précédentes du fichier en cours (pour l'indicateur) */
extern unsigned char dl_index;      /* fichier en cours (0..dl_nfiles-1) */
extern unsigned char dl_phase;      /* 0 = réception, 1 = vérification CRC-32 */
extern unsigned long dl_total;      /* taille du fichier en cours (0xFFFFFFFF = inconnue) */
extern unsigned char dl_verified;   /* 1 : tous les fichiers vérifiés par CRC-32 */
unsigned char download_package(const char *id, dl_progress progress);
extern const char *dl_error;
extern char dl_last_tap[64];        /* chemin LOCI du .tap à lancer ("" = aucun) */
extern char dl_last_dsk[64];        /* idem pour un .dsk */
extern unsigned char dl_skipped;    /* fichiers ignorés (.zip : inutilisables sur Oric) */
#endif
