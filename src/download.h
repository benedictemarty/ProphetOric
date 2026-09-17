#ifndef DOWNLOAD_H
#define DOWNLOAD_H
extern char dl_dir[32];             /* dossier cible sur le LOCI ("" = dossier courant, "1:JEUX" = clé USB 1) */
/* Télécharge tous les fichiers du paquet `id` (liste /files/<id>) dans dl_dir.
 * `progress(nom, octets)` est appelé au début et à la fin de chaque fichier.
 * Retour : nombre de fichiers écrits, ou 0 avec un message dans dl_error. */
typedef void (*dl_progress)(const char *name, unsigned long bytes);
unsigned char download_package(const char *id, dl_progress progress);
extern const char *dl_error;
extern char dl_last_tap[64];        /* chemin LOCI du dernier .tap téléchargé ("" = aucun) */
extern char dl_last_dsk[64];        /* idem pour un .dsk */
extern unsigned char dl_skipped;    /* fichiers ignorés (.zip : inutilisables sur Oric) */
#endif
