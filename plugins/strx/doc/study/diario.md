# Diario di strx — analizzatore M/S di osservazione per STONE

Sessione: 2026-07-16.
Autore: Giuseppe Silvi (con Claude Code).
Repo: seam-ltm, plugin `plugins/strx`.

Questo diario racconta la nascita di **strx**, il primo strumento del sistema di auto-taratura degli altoparlanti **STONE**.
È scritto in italiano di proposito, per lasciare traccia narrativa del ragionamento DSP, che la letteratura in italiano copre poco.
La documentazione formale in inglese vive accanto (`doc/README.md`, i blocchi `FAUST REFERENCE` nel codice).

---

## 1. Cosa è strx, e perché nasce

`strx` è un **analizzatore M/S di osservazione**: un ricevitore che si mette su una traccia microfonica e legge la risposta della sala a uno STONE, senza toccare l'audio.
Fa passare il segnale invariato e ne ricava tre viste vive: un **goniometro** con scia, una **curva spettrale M+S**, e le colonne dei **meter** In L/R · M · S · Width.
È l'oggetto della "modalità osservazione" da cui era partita l'idea del sistema STONE.
Sta in piedi da solo e serve già oggi; le spec successive (bus di taratura, funzione di trasferimento, auto-EQ) gli si agganciano sopra senza riscriverlo.

La regola del progetto guida tutto: **Faust è la specifica, il C++ leggibile è il deliverable**.
Ogni processo d'analisi cita in un commento la sua ancora Faust; dove Faust non aveva la funzione, l'abbiamo scritta noi nelle librerie SEAM.

---

## 2. La matrice M/S e la geometria del goniometro

Il cuore è la **matrice somma-e-differenza di Blumlein** (`sst.sdmx` nelle librerie Faust), energy-preserving:

```
M = (L + R) / √2      (Mid, il "monofonico")
S = (L − R) / √2      (Side, la "differenza")
```

Questo è anche, letteralmente, il **processo del goniometro**.
Il goniometro non è altro che il Lissajous L/R **ruotato di 45°**: si disegna il punto `(x = Side, y = Mid)`.
Da questa scelta discende tutta la lettura visiva:

