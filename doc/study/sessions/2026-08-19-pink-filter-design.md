# Il filtro di pinking di qualità metrologica — progetto e misure

Ripresa del lavoro del 2026-08-18, dopo l'audit di letteratura e il test di
accettazione SMPTE.
Due sessioni si sono spente per mancanza di corrente prima che questo diario
esistesse; il materiale qui è ricostruito dai transcript e rimisurato.

## Le tre decisioni di perimetro

1. **Sostituzione netta.**
Il fit invfreqz a 3 poli sparisce dal codice.
Nessuna modalità legacy, nessun parametro nuovo, nessuna scelta da spiegare
all'utente.
Le misure storiche restano confrontabili leggendo i diari, non riproducendo il
segnale.

2. **La banda controllata è il limite fisico di ogni fs.**
Non i 16 kHz di SMPTE ST 2095-1, ma da 20 Hz alla banda ISO 266 più alta il cui
bordo superiore sta entro 0,85·Nyquist — la stessa regola `bandMeasurable` che
`strx` già applica.
L'argomento è di Giuseppe e vale la pena riportarlo per intero: scegliere 96 o
192 kHz *è già* la dichiarazione di voler guardare sopra i 20 kHz; se la STONE
va in camera anecoica, o se si progetta un amplificatore analogico che curi
tutta la banda passante, il riferimento deve essere buono quanto lo strumento su
tutta l'estensione che lo strumento mostra.
Sotto i 48 kHz il criterio coincide esattamente con SMPTE: la banda dei 20 kHz
ha bordo a 22,39 kHz e non è misurabile comunque, quindi il tetto reale è
16 kHz.
Lo standard non è conservativo — è il massimo ottenibile alle fs che copre.

| fs | bande giudicate | tetto |
|---|---|---|
| 44,1 / 48 kHz | 30 | 16 kHz |
| 88,2 / 96 kHz | 33 | 31,5 kHz |
| 176,4 / 192 kHz | 36 | 63 kHz |

Da correggere un'affermazione fatta in sessione e sbagliata: `strx` **non**
mostra bande fino a 40 kHz.
La sua griglia è fissa a 31 bande, n = −17..13, e si ferma a 20 kHz.
Il tetto odierno è la griglia, non Nyquist.
Estendere la griglia di `strx` sopra i 20 kHz è un perimetro separato, non
incluso qui; il filtro invece si progetta subito largo, perché progettarlo
stretto e allargarlo dopo costerebbe una campagna di misure.

3. **C++ prima, Faust subito dopo, incrociati.**
La convenzione della suite mette Faust come specifica, ma qui i calcoli si fanno
meglio in C++ e la tecnica inversa è già stata usata in passato.
Vincolo esplicito: i due progetti restano collegati e cross-riferiti — il
`FAUST REFERENCE` nell'header C++ e un rimando al plugin e al test nella
libreria Faust.

## Il criterio, e perché il test viene prima del filtro

`tests/multipink_pink_test.cpp`, scritto il 2026-08-18 prima di scegliere un
candidato, porta la risposta del filtro attraverso il banco ISO 266 di
`strx_bands.h` e verifica ±0,25 dB per terzo d'ottava.
È analitico e non a rumore: con ingresso bianco lo spettro di potenza in uscita
**è** |H(f)|², quindi l'energia di banda è un integrale esatto, mentre una
misura su rumore porterebbe ±0,6 dB di dispersione a BT = 50 e non potrebbe
risolvere una tolleranza di 0,25.

Il filtro in produzione fallisce a ogni frequenza, **inclusa quella a cui è
stato fittato**: 0,41 dB a 44,1 kHz, 0,60 a 48, 2,40 a 88,2, 2,68 a 96, 4,99 a
192.
Non è un filtro corretto che si guasta alzando fs; non è mai stato di qualità
metrologica.

Conseguenza già registrata: il candidato A dell'audit (rimappare il fit
esistente via prototipo analogico) non può passare a nessuna frequenza, perché
rimappare riproduce la risposta alla frequenza di progetto e quella è già fuori
tolleranza.

## Errore 1 — densità invece di energia

La prima sonda dava scostamenti di 14–20 dB per qualunque configurazione, e —
segnale d'allarme — **peggiorava aumentando i poli per ottava**, il contrario di
quanto dice la teoria.
La causa era nella sonda, non nel filtro: nell'integrale di banda mancava il
fattore di larghezza (fu − fl), quindi misuravo la densità media di potenza
invece dell'energia.
La densità di un segnale rosa cala di 3 dB/ottava, e su dieci ottave fa 30 dB di
escursione, cioè i ±15 dB osservati.

