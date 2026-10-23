# SEAM-LTM · Pd externals for the STONE chain
#
#   make            build every external (fat x86_64 + arm64 .d_fat)
#   make install    copy binaries and help patches to INSTALLDIR
#   make smoke      open each tests/smoke-*.pd in a headless Pd, fail on
#                   "couldn't create"
#   make clean
#
# No VST3 SDK and no downloads: the externals include the plugins' DSP
# headers and the installed Pd's m_pd.h.

PDAPP        ?= /Applications/Pd-0.56-2.app
PDINCLUDEDIR ?= $(PDAPP)/Contents/Resources/src
PDBIN        ?= $(PDAPP)/Contents/Resources/bin/pd
INSTALLDIR   ?= $(HOME)/Documents/Pd/externals/seam-ltm

EXTERNALS = m2xhgr~

CXX      ?= c++
ARCHS     = -arch x86_64 -arch arm64
CXXFLAGS  = -std=c++17 -O2 -Wall $(ARCHS) -mmacosx-version-min=11.0 \
            -I$(PDINCLUDEDIR) -I. -I../plugins/_common
LDFLAGS   = $(ARCHS) -mmacosx-version-min=11.0 -bundle -undefined dynamic_lookup

BINARIES = $(addsuffix .d_fat,$(EXTERNALS))

all: $(BINARIES)

m2xhgr~.d_fat: m2xhgr~.cpp m2xhgr_engine.h seam_pd_param.h \
               ../plugins/m2xhgr/source/m2xhgr_dsp.h
	$(CXX) $(CXXFLAGS) -I../plugins/m2xhgr/source $(LDFLAGS) -o $@ $<

install: all
	mkdir -p "$(INSTALLDIR)"
	cp $(BINARIES) $(addsuffix -help.pd,$(EXTERNALS)) "$(INSTALLDIR)"

# perl's alarm stands in for timeout(1), which macOS lacks.
smoke: all
	@for t in tests/smoke-*.pd; do \
	  log=$${t%.pd}.log; \
	  perl -e 'alarm 20; exec @ARGV' -- "$(PDBIN)" -nogui -noprefs -nomidi -noaudio \
	    -stderr -r 48000 -path "$(CURDIR)" -open $$t > $$log 2>&1; \
	  if grep -q "couldn't create" $$log; then echo "FAIL $$t"; cat $$log; exit 1; fi; \
	  echo "ok   $$t"; \
	done

clean:
	rm -f $(BINARIES) tests/*.log

.PHONY: all install smoke clean
