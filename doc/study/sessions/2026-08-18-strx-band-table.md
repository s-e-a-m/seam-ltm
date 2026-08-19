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
un filtro solo, HSF 125,0 Hz −18,0 dB, che non compare nella pagina di CH1, e
il loro passa-alto è a 35,1 Hz invece che a 38,5. Sono residui della voicing
precedente: il preset del 2023 (`STONE.preset`) porta lo stesso shelf, HSF
101,5 Hz −16,0 dB, su **tutti e quattro** i canali — ed è esattamente il valore
annotato a mano nel log del 2026-07-14, il che ha validato la decodifica contro
una fonte indipendente. Tarando sono stati riscritti CH1 e CH3; sui due schiavi
è rimasto il vecchio. Se il DSP applica il ramo schiavo all'uscita in ponte
nella catena c'è uno shelf in più di quanto si intendesse; se il ponte usa solo
il master, è innocuo. In entrambi i casi la misura di strx lo comprendeva,
quindi la taratura resta valida — ma va deciso prima di replicarla.

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

- **Filtro di pinking di multipink** — RISOLTO il 2026-08-19: progettato un
  nuovo filtro matched-Z che sostituisce nettamente il vecchio fit, passa il
  test SMPTE a tutte le fs. La taratura è stata infatti rifatta —
  `kCalibrationOffsetDb` ricalcolato (Task 5), non più misurato con sox — vedi
  `doc/study/sessions/2026-08-19-pink-filter-design.md` e
  `plugins/multipink/doc/calibration.md`.
- **Il campo di misura utile** finisce a 16 kHz a 44,1 e 48 kHz. Per leggere i
  20 kHz serve lavorare a 88,2/96, e da qui il pink non è più il blocco: il
  filtro nuovo del 2026-08-19 è corretto anche in basso a quelle fs. Resta
  aperta solo l'estensione della griglia di `strx` sopra i 20 kHz, un
  perimetro a sé, non toccato dal lavoro sul pinking.

## Commit

- `bcf0539` generatori: bus di ingresso dichiarato, categoria `Fx|Generator`
- `99def21` griglia a bande ISO 266 sull'asse dello spettro
- `0518776` asse a 24 kHz, etichette in TextLight, luminosità misurate
- `3ae732e` tabella per bande ISO 266
- `5297325` griglia a due livelli, 16k, gate di Nyquist
- faust-libraries: `279037e`, `fc7d707`

---

## Sera: export della tabella, ripetibilità e tre posizioni di microfono

### SAVE TABLE

Aggiunto a strx un bottone che scrive la tabella in `/Volumes/Aleph/strx` come
file di testo leggibile, con il contesto e non solo i 31 numeri: frequenza di
campionamento, sorgente dal bus, secondi di integrazione, bande misurabili, e
per banda la compensazione, il **livello grezzo** e lo stato di assestamento.
I campi `position` e `note` sono scritti vuoti perché li compili l'operatore.

Al primo uso reale ha mostrato subito un difetto: un file salvato prima che
l'analizzatore avesse girato dichiarava `bands: 0 of 31` e poi stampava **31
righe di zeri perfetti** — una misura impeccabile del nulla. Corretto: ora in
quel caso non c'è tabella, c'è una frase che dice che non c'è misura.

### Ripetibilità dello strumento

Quattro salvataggi consecutivi nella stessa posizione:

| regione | escursione fra salvataggi |
|---|---|
| 250 Hz – 16 kHz | **≤ 0,7 dB**, spesso ≤ 0,3 |
| 63 – 200 Hz | ≤ 1,4 dB |
| sotto i 50 Hz | 2,3 – 6,4 dB |

**Sotto il passa-alto del finale non c'è misura.** A 20 Hz la tabella chiedeva
+11 dB: è il passa-alto a 38,5 Hz BW24 dell'operatore, misurato fedelmente. Ma
il livello grezzo (−65,6 dB contro −53) è **12 dB** sotto la media mentre il
filtro ne toglierebbe ~20: la differenza è il **rumore di fondo della stanza**
che riempie quelle bande. Sotto i 50 Hz non si misura il diffusore, si misura
l'ambiente — ed è per questo che oscilla di 6 dB.