La lezione è metodologica: l'harness va validato contro numeri già noti prima di
credergli.
Fatto subito dopo — il modello riproduce i cinque valori pinnati nel test C++
(0,41 / 0,60 / 2,40 / 2,68 / 4,99) cifra per cifra.

## La scoperta — `fi.spectral_tilt` non passa, e non è questione di parametri

Con l'harness corretto, `fi.spectral_tilt(N, f0, bw, −0.5)` — la
raccomandazione primaria dell'audit — dà 1,5–2,1 dB a ogni fs.
L'interno della banda è già piattissimo (±0,02 dB con 1,4 poli/ottava): il
ripple non è il problema.
Il problema è il bordo alto: −0,45 dB a 10 kHz, −0,97 a 12,5 kHz, −1,92 a
16 kHz, con fs = 48 kHz.

Nessun parametro lo corregge.
Sono stati provati: guardia inferiore f0 da 10 Hz a 1 Hz, guardia superiore fino
a Nyquist e oltre, densità da 1 a 4 poli/ottava.
Tutte le combinazioni saturano intorno a 0,8–2 dB.

La causa è **la trasformazione bilineare**, ed è aritmetica verificabile: a
16 kHz con fs = 48 kHz il warping vale tan(60°)/(π/3) = 1,654, cioè 0,73 ottave,
che a −3,01 dB/ottava fa −2,19 dB — il −1,9 dB misurato.
Il prewarping di Faust è corretto e mette ogni polo alla sua frequenza digitale
giusta; ma *la curva fra un polo e l'altro* viene valutata alla frequenza
analogica deformata, e la deformazione cresce verso Nyquist.

È un difetto strutturale, non di parametrizzazione.
L'articolo di Smith & Smith (arXiv:1606.06154, verificato sul PDF il
2026-08-19) **tratta** la discretizzazione, in §VII: prescrive la bilineare con
prewarping, `c = 2π f₁ / tan(π f₁ T)` per mappare esattamente il primo polo e
`f̂_k = f₁ tan(π f_k T)/tan(π f₁ T)` per gli altri — cioè esattamente ciò che
Faust implementa.
Quello che l'articolo non fa è esaminare l'errore che quel warping introduce
**nell'interno** della banda: discute i poli all'infinito che finiscono in
z = −1 e il vincolo su N perché f_{N+1} superi Nyquist, non la curva fra un polo
e l'altro.
Matched-Z, invarianza all'impulso o qualunque altra mappatura non compaiono nel
testo.

Nota che spiega il vecchio filtro: il fit invfreqz, pur mediocre, sta a 0,41 dB
fino a 16 kHz a 44,1 kHz proprio perché **è stato fittato direttamente nel piano
z** e ha il warping già dentro i coefficienti.
Compra accuratezza al bordo alto pagandola col bordo basso e con la dipendenza
da fs.

## Matched-Z, e il residuo che è sempre lo stesso

SMPTE ST 2095-1 realizza il proprio roll-off inferiore in matched-Z e solo
quello superiore in bilineare: la strada era indicata dallo standard stesso.

Sostituendo z = e^{sT} (polo analogico −a → polo digitale e^{−aT}, idem per gli
zeri, guadagno unitario a DC per sezione):

- l'interno scende a ±0,05 dB;
- il bordo alto passa da −1,9 dB a **+0,93 dB** — segno opposto, causa diversa:
  matched-Z non deforma l'asse, ma somma le code di aliasing della risposta
  analogica, che a −3 dB/ottava decade troppo lentamente per essere trascurabile;
- e soprattutto la curva d'errore a 48 kHz e a 192 kHz **coincide fino alla
  terza cifra decimale**.

Quest'ultimo fatto è ciò su cui poggia tutto il progetto: l'errore residuo è
funzione della sola frequenza normalizzata f/fs.

| f/fs | 0,03 | 0,09 | 0,117 | 0,197 | 0,256 | 0,333 | 0,43 |
|---|---|---|---|---|---|---|---|
| errore (dB) | +0,006 | +0,057 | +0,097 | +0,280 | +0,478 | +0,821 | +1,429 |

L'andamento è ≈ c·x², e questo separa il problema in due metà ortogonali: la
scala in Hz dipende da fs e la gestisce la scala di poli; la coda di aliasing
dipende solo da f/fs e si corregge **una volta sola, con coefficienti costanti a
ogni sample rate**.

## Errore 2 — l'ottimizzatore impantanato

