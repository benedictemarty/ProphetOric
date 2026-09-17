#ifndef FAKE_SERIAL_H
#define FAKE_SERIAL_H
void fake_reset(const char *http_reply);   /* réponse renvoyée après la ligne vide de la requête */
const char *fake_tx(void);                 /* tout ce que le client a émis */
unsigned char serial_poll(void);
unsigned char serial_recv(void);
void serial_send(unsigned char b);
void serial_tx_flush(void);
#endif
