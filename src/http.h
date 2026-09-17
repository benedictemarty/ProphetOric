/*
 * http.h — GET HTTP/1.1 minimal au-dessus du modem Hayes (LOCI +
 * PicoWiFiModemUSB, ACIA 6551 $0380) : ATZ → ATD hôte:port → requête →
 * en-têtes (jusqu'à \r\n\r\n) → corps (Content-Length) → raccrochage.
 * Une connexion par requête (le serveur Prophet répond `Connection: close`
 * au client texte).
 */
#ifndef HTTP_H
#define HTTP_H

#ifdef TEST_HOST
#define __fastcall__
#endif

extern char http_host[40];      /* hôte ou IP */
extern char http_port[6];       /* port décimal */
extern char http_pass[32];      /* mot de passe des zones réservées ("" = aucun) */
extern unsigned int http_status;      /* code de la dernière réponse (0 = pas de réponse) */
extern unsigned long http_length;     /* Content-Length annoncé (0xFFFFFFFF = absent) */
extern const char *http_error;        /* texte de la dernière erreur, ou NUL */

/* Analyse un bloc d'en-têtes HTTP (terminé par \r\n\r\n ou NUL) : statut,
 * Content-Length. Retour 1 si la ligne de statut est valide. Testable sur l'hôte. */
unsigned char http_parse_headers(const char *hdr);

/* GET path (avec ResponseFormat: cli, Connection: close, X-Prophet-Password
 * si défini, Range si `range` non NUL, ex. "bytes=0-2047"). Le corps est copié
 * dans buf (au plus max-1 octets, terminé par NUL), *len = octets reçus.
 * Retour 1 si une réponse a été reçue (voir http_status), 0 sinon (http_error). */
unsigned char http_get(const char *path, const char *range, char *buf, unsigned int max, unsigned int *len);

#endif
