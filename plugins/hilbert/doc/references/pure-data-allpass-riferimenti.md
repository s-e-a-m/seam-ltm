# Riferimenti al filtro allpass in `pure-data`
Di seguito i punti principali trovati nel repository:

## Esempi patch (`.pd`)
- `doc/3.audio.examples/H14.all.pass.pd`  
  Esempio esplicito di filtro all-pass (catena `rzero_rev~` + `rpole~`).
- `doc/3.audio.examples/H15.phaser.pd`  
  Phaser realizzato con più stadi all-pass (più coppie `rzero_rev~` + `rpole~`).
- `extra/hilbert~.pd`  
  Coppia di filtri all-pass di 4° ordine (implementati con `biquad~`).
- `extra/rev1~-help.pd`  
  Help patch che descrive `rev1~` come serie di filtri all-pass.

## Implementazione oggetti DSP usati per l’allpass
- `src/d_filter.c`  
  Implementazione di:
  - `rpole~` (one-pole reale)
  - `rzero_rev~` (one-zero reale “reverse”)

## Reference utili
- `doc/5.reference/rpole~-help.pd`
- `doc/5.reference/rzero_rev~-help.pd`

## Repo Locale
/Users/giuseppe/Documents/github/pure-data