Regola operativa: **nessuna banda sotto il passa-alto del finale è una misura.**

### Tre posizioni di microfono

| | posizione | integrazione |
|---|---|---|
| A | frontale, 1 m, stessa altezza, lontano dalle pareti | 30–51 s |
| C | di lato, 1 m, stessa altezza, vicino a una parete | 36–55 s |
| B | 60 cm dal basso, in faccia a un cono | 116–121 s |

(Fra C e B il contatore non si è azzerato ma sono passati 62 s, contro un 3τ
massimo di 33 s: della posizione precedente resta lo 0,3%. Le misure sono
pulite. Se avesse salvato dieci secondi dopo lo spostamento non lo sarebbero
state — manca un gesto di "riparto pulito".)

**Il risultato principale: A e C concordano entro ±0,7 dB — quasi tutte entro
0,3 — da 200 Hz a 16 kHz.** Due posizioni completamente diverse, una a ridosso
di una parete e una in campo libero, danno la stessa risposta su sette ottave.
È la **sfericità dello STONED misurata**, non affermata: una direttività
marcata sarebbe emersa ruotando di 90°.

**Sotto i 200 Hz divergono fino a 3,8 dB** (80 Hz: A −1,1 contro C +2,6; 125 e
160 Hz: C ~3 dB più caldo): è il rinforzo di parete. Quindi **nel tuo studio il
confine fra "diffusore" e "stanza" cade intorno ai 200 Hz**, misurato e non
assunto. Sopra si corregge sul finale, sotto è lavoro del futuro stadio di sala.

**La posizione B non è una posizione di taratura.** Differisce da A di −4,3 dB
a 1,6 kHz e −2,5 a 12,5 kHz, dove A e C concordavano entro 0,3. A 60 cm si è
nel campo vicino di **un solo cono**: i quattro driver non si sono ancora fusi.
La trappola è che è la misura **più ripetibile di tutte** (0,2–0,5 dB): misura
benissimo la cosa sbagliata.

### Lo zero è spostato dal passa-alto

La media che fissa lo zero include le bande a 31,5 e 40 Hz, ~7 dB sotto per via
del passa-alto: due bande su ventotto tirano giù il riferimento di **~0,7 dB**.
Con una **mediana** delle sole bande sopra il passa-alto il residuo medio sopra
i 200 Hz passa da −0,50 a +0,20 dB. Proposta in sospeso: cambiare lo zero di
strx da media a mediana, in C++ e nella spec Faust.

### Cosa è lo STONED, misurato

Con A e C e zero robusto, sopra i 200 Hz: tutto entro **±1,0 dB** tranne
**16 kHz, +3,0 dB** — presente in entrambe le posizioni (escursione interna
0,3, differenza fra posizioni 0,5). È l'unica anomalia reale.

### Proposta per i 16 kHz (non ancora applicata)

Lo shelf esistente (HSF 15277 Hz, +9 dB) dà già +5,2 a 16 k e +8,7 a 20 k, e
non può fare la forma che serve — il difetto è largo un terzo d'ottava, uno
shelf sale monotòno. Quantificato:

| mossa sullo shelf | 10k | 12,5k | 16k | 20k |
|---|---|---|---|---|
| gain a +15 | +0,6 | +1,6 | +3,3 | **+5,6** (banda non misurabile a 48 kHz) |
| angolo a 10 k | **+3,9** | **+5,2** | +3,4 | +0,3 |

Proposta: **Peak 16000 Hz, Q 3,03, +3,0 dB** su CH1 e CH3. Entrambi i valori
cadono esattamente sulla griglia del Quadro 500 (indice 290; Q indice 9).
Effetto: +2,9 a 16 k, +0,3 a 12,5 k, **+0,1 a 20 k**.

