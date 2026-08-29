# Documentazione: registro dei plugin -> README + sito SEAM.
.PHONY: doc publish test

# Il Makefile vive in doc/, e `make -C doc` esegue con CWD=doc/:
# il percorso relativo si conta da li, non dalla radice del repo.
SITE ?= ../../../blog/s-e-a-m.github.io

doc: ;	@python3 scripts/render-readme.py

publish: doc ; @python3 scripts/publish.py $(SITE)

test: ; @scripts/test-doc.sh