Il primo fit della correzione dava 0,45 dB con una sezione e — assurdo — 0,60
con due.
Alcuni parametri restavano esattamente al valore iniziale.
Era una discesa per coordinate su un obiettivo minimax, cioè non differenziabile
negli angoli: si ferma al primo punto in cui nessuna singola coordinata migliora,
che non è un minimo.

Anche il tentativo di raffinare iterativamente le posizioni degli zeri contro
l'errore digitale misurato (invece di una correzione separata) si è fermato a
0,50–0,75 dB, per una ragione strutturale che vale la pena registrare: **sopra la
banda giudicata non c'è spazio**.
Da 0,425·fs a Nyquist ci sono 0,23 ottave, e nessun polo può stare più in alto.

Riscritto come Nelder-Mead con 40 partenze casuali, il fit trova subito la
soluzione buona.

## Il progetto

Tre pezzi.

1. **Scala di poli e zeri reali in matched-Z**, spaziati geometricamente da
   f0 = 2 Hz a fs/2, alpha = −1/2 (zero a mezzo passo logaritmico sopra il
   proprio polo).
   Parametri in Hz, quindi corretta per costruzione a ogni fs.
   Cascata di sezioni del primo ordine: è la topologia che sopravvive al
   condizionamento a f/fs ≈ 4·10⁻⁴, dove la direct form di ordine 3 non
   sopravvive.

2. **Una sezione di correzione a coefficienti fissi**, uguale a ogni sample rate:
   zero = −0,250775213, polo = −0,160124183, guadagno unitario a DC.
   Porta il residuo da 1,48 dB a **0,029 dB**.
   Due sezioni arrivano a 0,012 dB, tre non migliorano.

3. **Il test di accettazione**, esteso al criterio scelto.

### Risultato

| poli/ottava | sezioni | peggiore scostamento, tutte le fs | esito |
|---|---|---|---|
| 1,0 | 16–18 | 0,072 – 0,081 dB | PASS |
| 1,5 | 23–26 | 0,029 – 0,036 dB | PASS |
| 2,0 | 29–34 | 0,028 – 0,030 dB | PASS |

A 96 kHz con 1,5 poli/ottava, da 20 Hz a 31,6 kHz, l'errore massimo è 0,032 dB.
Il filtro in produzione, sulla stessa banda, sta a 2,68 dB.
Il margine è tale da soddisfare anche l'ipotesi più stretta di ±0,1 dB che
l'audit indicava come alternativa.

Oltre 1,5 poli/ottava non si guadagna nulla: il pavimento è la qualità del fit
della correzione, non la densità dei poli.

### Livello e calibrazione

Guadagno su rumore bianco, in dB:

| fs | nuovo: RMS | nuovo: a 1 kHz | vecchio: RMS | vecchio: a 1 kHz |
|---|---|---|---|---|
| 44,1 kHz | −31,07 | −27,71 | −21,29 | −17,16 |
| 48 kHz | −31,41 | −27,72 | −21,29 | −16,75 |
| 88,2 kHz | −33,80 | −27,71 | −21,29 | −13,76 |
| 96 kHz | −34,14 | −27,72 | −21,29 | −13,37 |
| 176,4 kHz | −36,54 | −27,72 | −21,29 | −10,90 |
| 192 kHz | −36,88 | −27,72 | −21,29 | −10,59 |

Le due colonne raccontano il difetto in una forma nuova.
Un filtro fittato nel piano z ha forma fissa in frequenza normalizzata: conserva
l'RMS e sposta le frequenze — 6,6 dB di scivolamento a 1 kHz da 44,1 a 192 kHz.
Un filtro ancorato in Hz conserva le frequenze (−27,71 dB a 1 kHz ovunque, entro
0,01 dB) e lascia muovere l'RMS, di 5,8 dB sullo stesso intervallo.

Conseguenza diretta: `kCalibrationOffsetDb = 26.45`, misurato una volta a
48 kHz con un render e `sox`, **deve diventare una funzione di fs**.
(Previsione corretta, e non seguita fino in fondo: il Task 5 l'ha implementata
come un numero fisso. Il seguito è in fondo a questa pagina, alla voce
«sezione 3» di *Aperto*.)
Il motore di progetto può calcolarselo esattamente, perché l'RMS del filtro è
l'integrale di |H|² che il progetto già conosce, e l'RMS della sorgente bianca è
noto in forma chiusa.
La costante attuale include entrambi i contributi (l'LCG uniforme porta 1/√3,
cioè −4,77 dB), ed è questa fusione che l'aveva costretta a essere misurata
invece che calcolata.