Costo: gli otto slot sono pieni. Il candidato da cedere è EQ3 (198,4 Hz, +1,5
dB), il più piccolo dell'insieme; senza di lui i 200 Hz leggerebbero ~1,2 dB
caldi. **Prima però va risolta la domanda su CH2/CH4**: se in ponte quel banco
è nel percorso del segnale ci sono otto filtri in più e non si cede niente. Si
verifica mettendo su CH2 un picco vistoso (−12 dB a 1 kHz, Q 1) e guardando se
strx lo vede.

### Aperto per domani

- [ ] test CH2/CH4: il banco schiavo è nel percorso del segnale?
- [ ] decidere il picco a 16 kHz (e se cedere EQ3)
- [ ] mediana al posto della media per lo zero
- [ ] una misura a 96 kHz per vedere la banda dei 20 kHz (a 96 k il pink è
      sbagliato **sotto i 63 Hz**, non in alto: per guardare lassù è valido)
- [x] pink: sostituzione netta, non A/B. Né il fit invfreqz a 3 poli (A) né
      `fi.spectral_tilt` (B) passano il test di accettazione a nessuna fs —
      A perché il fit di partenza è già fuori tolleranza, B per il warping
      della trasformazione bilineare. Progettato un nuovo filtro matched-Z
      (scala di poli/zeri anchorata in Hz più una sezione di correzione a
      coefficienti fissi) che passa a tutte le sei fs, 0,071–0,081 dB contro
      0,25 dB di tolleranza. Spec:
      `docs/superpowers/specs/2026-08-19-pink-filter-mz-design.md`; diario:
      `doc/study/sessions/2026-08-19-pink-filter-design.md`.

## Test di accettazione SMPTE per il filtro di pinking

Costruito prima di scegliere il filtro, perché la scelta diventi una misura e
non un'opinione: `tests/multipink_pink_test.cpp` porta la risposta del filtro
attraverso il banco ISO 266 di `strx_bands.h` e verifica la tolleranza di
**SMPTE ST 2095-1: ±0,25 dB per terzo d'ottava da 20 Hz a 16 kHz**.

**Analitico, non a rumore.** La tolleranza è ±0,25 dB e un livello di banda
misurato su rumore porta ±0,6 dB di dispersione a BT=50: un test a rumore non
potrebbe risolvere ciò che deve giudicare. Con ingresso bianco lo spettro di
potenza in uscita **è** |H(f)|², quindi l'energia di banda è l'integrale di
|H|² sulla banda, calcolato esattamente.

| fs | scostamento peggiore | esito |
|---|---|---|
| 44,1 kHz | **0,41 dB** | FAIL |
| 48 kHz | **0,60 dB** | FAIL |
| 88,2 kHz | 2,40 dB | FAIL |
| 96 kHz | 2,68 dB | FAIL |
| 192 kHz | 4,99 dB | FAIL |

**Il risultato che riorienta la questione: fallisce anche alla frequenza a cui
è stato fittato.** Non è un filtro corretto che si guasta alzando fs — è un
filtro che non è mai stato di qualità metrologica, e a 96 kHz il difetto
diventa solo abbastanza grande da vedersi in una misura di sala.

Conseguenza sulla rosa dei candidati: **A (rimappare il fit esistente) non può
passare a nessuna frequenza**, perché rimappare riproduce la risposta alla
frequenza di progetto e quella è già fuori tolleranza di 0,41 dB. A resta utile
solo come *ponte di compatibilità* con le misure vecchie, mai come metodo
primario. Il campo si stringe su **B** (`fi.spectral_tilt`, ordine da
determinare col test) — o su un fit nuovo di ordine più alto, che il test
giudicherebbe allo stesso modo.

Nota sulla severità: la deviazione è calcolata rispetto alla **media** delle
bande, cioè nell'interpretazione più favorevole (SMPTE fissa un livello
assoluto per banda; permettere il trim di guadagno complessivo è generoso). I
numeri sopra sono quindi un **limite inferiore**: il filtro non può fare meglio
di così nemmeno con il guadagno ottimizzato.

Distinzione da tenere presente: i coefficienti in uso sono il fit **invfreqz a
3 poli** di GRAME, non i coefficienti "instrumentation grade" di Kellett
(±0,05 dB dichiarati, filtro diverso e più lungo). Sono due cose che la
letteratura confonde spesso.