- un segnale **mono** ha `L = R`, quindi `S = 0`: il punto vive sull'asse verticale → **linea verticale**;
- un segnale in **anti-fase** ha `L = −R`, quindi `M = 0`: il punto vive sull'asse orizzontale → **linea orizzontale**;
- una sinusoide in **quadratura** (L e R a 90°, come dall'Hilbert) traccia un **cerchio pieno**.

La cosa elegante è che *la stessa* geometria M/S serve sia al goniometro sia ai meter: un solo modello mentale per tutto il plugin.

Un dettaglio che ci ha morso: il verso.
Avevo scelto `x = S = (L − R)`, che manda un segnale **panpottato a sinistra** verso **destra** dello schermo — l'opposto della convenzione classica (e di Melda).
La correzione è stata specchiare l'asse orizzontale (`x = −S`) e scambiare le etichette, così L sta a sinistra.
Con lo specchiamento applicato **identico** ai punti, all'ago e alle etichette, la coerenza torna: sinistra a sinistra, e il PANORAMA −100% coincide con ciò che vedi.

---

## 3. I descrittori nuovi: correlation, width, panorama, vectorangle

Correlazione, larghezza stereo, bilanciamento e angolo del vettore **non esistevano** né nelle librerie Faust ufficiali né in quelle SEAM.
Li abbiamo scritti come nuove funzioni **numeriche, senza GUI**, in `seam.analyzers.lib`, ciascuna con il suo test inline — sono candidabili a un contributo upstream a GRAME.

```
correlation(l,r) = mean(l·r) / √(mean(l²)·mean(r²))          → [−1, +1]
width(l,r)       = rmsS / (rmsM + rmsS)                       → [0, 1], 0 = mono
panorama(l,r)    = (mean(r²) − mean(l²)) / (mean(r²)+mean(l²))→ [−1, +1]
vectorangle(l,r) = ½·atan2(2·mean(l·r), mean(l²) − mean(r²))  → asse principale
```

Le medie mobili sono un one-pole a costante di tempo fissa (0.3 s).
Il C++ le porta a mano una per una, citando la fonte Faust — così quando la libreria evolve, il porto non si aggiorna in silenzio: il passaggio umano è anche una revisione.

---

## 4. Lo spettro Welch, e la calibrazione in dBFS (il primo bug)

Faust è un linguaggio **streaming per-campione**: la FFT a blocchi gli è estranea.
Abbiamo scritto quindi una piccola **FFT reale radix-2** riusabile (`_common/seam_fft.h`, adattata dal codice di *the-accountant*) e sopra un accumulatore **Welch** (finestra di Hann, overlap 50%, media esponenziale viva).
Nella spec la ancora Faust idiomatica resta `an.mth_octave_spectral_level` (un banco di filtri SR-indipendente); il C++ sceglie la FFT per una curva più fine.

Al primo test dal vivo è emerso un **bug istruttivo**.
Con una sinusoide che scendeva di 6 dB, il goniometro dimezzava il cerchio e i meter scendevano — corretto — ma il **picco dello spettro restava piantato in cima**, indifferente al livello.

La causa è un classico della stima spettrale.
Il Welch normalizzava con `1 / Σ(w²)`, cioè la calibrazione da **densità spettrale di potenza**, giusta per il rumore ma **sbagliata per i toni**: un tono leggeva circa **+28 dB troppo alto** (per una FFT da 4096 punti).
Sia il fondo-scala sia il −6 dB finivano oltre il +6 dB in cima all'asse, entrambi clampati → sembravano "incollati".

La correzione è la calibrazione a **guadagno coerente**, che fa leggere a una sinusoide a fondo scala esattamente **0 dBFS**:

```
ampiezza_k = 2 · |X_k| / Σw          (Σw = somma della finestra, non dei quadrati)
winNorm    = 4 / (Σw)²               → potenza = |X_k|² · winNorm = ampiezza²
```

Un test lo blinda per sempre: fondo-scala → −0.006 dB, mezza scala → −6.03 dB.
Lezione, degna di memoria: **la calibrazione per densità (Σw²) e quella per toni (Σw) differiscono**, e per un display dove 0 dBFS = fondo scala serve la seconda.
*(Nota per il futuro: i bin DC e Nyquist restano +6 dB alti, perché non hanno il gemello a frequenza negativa da sommare — irrilevante su un asse log che salta il DC, ma da ricordare per la Spec 3.)*

---

## 5. Il confine audio→GUI: il triple-buffer lock-free (il secondo bug, più subdolo)

Il goniometro e lo spettro **nascono nel thread audio** ma **si disegnano nel thread GUI**.
Il vero problema non è l'API di messaggistica, ma il **confine fra i due thread**: come passo un pacchetto di dati senza che l'audio si fermi mai (niente lock, niente allocazioni) e senza che la GUI legga un frame a metà?

Tutta la suite usa `SingleComponentEffect`, quindi processor e controller sono lo **stesso oggetto**: una sola memoria condivisa.
Non serve `IMessage` (che è per componenti separati, in processi diversi): basta un **buffer condiviso lock-free**.

Il mio piano proponeva uno schema *ad-hoc*: il lettore ricorda lo slot, lo scrittore lo "salta".
La review avversariale ha dimostrato — con un'interleaving concreta — che **quello schema può strappare** (*tearing*).
Il lettore registrava lo slot **dopo** la copia; nel divario, lo scrittore poteva sovrascrivere lo slot in lettura.
Un secondo tentativo (rivendica-prima-di-copiare) lasciava ancora un divario *load-then-store* irriducibile.

La soluzione corretta è il **triple-buffer canonico a scambio atomico singolo**.
Un solo `atomic<uint8_t>` impacchetta l'indice del buffer "back" più un bit *dirty*; scrittore e lettore possiedono privatamente un indice ciascuno; la proprietà passa in **una sola `exchange`**, senza divario.

```
process()      : analyze(slots_[writeBuf_]); back_.exchange(writeBuf_ | dirty); writeBuf_ = old
tryReadFrame() : se dirty → back_.exchange(front_); front_ = old; copia slots_[front_]
```

L'invariante che lo rende sicuro: i tre indici `{front_, writeBuf_, back}` restano sempre una **permutazione di {0,1,2}**.
Ogni scambio è una trasposizione di due elementi, quindi la permutazione si conserva: scrittore e lettore **non condividono mai uno slot** → niente tearing, comunque avvenga la preemption.
Lezione salvata in memoria: gli schemi *skip/claim* fatti a mano hanno un divario ineliminabile; per il passaggio bulk audio→GUI si usa lo scambio atomico canonico.

Con tre viste che leggono, un ultimo accorgimento: il `tryReadFrame` è **single-consumer**, quindi lo si incapsula in **un** accessore cache-ato sul processor (`latestFrame()`), e tutte le viste leggono quello.
Altrimenti solo una vista prenderebbe ogni frame e le altre resterebbero a secco.

---

## 6. Il raggio log-dB del goniometro

Con il raggio **lineare**, una sinusoide a −6 dB dimezza il cerchio: gli attenuati diventano subito piccoli e illeggibili.
Su suggerimento di GS siamo passati a un **raggio in scala dB** (fondo −48 dB), preservando la **direzione** di ogni punto e deformando solo la distanza radiale:

```
rLin  = hypot(Sx, My)
rNorm = db2norm(lin2db(rLin, −48), −48)      → [0, 1]
scala = rNorm / rLin                          (isotropa → direzione intatta)
```

Un −6 dB ora sta a ~0.875 del raggio, non a metà; il fondo-scala tocca il bordo; il segnale sparisce al centro solo sotto −48 dB.
Poiché `db2norm` satura a 1, i punti non escono mai dal cerchio, e sparisce anche il vecchio artefatto di "squadratura" ai bordi.
Resta assoluto (non normalizza, a differenza di Melda): la dimensione racconta ancora il livello, solo con una scala più comoda.

---

## 7. La bussola: asse principale contro pan (una scelta di semantica)

Mandando una sinusoide in **quadratura** (Hilbert), il goniometro disegna un cerchio, e qui è emersa una domanda fine sull'**ago** della bussola.
Melda lo dà **frontale** (verticale); il nostro **laterale**.

La ragione è geometrica.
Un cerchio è **rotazionalmente simmetrico**: la sua matrice di covarianza L/R è isotropa, quindi **l'asse principale è indefinito**.
Il nostro ago è l'asse principale (`vectorangle`): su un cerchio perfetto **degenera**, e l'angolo che legge è essenzialmente rumore vicino al punto degenere.
L'ago di Melda invece ricava la direzione dal **bilanciamento L/R** (il pan): L/R uguali → sempre verticale, stabile.

Sono quindi **due bussole diverse**: la nostra racconta l'*orientamento di correlazione* (ricco per segnali correlati), quella di Melda il *pan* (stabile sempre).
GS ha scelto di **tenere l'asse principale**, più informativo per il lavoro M/S, accettando che sul cerchio l'informazione sia labile.
Per renderlo comunque calmo, l'ago si **smorza** con una media circolare sull'**angolo raddoppiato** (`cos 2φ, sin 2φ`): trattandolo come un asse `mod π`, la media non litiga mai con un salto di 180°.

Piccola nota di verità che vale la pena ricordare: l'intuizione "quadratura = ±45°" non ha base robusta, perché correlazione nulla vale sia per la quadratura sia per il rumore decorrelato — li distingue solo la **forma** della nuvola, non l'angolo.

---

## 8. Come è stato costruito

Il percorso ha seguito il metodo del progetto: brainstorming → spec → piano a task → esecuzione.
La spec ha fissato ogni decisione (bus stereo pass-through, sempre M/S, triple-buffer non IMessage, Welch-FFT, tre zone, zero parametri).
Il piano ha decomposto il lavoro in 11 task TDD in ordine **Faust-first**: prima i `san.*`, poi `seam_fft.h`, poi il core `strx_dsp.h`, poi il plugin, poi le tre viste, infine il layout.
Ogni task è passato per una revisione a due verdetti (aderenza alla spec + qualità) prima di essere accettato, e una revisione finale sull'intero ramo ha dato il via libera.

Poi sono arrivati i tuoi test dal vivo, e con loro cinque giri di rifinitura della GUI: la calibrazione dello spettro, il raggio log-dB, il verso L/R, lo smorzamento e il colore azzurro dell'ago, le etichette fuori dal cerchio, il nome **STRX** e il sottotitolo **STEREO M/S Analyser**, i decimali nei meter.
Ogni giro: modifica, build, validator 47/47, deploy, tuo sguardo.

---

## 9. Cosa lascia in eredità

- `_common/seam_fft.h` — FFT reale + Welch calibrato in dBFS, pronto per la **Spec 3** (misura della funzione di trasferimento).
- `san.correlation / width / panorama / vectorangle` — nuovi descrittori Faust, candidabili a upstream.
- Il pattern del **triple-buffer lock-free a scambio atomico** per il passaggio bulk audio→GUI, riusabile da ogni futuro analizzatore/scope.
- Tre lezioni che restano: la calibrazione dei toni (Σw contro Σw²); il tearing degli schemi skip/claim; la degenerazione dell'asse principale sui campi isotropi.

strx è il **primo mattone** del sistema STONE.
La strada prosegue verso il bus di taratura peer-aware (Spec 2), la funzione di trasferimento (Spec 3) e l'auto-EQ (Spec 4).