## Il costo misurato, e la domanda che il piano non si è fatto

Il piano fissava una regola di decisione su un solo confronto: 1,5 poli per
ottava entra nel 5% di un core a 192 kHz?
La misura (macchina Intel Core i7-8850H) risponde no — 63,7–149,4% a seconda
dell'ordine dei cicli e della statistica — quindi la densità resta 1,0, come
registrato sopra nella tabella dei risultati.

Quello che il piano non ha mai chiesto è se **1,0** poli per ottava, la
densità che resta in uso, stia in quel budget.
Non ci sta.
Misurato nell'ordine dei cicli del plug-in in produzione: **16,5–20,1% di un
core a 48 kHz e 77,8–87,7% a 192 kHz**.
A 192 kHz il solo stadio di pinking, con tutti i 64 stream sempre avanzati,
si mangia quasi un intero core.

Tre leve potrebbero abbassarlo, nessuna delle tre è stata tirata:

1. **Scambiare l'ordine dei cicli.**
   L'ordine B misurato (canale esterno, campione medio, sezione interna)
   corre 2,1–2,3 volte più veloce dell'ordine A in produzione (sezione
   esterna, canale medio, campione interno), a parità di aritmetica e di
   stato — 7,2–8,8% a 48 kHz e 35,3–40,6% a 192 kHz.
   È un cambiamento meccanico, senza conseguenze numeriche.
2. **Filtrare solo le righe CLAIMED del pool, non tutte e 64.**
   La sorgente di rumore deve avanzare tutti e 64 gli stream per restare
   deterministica — il pool è condiviso e peer-aware, e un'altra istanza può
   contare sul fatto che lo stream N esista sempre allo stesso stato — ma i
   filtri per-stream sono indipendenti l'uno dall'altro: uno stream non
   reclamato non deve necessariamente essere filtrato.
   Il guadagno potenziale è 8–32 volte (il numero di canali realmente
   reclamati contro 64), ma va verificato contro la semantica peer-aware del
   pool descritta in `multipink_pool.h` prima di toccare il codice.
3. **SIMD fra stream.**
   Il layout dello stato è già section-major (`state[section][stream]`)
   proprio per permetterlo, come annotato nel codice.
   Non basta da solo: servirebbe anche trasporre il buffer di scratch a
   `[sample][stream]`.

Nessuna delle tre leve è stata applicata in questo piano: la densità di 1,0
resta quella in produzione, con il costo misurato sopra.

## Aperto

- [x] sezione 2 del progetto: stato DSP, layout di memoria, `prepare(fs)` —
      Task 4, cascata section-major in `multipink_processor.h/.cpp`.
- [x] sezione 3: `kCalibrationOffsetDb` calcolato, e convenzione di metering
      (dBFS RMS vs dBFS(AES17), 3,01 dB di differenza) — Task 5,
      `kCalibrationOffsetDb = 36.180`; vedi
      `plugins/multipink/doc/calibration.md`.
      **Esito corretto dopo la stesura di questo diario: la costante non è una
      costante.** Poche righe sopra (§ «Le due colonne raccontano il difetto in
      una forma nuova») questo stesso diario aveva già scritto che l'offset
      «deve diventare una funzione di fs»; il Task 5 l'ha poi implementata come
      un solo numero fisso, perché la sonda che l'ha validata misurava
      l'integrale di banda del *filtro* — che è davvero invariante — e dava per
      assunto il contributo della *sorgente*. La previsione era giusta e la
      conclusione no. La forma corretta è
      `offset(fs) = 36.180 + 10·log10(fs/48000)`: il termine mancante è la
      densità spettrale della sorgente, e costava 3,01 dB per raddoppio di fs.
      Misurato in Reaper attraverso `strx`: livello medio di banda −38,4 dB a
      48 kHz contro −41,3 dB a 96 kHz. Vedi
      `doc/study/sessions/2026-08-19-pink-filter-execution.md` (§ «La stessa
      confusione, due volte») e la sezione Calibration di
      `plugins/multipink/source/multipink_pink.h`.
- [x] costo CPU misurato — Task 6; risultato e le tre leve non tirate sopra.
- [x] la funzione Faust nuova — Task 7, `sfi.spectral_tilt_mz` e
      `sfi.pink_filter_mz` in `seam.filters.lib`. Se proporla a monte in
      GRAME resta una domanda aperta, non affrontata da nessuno dei sette
      task.
- [ ] estensione della griglia di `strx` sopra i 20 kHz — perimetro separato,
      non toccato da questo piano; vedi anche l'apertura equivalente nel
      diario del 2026-08-18.
