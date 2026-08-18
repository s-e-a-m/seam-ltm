# Log di sessione — strx: tabella per bande e prima taratura di uno STONE

Data: 2026-08-18
Operatore: Giuseppe Silvi
Modalità: **taratura** (prima uscita del sistema di misura in condizioni reali)

---

## Cosa è stato costruito

Il metodo, deciso in questa sessione e più stretto di quanto la roadmap
prevedesse: multipink → STONE → coppia AB → strx, e strx dice **soltanto** di
quanto e dove il segnale captato si discosta dal pink. I filtri li mette
l'operatore sul finale, con quelli che ha già. Niente memoria in strx, niente
sintesi automatica, niente auto-apply — le Spec 3 e 4 del log del 2026-07-14
restano sostituite da questo, per il ramo pink.

Perché il ramo pink costa poco: il pink è stazionario e il suo spettro è noto,
quindi non serve misurare una funzione di trasferimento. In bande a banda
relativa costante il pink porta energia uguale in ogni banda, quindi il
bersaglio non è una curva ma una retta a zero. Niente Δt, niente fase, niente
sincronizzazione campione-esatta.

- **Spec Faust** (`seam.analyzers.lib`, sezione ISO 266): `san.tob_fc`,
  `san.tob_tau`, `san.thirdoctave_levels_ab`, `san.pinkdev`, `san.pinkcomp`,
  `san.stone_pinkcomp`. Scritta prima del C++, secondo la convenzione.
- **Porting** (`plugins/strx/source/strx_bands.h`): parità numerica con
  `faust -double` a **7,4e-15** su 40 campioni della risposta all'impulso.
- **Vista** (`strx_bandtable.h`): 31 barre sull'asse log dello spettro, numeri
  per ottava, valore stampato solo sul picco di ogni escursione.

Tre scelte di progetto, ciascuna con il suo test e ciascun test verificato
rompendo il codice apposta:

| scelta | perché | test che la difende |
|---|---|---|
| media in **potenza** di L e R, mai il Mid | a 40 cm le capsule si scorrelano sopra c/2d = 430 Hz, e una somma a Mid pettina per geometria | canale R ritardato di 56 campioni (i 40 cm in aria) |
| τ per banda = BT/B | con una τ unica le bande basse ballano e le alte sono in ritardo | BT costante dove τ è libera |
| segno = compensazione, non scostamento | è l'unica cosa che un commento non può garantire | buco iniettato a 125 Hz |

## Le tre cose che ha detto la strada e non il codice

1. **La griglia dello spettro**: le etichette erano disegnate in `Structure`
   invece che in `TextLight` — violazione dello standard in `ui-style.md`, non
   questione di gusto. E un livello di righe più marcate sulle decadi, non
   etichettate, rompeva il passo visivo dei terzi: rimosso.
2. **multipink non è pink a 96 kHz.** Con multipink diretto in strx la tabella
   segnava +3,3 dB sui 20 Hz. Non era strx: i coefficienti di Kellet sono un
   fit fatto nel piano z, quindi le loro frequenze sono frazioni di fs.
   Verificato sulla risposta del filtro, rispetto al pink ideale e riferito a
   1 kHz:

   | f | 44,1 kHz | 48 kHz | 96 kHz |
   |---|---|---|---|
   | 20 Hz | −0,20 | −0,45 | **−3,09** |
   | 31,5 Hz | +0,51 | +0,39 | **−1,50** |
   | da 63 Hz | ~0 | ~0 | ~0 |

   Lo stesso difetto è in `no.pink_filter` di GRAME: multipink aveva portato
   fedelmente la spec. Indagine aperta, vedi sotto.
3. **Una banda schiacciata contro Nyquist non è una misura.** A 44,1 kHz la
   banda dei 20 kHz (17783–22387 Hz) sta mezza sopra Nyquist e leggeva −3,0 dB
   su un segnale esattamente pink. L'errore contro il rapporto bordo/Nyquist:
   0,47→+0,15 · 0,74→+0,20 · 0,81→−0,12 · 0,93→−0,65 · 1,02→−3,01. Oltre 0,85
   la banda ora **si tace** invece di riportare un numero che sembra usabile.

## Prova su strada

**Uno STONE tarato con il nuovo sistema.** Riferito dall'operatore: procedura
**lunga e laboriosa**, sono stati usati **tutti i filtri messi a disposizione
dal finale di potenza**, e il risultato è *"dritto e pulito come non mai"*.

È la prima validazione del metodo su hardware reale, e valida insieme: la
tabella per bande come strumento di lettura, la convenzione di segno, e la
scelta di fermarsi alla descrizione lasciando la correzione all'operatore.

