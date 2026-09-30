/*
 * himem.h — tampons placés en $A000-$B3FF : zone de l'écran HIRES, libre en mode TEXT
 * (ProphetOric ne passe jamais en HIRES ; jeux de caractères en $B400, écran en $BB80).
 * La RAM du programme ($0501-$97FF, cfg/prophetoric.cfg) est pleine : ces tampons n'y
 * sont pas. Rien n'est lié au-delà de RAMEND = $A000.
 */
#ifndef HIMEM_H
#define HIMEM_H
#define LNG_BUF   ((char *)0xA000)   /* fichier de langue EN.LNG / ES.LNG (lang.c) */
#define LNG_MAX   0x0A00             /* 2 560 octets : $A000-$A9FF */
#define BODY_BUF  ((char *)0xAA00)   /* réponse HTTP courante, parsée en place (main.c) */
#define BODY_SIZE 0x0800             /* 2 048 octets : $AA00-$B1FF */
#define FILES_BUF ((char *)0xB200)   /* réponse de /files/<id> (download.c) */
#define FILES_SIZE 0x0200            /* 512 octets : $B200-$B3FF */
#endif
