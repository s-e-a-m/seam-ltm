DOC = dslar-study

.PHONY: all clean
all: $(DOC).pdf

$(DOC).pdf: $(DOC).tex refs.bib
	latexmk -pdf -interaction=nonstopmode $(DOC).tex

clean:
	latexmk -C $(DOC).tex
