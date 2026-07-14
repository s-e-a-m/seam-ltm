# Log di sessione — STONE + Linkwitz burst + dslar

Data: 2026-07-14
Operatore: Giuseppe Silvi
Modalità: **osservazione** (nessuna misura microfonica di compensazione, per ora)

---

## Setup

### STONE — modalità "musica da camera"
- Altoparlante tetraedrico a diffusione sferica (4 driver, invenzione di GS).
- EQ applicata **sul finale di potenza**: high-shelf con corner molto basso (~100 Hz).
- Scopo: attenuare l'energia alle **medio-alte** frequenze per ottenere una
  risposta **piena anche alle basse**.
- Parametri shelf: corner ~**100 Hz**, gain **−16 dB**, applicato **identico su
  tutti e 4 i canali** dello STONE.
- **Nessun altro filtro di correzione** nella catena (solo lo shelf).

### Catena di livello
- Sorgente: **multipink** (il nostro) a **−23 dB**.
- Finale di potenza: **−9 dB**.
- Lettura al **fonometro**: **85 dB SPL** (A) / **~87 dB SPL** (C), risposta **Slow**,
  range di misura **50–100 dB**.
  - Δ(C−A) ≈ **+2 dB** → indice del contenuto in bassa frequenza presente nel segnale.
- Esito soggettivo: suono **pieno**, pronto per una EQ di fino per linearizzarlo
  al pink noise.

---

## Modello del sistema (ripasso ad alta voce di GS, 2026-07-14)

Cornice concettuale dell'intero sistema di taratura.

- **Allestimento:** uno o più **STONE** in sala, in **posizioni diverse**.
- **Eccitazione per la taratura:** **multipink** su ciascuno STONE → fa suonare
  l'oggetto **scavalcando la natura ambisonica e la catena DSP**, al **massimo
  della sfericità** (pink decorrelato).
- **Unico punto di intervento sul segnale = il FINALE di potenza**, dove risiede
  la **curva di equalizzazione del singolo STONE**, con **modalità selezionabili**:
  - **"Musica da camera":** STONE a **potenza controllata** ("piano"), per avere la
    **gamma più larga possibile** (è il modo del test di oggi: shelf −16 dB @~100 Hz).
  - **"Concerto":** **highpass** che **protegge i piccoli coni da 5"** dal contributo
    a bassa frequenza e ottimizza la resa "da un certo punto in poi":
    **~120 Hz** se **non** c'è LFE in sala, **~160 Hz** se **c'è** LFE.
