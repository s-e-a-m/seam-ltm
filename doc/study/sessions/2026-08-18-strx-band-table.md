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

| filtro | tipo | frequenza | guadagno | Q (byte grezzo) |
|---|---|---|---|---|
| passa-alto | BW24 | 38,6 Hz | — | — |
| EQ1 | LSF | 90,5 Hz | **+9,0 dB** | 35 (default) |
| EQ2 | HSF | 119,3 Hz | **−18,0 dB** | 35 (default) |
| EQ3 | PEQ | 198,0 Hz | +1,5 dB | 28 |
| EQ4 | PEQ | 475,0 Hz | **−16,0 dB** | 16 (= Q 1.0) |
| EQ5 | PEQ | 1369,8 Hz | −9,5 dB | 28 |
| EQ6 | PEQ | 2435,9 Hz | −6,0 dB | 23 |
| EQ7 | PEQ | 6407,2 Hz | −7,5 dB | 23 |
| EQ8 | HSF | 15019,8 Hz | +9,0 dB | 35 (default) |

EQ1 ed EQ2 sono stati letti dall'interfaccia e coincidono con la decodifica: è
la verifica della chiave, non un adattamento. La forma complessiva conferma la
voicing "musica da camera" del log del 2026-07-14 — forte attenuazione delle
medio-alte perché il grave emerga — e vi aggiunge correzioni puntuali che prima
non c'erano.

**Aperto sul preset**: la scala del Q non è ancora determinata (un solo punto
noto, byte 16 = Q 1.0), e il decoder trova un **decimo filtro attivo** —
HSF 124,9 Hz, −18,0 dB — che l'operatore non ha enumerato. Da verificare
sull'interfaccia prima di fidarsi del decoder su preset futuri.

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