### Setup e correzione applicata

- **Diffusore**: STONED, versione in metallo, coni da 8 pollici.
- **Amplificazione**: due t.amp DSP Quadro 500 (software QUADRO 500 DSP V2
  v1.7.4), ciascuno **in bridge** su due canali, per arrivare ai 4 canali dello
  STONE. La correzione è **identica sui due canali** di ogni ponte — verificato
  sul preset, i due banchi sono uguali byte per byte.
- Compressore e noise gate in bypass; guadagno di canale −15; passa-basso in
  bypass.

Il preset è `2026-08-13-STONED-BRIDGE-FLAT.preset` (la data nel nome è
sbagliata: il file è del 18 agosto). Decodificato con
`tools/decode-tamp-preset.py`:

| filtro | tipo | frequenza | Q | guadagno |
|---|---|---|---|---|
| passa-alto | BW24 | 38,5 Hz | — | — |
| EQ1 | LSF | 90,5 Hz | — | **+9,0 dB** |
| EQ2 | HSF | 119,4 Hz | — | **−18,0 dB** |
| EQ3 | Peak | 198,4 Hz | 2,0 | +1,5 dB |
| EQ4 | Peak | 477,4 Hz | 1,0 | **−16,0 dB** |
| EQ5 | Peak | 1381,9 Hz | 2,0 | −9,5 dB |
| EQ6 | Peak | 2462,3 Hz | 1,5 | −6,0 dB |
| EQ7 | Peak | 6498,0 Hz | 1,5 | −7,5 dB |
| EQ8 | HSF | 15277,5 Hz | — | +9,0 dB |

Identica su CH1 e CH3, cioè sui due lati "master" dei due ponti. La decodifica
riproduce riga per riga la schermata del software, verificata su fotografia.

Le due scale del formato sono griglie a temperamento equabile: la frequenza
avanza di **1/30 di ottava** per passo (ancorata a 1 kHz), il Q di un rapporto
di **semitono** (byte 16 = Q 1,0, e il default byte 35 cade su 3,0).

La forma complessiva conferma la voicing "musica da camera" del log del
2026-07-14 — forte attenuazione delle medio-alte perché il grave emerga — e vi
aggiunge correzioni puntuali che prima non c'erano: −16 dB stretti a 477 Hz,
−9,5 a 1382, −6 a 2462, −7,5 a 6498.

**Da chiarire**: su CH2 e CH4 — i lati *schiavi* dei due ponti — resta acceso
un filtro solo, HSF 125,0 Hz −18,0 dB, che non compare nella pagina di CH1. Se
il DSP lo applica all'uscita in ponte, nella catena c'è uno shelf in più di
quanto l'operatore intendesse; se il ponte usa solo il ramo master, è un
residuo innocuo. In entrambi i casi la misura di strx lo comprendeva, quindi la
taratura resta valida — ma va deciso prima di replicarla su un altro STONE.

**Non registrato in questa sessione** (da catturare alla prossima):
- l'ambiente, e la posizione dello STONE al suo interno;
- i valori per banda **prima** della correzione, che avrebbero dato la misura
  del guadagno (il preset dice cosa è stato fatto, non da cosa si partiva);
- il numero di posizioni del microfono e di iterazioni;
- la frequenza di campionamento della sessione di taratura.

La tabella sotto-riporta per costruzione (un buco stretto da 6 dB legge ~3,5),
quindi il fatto che sia servita l'intera dotazione di filtri del finale è
coerente con il metodo, non un sintomo.

## Aperto

- **Filtro di pinking di multipink** — indagine avviata; vedi
  `doc/study/2026-08-18-pink-filter-literature-audit.md`. Qualunque strada si
  scelga **invalida la taratura** (`kCalibrationOffsetDb`, procedura sox in
  `plugins/multipink/doc/calibration.md`): un pinking diverso ha
  un'attenuazione RMS diversa.
- **Il campo di misura utile** finisce a 16 kHz a 44,1 e 48 kHz. Per leggere i
  20 kHz serve lavorare a 88,2/96 — dove però il pink oggi è sbagliato in
  basso. Le due cose si sbloccano insieme.

## Commit

- `bcf0539` generatori: bus di ingresso dichiarato, categoria `Fx|Generator`
- `99def21` griglia a bande ISO 266 sull'asse dello spettro
- `0518776` asse a 24 kHz, etichette in TextLight, luminosità misurate
- `3ae732e` tabella per bande ISO 266
- `5297325` griglia a due livelli, 16k, gate di Nyquist
- faust-libraries: `279037e`, `fc7d707`