- **Driver:** coni da **5 pollici** (piccoli → sensibili all'energia in bassa).
- **Scopo della taratura:** correggere le **emissioni energetiche su base pink noise
  sferico**, **in funzione della posizione in sala**. Gli STONE a diffusione sferica
  **parlano molto con l'ambiente** (lo sollecitano); posizioni diverse in un ambiente
  molto **genuino** = **risposte acusticamente diverse**.

> Implicazione per le spec: la correzione è **per-STONE _e_ per-posizione**, non una
> curva unica. Spec 3/4 devono trattare la misura e la EQ come **legate all'istanza
> e alla collocazione**; la EQ correttiva di Spec 4 **si somma / alimenta la curva
> del finale** (che resta il punto fisico di intervento, con i suoi modi camera/concerto).

## Osservazioni

1. In modalità osservazione: NON si sta ancora acquisendo la curva spettrale di
   diffusione con microfono per compensare l'EQ. Fase puramente di ascolto/lettura.

---

## Analisi fenomenologica — LTBURST → m2xhgr → STONE (2026-07-14)

Il burst di Linkwitz **attraversa la catena DSP** (simula la sorgente mono in
ingresso) → fa emergere caratteristiche/criticità della codifica sferica.

### Fatti fisici dello STONE
- **Cassa sigillata**, coni da **5"**.
- Particolarmente adatto a **segnali ricchi di contributi di fase sui 4 canali**
  (4 facce del tetraedro), **meno** adatto a segnali **senza** contributi di fase
  sulle 4 facce (segnale correlato → le 4 facce ricevono lo stesso → monopolo).

### Stato di m2xhgr
- Implementato: **m2xh** = mono → AmbiX di prim'ordine via **Haar** (`haarmn(1) : ro.cross(4)`).
- **Manca la parte G-R:**
  - **G** = gain trim sulle componenti generate.
  - **R** = algoritmica **rotate** (Yaw/Pitch/Roll) inglobata nel plugin.

### Fenomenologia all'ascolto (fedele al disegno iniziale, ma "poco corretta")
- **Alte:** segnale **bilanciato sulla sfera**, con presenza genuina di **A2 (Z)**
  → acuti particolarmente **liberi in aria**.
- **Medie:** il segnale si distribuisce sul **fronte-lato**.
- **Gravi:** diventa **praticamente frontale**, con **dominanza W-X**.

### Perché succede (decodifica dal codice `seam_haar.h`)
La Haar è un banco QMF che spacca il mono in **bande di frequenza** e le assegna
alle 4 componenti AmbiX (ordine ACN dopo `ro.cross(4)`):

| out | segnale | banda | ACN | componente |
|---|---|---|---|---|
| a0 | `x` (passthrough) | **banda larga** | 0 | **W** (omni) |
| a1 | `dif0` (HP) | **alta** | 1 | **Y** (sin/dx) |
| a2 | `dif1` (HP del LP) | **medio-alta** | 2 | **Z** (alto/basso) |
| a3 | `sum1` (LP del LP) | **bassa** | 3 | **X** (fronte/retro) |

Quindi la **mappa frequenza→spazio è strutturale**, non incidentale:
- **Alte** → energia in **Y (laterale) + Z (altezza)** → sfera "libera in aria". ✓
- **Medie** → **Z** che scende + **X** che sale → fronte-lato. ✓
- **Gravi** → **W (omni) + X (fronte)** → frontale, W-X dominante. ✓

### Criticità (doppia penalità sui gravi per lo STONE)
Ai gravi il campo collassa su **W+X** → poca energia direzionale (Y,Z≈0) →
i 4 driver ricevono segnali **poco decorrelati**. Ma è **esattamente il regime
debole** dello STONE (che vuole contributi di fase sulle 4 facce). Sui gravi lo
STONE riceve quindi **due problemi insieme**: bassa frequenza (coni 5" sigillati)
**e** basso contributo di fase tra le facce.
- Nota: in **modalità "concerto"** l'highpass @120/160 Hz **rimuove** proprio questa
  gamma → la criticità morde soprattutto in **"musica da camera"** (full range).

### Tre leve per rimodellare (mapping frequenza→spazio)
1. **Permutazione banda→asse** — oggi fissata da `ro.cross(4)`; quale banda va su
   quale componente (hardcoded).
2. **Gain trim (G)** — per-componente = **peso spaziale per banda di frequenza**.
   È la leva che manca ed è la più diretta per correggere il bilancio udito.
3. **Rotate (R)** — riorienta l'intero campo (broadband): sposta il pattern, non
   lo rimodella per banda.

→ La criticità potrebbe essere **"by-design-incompleto"** più che "by-design-sbagliato":
la manopola di correzione (**G**) non è ancora costruita.

### m2xh = strumento di studio, NON encoder definitivo
- `m2xh` è la fase di studio, non necessariamente l'oggetto finale per l'encoding
  mono→STONE. Possibile arrivare a un **encoder dedicato**.

### TETRAREC — l'origine della ricchezza di fase (tecnica di ripresa di GS)
- Tecnica di ripresa microfonica disegnata da GS per portare **strumenti acustici**
  in STONE. È la **soundfield di Gerzon "esplosa nello spazio"**: i 4 microfoni sono
  allargati **al punto da contenere il musicista** dentro il tetraedro.
- Il musicista suona dentro un **tetraedro di 4 microfoni OMNI** → rilevano
  **differenze di pressione**; nel passaggio **A-format → B-format via `abmodule`**
  emergono anche **differenze di FASE** delle **6 coppie AB** che il tetraedro di
  omni produce (la spaziatura ampia trasforma la direzione in **fase/tempo d'arrivo**).
- È **questa ricchezza di fase** che lo STONE vuole riprodurre.

> **Simmetria record ↔ play:** TETRAREC = Gerzon *esploso* (ripresa: 4 omni spaziati
> → `abmodule` → B-format). STONE = Gerzon *rovesciato* (diffusione: B-format →
> `bamodule` → 4 driver tetraedrici). La catena **mono→STONE** (mono → m2xhgr →
> bamodule → STONE) **non ha** la spaziatura fisica → la fase va **sintetizzata**
> dall'encoder. [[reference_stone_ambisonic_chain]]

### Comportamento voluto (TARGET) dello STONE — GS
1. **Decorrelazione alle 4 facce = ortogonalità di fase controllata.**
   - L'Haar attuale produce X/Y/Z **senza ortogonalità di fase controllata** →
     manca la ricchezza fase di TETRAREC.
   - Requisito (dopo il lavoro Hilbert per UHJ): i contributi di **ampiezza + fase**
     dell'encoder devono "raccontare" la posizione. Per un segnale **frontale**:
     - **W e X in fase** (stessa fase);
     - **Y e Z a 90°** (quadratura).
   - **Come ottenerlo:** rapidamente passando per il nostro **Hilbert**
     (`_common/seam_quadrature.h`, motore dell'x2uhj), **oppure** — quando
     disegneremo i filtri — come **risposta in fase dei filtri stessi** (LR/allpass).
   - → Scioglie la "tensione" grave-sferico vs regime-STONE: la copertura sferica
     nasce dalla **decorrelazione di fase** (quadratura), non dal monopolo coerente.
2. **Direttività per banda:**
   - **Grave** → **sferico** (o **subcardioide**).
   - **Medio** → **frontale** (o **cardioide**).
   - **Acuto** → **cardioide ma verticalizzato** (≈ come ora).

> Rispetto all'attuale è soprattutto il **grave** a invertirsi: da *frontale W-X*
> a *sferico/subcardioide*. Tensione apparente da chiarire: "sferico al grave" =
> radiazione omni ≈ W-dominante ≈ 4 facce correlate = **regime debole** dello STONE;
> ma GS vuole ANCHE decorrelazione alle facce. Probabile risoluzione: **copertura
> sferica ottenuta per decorrelazione** (4 facce sfasate che sommano a un campo
> sferico) invece che per monopolo coerente. Da confermare col meccanismo di (1).

### Perché Haar NON è definitivo — SR-dipendenza (scoperta chiave)
- L'Haar è una QMF a **1 campione di ritardo**: i suoi crossover **scalano con fs**
  (~`fs/4`). A **96 kHz** la banda "alta" (`dif0`→Y) finisce **sopra l'udibile** →
  la sfericità sugli acuti **sparisce/si sposta**. A **48 kHz** "torna normale" →
  **l'Haar è tarato implicitamente per 48 kHz** (ed era così per disegno).
- **Stessa classe di problema** della SR-dipendenza vista in UHJ quadrature
  (`project_uhj_quadrature_fs_dependence`): coefficienti/filtri fissi ai campioni
  non sono fs-indipendenti.

### Direzione futura (DIFFERITA — "al momento dovuto")
- Alla luce del lavoro **biquad per UHJ**, testare la direttività-per-banda voluta
  con **filtri e crossover di Linkwitz-Riley** (progettati **in Hz** → **SR-indipendenti**).
- Naturale casa: `seam.filters.lib` (`project_uhj_roadmap_next`). Tema coerente con
  tutta la linea Linkwitz (ltburst/ltglide).
- Nota: questo riguarda l'**encoder mono→STONE**, distinto dal sistema di taratura
  (Spec 1–4) — ma è il **burst di taratura** che l'ha fatto emergere.

> **Thread "modi di propagazione STONE" CHIUSO** (2026-07-14). Salvato in memoria:
> `project_tetrarec_stone_phase_encoder`, `project_haar_sr_dependence`.
> Prossimo: prove con **dslar** + il neonato **discipiano** (sds).

## Idee emerse

### Plugin ricevitore microfonico per taratura STONE (verso un sistema auto-calibrante)
Emersa il 2026-07-14 durante l'osservazione.
- Obiettivo: **taratura automatica** degli STONE.
- Forma: **plugin ricevitore microfonico** (input mono o stereo, analisi in **mid-side**).
- **Peer-aware**: si aggancia allo **slot multipink** e **individua quale slot sta
  suonando** (riusa il pattern `multipink_pool` — cross-instance state).
- Funzione: **tracciare la funzione di trasferimento spettrale rilevata**
  (segnale noto emesso vs segnale captato dal microfono).
- GUI: valutare l'oggetto **2D curve-plot dell'SDK VSTGUI** visto nell'ultima
  sessione per disegnare le curve di trasferimento.
- Collegamenti roadmap esistente:
  - `project_ltglide_receiver_calibration` (ricevitore peer-aware, riferimento
    deterministico, Δt via Dirac, media dei passaggi, EQ ≤8 bande auto).
  - `project_seam_ltm_metering_system` (sottosistema metering, EBU R128 differito).
  - `reference_stone_ambisonic_chain` (catena mono→STONE, taratura come parte
    integrante).
- **Riferimento = entrambe le sorgenti (estensibile)**:
  - **multipink decorrelato** → fa suonare lo STONE **completamente sferico**
    (misura la diffusione a banda larga, medie spettrali).
  - **mono burst Linkwitz** → passa per **encoder m2x** + **conversione bamodule**
    → calibra **tutta la catena** (non solo il diffusore).
  - Progettare per **aggiungere nel tempo altri sistemi di taratura** →
    astrazione "sorgente di misura" pluggable.
- Nota GUI: l'oggetto **2D** citato è emerso durante **l'indagine sui meter per
  dslar**; nella SDK non esiste un plot pronto → primitiva = **`CGraphicsPath`**
  (`vstgui/lib/cgraphicspath.h`), curve disegnate in un `CView` custom.
- **Direzione scelta (approccio A, peer-aware deterministico):**
  - Ancora di sincronizzazione = `ProcessContext::continuousTimeSamples` (clock
    campione globale condiviso da tutte le istanze) → la latenza residua acustica+
    converter diventa un singolo **Δt** da stimare per cross-correlazione.
  - **Sorgente pink**: riferimento = **spettro analitico** (−3 dB/ott). Nessuna
    rigenerazione campioni; basta il metadato "pink a livello L attivo". Misura di
    **solo modulo** (la EQ di fino).
  - **Sorgente burst Linkwitz**: **rigenerazione esatta** della forma d'onda +
    **Dirac** (già emessi da ltglide) per il Δt → misura **modulo + fase/tempo**
    di tutta la catena m2x→bamodule→STONE.
  - Stato condiviso = estensione di `multipink_pool` da bitmap a **bus di
    taratura**: pubblica per ogni emettitore attivo `{tipo, slot, livello,
    startSample, parametri}`. Il ricevitore lo legge → "individua lo slot che suona".
- **Lato microfono = analisi M/S sempre attiva (1 o 2 ingressi):**
  - **1 microfono** → solo **Mid** (S = 0). L'analisi del Mid funziona lo stesso.
  - **2 microfoni** → **Mid + Side**. Il Side cattura il contributo della **stanza**
    alla sfericità dello STONE: un colpo d'occhio al bilanciamento M/S dice subito
    se quello STONE specifico, pur sferico, "risulti in aria" attraverso l'ambiente
    più o meno aperto.
  - Il M/S non è solo stereo: è una **sonda della decorrelazione spaziale**
    (pink decorrelato → molta energia in Side; se Side collassa, la sfericità non
    sta arrivando all'ascoltatore).
- **Riferimento GUI = analizzatore stereo tipo Melda MStereoScope** (screenshot:
  `ref-melda-mstereoscope.png`). Elementi da cui pescare:
  - **Goniometro / vettorscopio** (scatter L/R Lissajous in un cerchio, assi L·R,
    angolo + panorama).
  - Meter verticali: **In (L/R)**, **True (peak)**, **M**, **S**, e barra **Width**
    (scala mono ↔ 100% ↔ inv, con soglie).
  - Toggle **SCOPE / STEREO GRAPH**.
  - → il nostro plot 2D delle curve di trasferimento convive con questo mondo
    (goniometro + meter M/S/Width + curva spettrale), tutto su `CGraphicsPath`.
- Stato: **direzione approvata da GS** (2026-07-14). Brainstorming in corso su
  scope / fasatura.

---

## Mappa delle spec — sistema di auto-taratura STONE

> Metodo concordato (2026-07-14): **qui** si definiscono solo gli **ambiti** di
> ciascuna spec (confini, dipendenze, ordine). Le spec vere **non** si scrivono
> ora — altri requisiti emergeranno dal test. A fine test si rivede ogni ambito
> uno per uno e si fissa.

Nome di lavoro del ricevitore: **`strx`** *(stone receiver — provvisorio, da
confermare; alternative: `msrx`, `ltcal`, `stonecal`)*.

### Spec 1 — Analizzatore M/S di osservazione (il ricevitore, fase 1)
- **Cosa fa:** plugin analizzatore autonomo. Ingresso **1–2 canali** (microfono),
  internamente **sempre M/S**. Nessun riferimento, nessun peer-aware, nessuna EQ.
- **Mostra:**
  - **Goniometro / vettorscopio** (Lissajous L/R, cerchio, angolo/panorama) — CView custom.
  - Meter **In (L/R)**, **True peak**, **M**, **S**, barra **Width** (mono↔100%↔inv).
  - **Curva spettrale** del segnale captato (media a lungo termine, Welch) — il
    "coso 2D" su `CGraphicsPath`.
- **Riuso:** i meter di livello poggiano su `_common/seam_meter.h` (già usato da
  dslar); goniometro + curva spettrale = nuove `CView`.
- **Fuori ambito:** funzione di trasferimento, EQ, bus peer-aware.
- **Valore:** utile **da solo** e **subito** — è lo strumento della modalità
  osservazione di oggi.
- **Dipendenze:** nessuna (a parte seam_meter).

### Spec 2 — Bus di taratura peer-aware (infrastruttura condivisa)
- **Cosa fa:** stato statico condiviso nel modulo `.vst3` — evoluzione di
  `multipink_pool` da bitmap di occupazione a **bus di taratura**. Ogni emettitore
  attivo **pubblica** `{tipo sorgente, slot, livello, startSample, parametri}`;
  il ricevitore **sottoscrive**.
- **Tocca i plugin esistenti:** modifiche minime a **multipink** e **ltglide/
  ltburst** perché pubblichino cosa stanno suonando.
- **Convenzione di sincronizzazione:** ancora = `ProcessContext::continuousTimeSamples`
  (clock campione globale condiviso).
- **Fuori ambito:** la matematica della TF (Spec 3), la EQ (Spec 4).
- **Dipendenze:** nessuna; è dipendenza *di* Spec 3.

### Spec 3 — Misura della funzione di trasferimento (upgrade del ricevitore)
- **Cosa fa:** con il bus, calcola `H = Y/X`.
  - **Sorgente pink:** confronto spettro medio captato ↔ **riferimento analitico**
    pink (−3 dB/ott) → **TF di modulo**.
  - **Sorgente burst/glide:** **rigenera** il riferimento deterministico, usa i
    **Dirac** per bloccare il **Δt**, ricava **modulo + fase / IR** di tutta la
    catena m2x→bamodule→STONE.
- **Mostra:** curva TF misurata vs target, lettura Δt, (eventuale) coerenza.
- **Fuori ambito:** sintesi/applicazione EQ.
- **Dipendenze:** Spec 1 (GUI/analisi) + Spec 2 (bus).

### Spec 4 — Auto-taratura / sintesi EQ
- **Cosa fa:** dalla `H` misurata sintetizza una EQ correttiva **≤8 bande**.
  - **Step 1:** *descrive/raccomanda* la correzione.
  - **Step 2:** la *auto-applica*.
- **Collegamenti roadmap:** ltglide Phase 3b-ii; `reference_stone_ambisonic_chain`.
- **Grande nodo aperto:** **dove** vive la EQ correttiva — sul finale (come lo
  shelf "musica da camera" attuale) o come stadio del plugin/catena? Da decidere.
- **Dipendenze:** Spec 3.

### Ordine e razionale
1 → (2 → 3) → 4. La **Spec 1** parte subito e sta in piedi da sola; **2+3**
la promuovono a misuratore di trasferimento; **4** chiude il cerchio
dell'auto-calibrazione. Ogni pezzo successivo si aggancia pulito senza riscrivere
il precedente.

### Nodi aperti che il TEST può sciogliere (da riportare qui man mano)
- Nome definitivo del ricevitore.
- Δt reali della catena (acustica + converter) → dimensionano finestre/buffer di Spec 3.
- Comportamento M/S osservato nella stanza → conferma l'utilità dei descrittori di Spec 1.
- Serve compensare la **TF del microfono stesso** (calibrazione del mic)? → possibile
  requisito trasversale a Spec 1/3.
- Dove applicare la EQ correttiva (Spec 4).

---

## Prove dslar (post-ripristino build)

- **Fix build:** symlink VST3 rotto (puntava a `/tmp/dslar-gui-build`, svuotato al
  riavvio) → re-configure `build-release` (Xcode) + rebuild target dslar → symlink
  ora stabile dentro il repo. Ricaricato in Reaper: **funziona**.

### Chiarimenti parametri (dal codice `dslar_dsp.h`)
Architettura LAR (Di Scipio): homeostat **feedforward**, loop di Larsen **acustico
esterno**. Due rami: **audio** (`hip100·drive→delay(tab1)`) e **analisi**
(`delay(tab2)→HannRMS→|r−ref|^k→smooth`), con `g` che moltiplica l'audio.

- **Decorrelation (tab2, 1–200 ms):** ritardo sul **solo ramo di analisi**
  (`delayAnalysis_`), non sull'audio. Decorrela la *misura* dall'*emissione*.
  A orecchio cambia poco perché `g` è super-lisciato (Hann RMS ~46 ms + line ~200 ms):
  su materiale quasi-stazionario la misura spostata dà RMS ~uguale → `g` ~uguale.
  Effetto vero = **strutturale**: termine di fase/memoria nel sistema dinamico
  stanza+processore → cambia il **carattere delle auto-oscillazioni**, visibile
  nell'evoluzione temporale del feedback, non come scatto timbrico.
  → **manopola di dinamica, non di timbro.**
- **Steepness (k, 1–80, def 40):** **NON è in ms** → è l'**esponente** adimensionale
  di `g = |r−ref|^k` (`dslar_dsp.h:219`). Unità volutamente vuota
  (`dslar_processor.cpp:46`). Alto k = ecosistema nervoso/selettivo (soglia netta);
  basso k = morbido/continuo.

### GUI dslar — FATTO (2026-07-14, build-release, validator 47/47)
- **Steepness → "Steepness (^k)"** (solo label uidesc; convenzione come i "(ms)").
- **Bottone-reset temporizzato** accanto a Power:
  - Custom `CView` `DslarResetButton` (`source/dslar_reset_button.h`), primo custom
    view di dslar; wired via `VST3EditorDelegate::createCustomView` (pattern x2uhj).
  - **UI-only, nessun parametro VST3** → evita il bug momentary/coalescenza.
  - Al click → **azzurro**; rampa i **6 param di carattere** (Drive, Target,
    Steepness, Control smoothing, Loop delay, Decorrelation) ai default Di Scipio
    su **~1000 ms** (`kRampMs`) via `CVSTGUITimer` (tick 20 ms) + begin/perform/endEdit;
    resta azzurro per tutta la rampa, si svuota a fine. **Power/Output intatti.**
  - Costanti ritoccabili: `kRampMs` (durata), `kTickMs` (grana).
- **VERIFICATO DAL VIVO (GS, 2026-07-14):** reset funziona "perfettamente" (azzurro
  al click, rampa dei parametri).
- **Rifinitura GUI VERIFICATA (GS):** allineamento a due colonne (Power↔AUDIO,
  Reset↔ANALYSIS), simbolo a sinistra + label maiuscola a destra, quadratini
  della stessa dimensione (box 12px con geometria `CCheckBox` replicata). OK.
- Stato: **completo e verificato**. Da committare quando GS vuole (branch `dslar`).

### Spunto GUI originale (ora risolto)
- Steepness senza unità ha tratto in inganno GS (pensava ms). È corretto (adimensionale),
  ma si potrebbe mostrare `^k` o tooltip "exponent 1–80" per distinguerlo dai
  parametri in ms a colpo d'occhio. Piccolo, collegabile al lavoro custom-CView/
  transfer-curve già in roadmap (`project_dslar_plugin_status`).

## Punti aperti / da chiarire
- [x] Parametri shelf: ~100 Hz, −16 dB, uguale sui 4 canali; nessun altro filtro.
      (Resta da confermare: Q/slope dello shelf.)
- [x] Lettura fonometro chiarita: 85 dB SPL, curva A, Slow, range 50–100 dB.
- [ ] Sample rate della catena di test.
- [ ] Punto di misura del fonometro (distanza / asse rispetto allo STONE).
