# Message à l'équipe Phosphoric — backend `picowifi` : pas de contrôle de flux en réception

*(ProphetOric, 2026-09-17 ; copie déposée dans `~/Oric1/`.)*

## Constat

`src/io/serial_picowifi.c` : `picowifi_recv()` lit jusqu'à 256 octets de la
socket à chaque sondage de l'ACIA (`pw_conn_read`) et les pousse dans l'anneau
`rx_buf` de `PW_RX_BUFSZ` = 64 Ko ; `pw_rx_push()` **jette silencieusement**
l'octet quand l'anneau est plein (`if (pw->rx_count >= PW_RX_BUFSZ) return;`).

Conséquence : dès que le serveur envoie plus de 64 Ko plus vite que l'Oric ne
lit (toujours le cas : l'ACIA est rythmée à 9600 bauds, la socket locale
délivre 1 Mo en quelques ms), tout ce qui dépasse l'anneau est perdu, sans
trace. Observé avec ProphetOric : un `.dsk` de 1 Mo servi par un prophetd local
s'arrête à ~69 Ko (64 Ko d'anneau + FIFO), puis « peer closed connection » et
`NO CARRIER` alors que le client attend encore 950 Ko.

Le vrai PicoWiFiModemUSB, lui, ne lit la socket qu'au rythme où il peut
transmettre (fenêtre TCP de lwIP) : aucune perte, juste de la latence.

## Reproduction

```
# prophetd local (ou n'importe quel serveur HTTP) servant un fichier > 64 Ko
./oric1-emu -r roms/basic11b.rom -t ~/ProphetOric/build/prophet-test.tap -f \
  --loci --loci-usb none --loci-flash /tmp/flash --serial picowifi:Test --serial-buffer 32 \
  --headless --realtime ...   # voir ~/ProphetOric/tests/run.sh, scénario `dsk` (LONG=1)
```
Avant le contournement client, `hello.dsk` s'arrêtait à 69 220 octets sur 1 024 256.

## Suggestion

Ne lire la socket que quand l'anneau a de la place (`pw_conn_read(pw, tmp,
min(256, PW_RX_BUFSZ - rx_count))`, ou ne pas lire du tout si `rx_count >
seuil`) : le noyau applique alors la fenêtre TCP au serveur, comme le fait le
Pico. Idéalement, journaliser toute perte (`log_warning`) si vous gardez un
rejet.

## Contournement côté ProphetOric (0.5.0)

Téléchargement par tranches `Range: bytes=a-b` de 32 Ko (une connexion par
tranche, réponse 206 qui tient dans l'anneau), avec reprise en ajout sur
coupure. Ça fonctionne, mais un client comme `prophet.neo`/OricTel qui lit un
gros flux d'un trait restera exposé dans l'émulateur.
