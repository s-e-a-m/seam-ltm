DOC = sscdo2-porting
TEX = $(wildcard *.tex)

.PHONY: all check clean
all: $(DOC).pdf

$(DOC).pdf: $(TEX)
	latexmk -lualatex -interaction=nonstopmode -halt-on-error $(DOC).tex

check: $(DOC).pdf
	@! grep -E "Missing character|LaTeX Warning: (Reference|There were undefined)" $(DOC).log || (echo "BUILD LOG PROBLEMS"; exit 1)
	python3 check.py --selftest
	python3 check.py

clean:
	latexmk -C $(DOC).tex
