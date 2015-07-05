
### Global definitions

ASSSDIR = ../..

SHELL = /bin/sh
LATEX = latex
DVIPS = dvips
XDVI = xdvi

### Primary targets

DVITARGETS = devguide.dvi
PSTARGETS = devguide.ps
PDFTARGETS = devguide.pdf
HTMLTARGETS = devguide.html
TEXTARGETS = 
TXTTARGETS = devguide.txt
ALLTARGETS = $(DVITARGETS) $(PSTARGETS) $(PDFTARGETS) $(HTMLTARGETS) $(TEXTARGETS) $(TXTTARGETS)

### Dummy targets

default: view

dvi: $(DVITARGETS)

ps: $(PSTARGETS)

pdf: $(PDFTARGETS)

html: $(HTMLTARGETS)

txt: $(TXTTARGETS)

all: dvi ps pdf html txt

view: dvi # ps
	( for a in $(DVITARGETS) ; do $(XDVI) $$a & done ) > /dev/null
	#( for a in $(PSTARGETS) ; do gv $$a & done ) > /dev/null

clean:
	$(RM) -f $(ALLTARGETS) *.log *.aux *.blg *.bak *.hlog *-Z-* *~

publish: pdf html
	scp *.pdf *.html asss:web


### Dependencies and commands

devguide.dvi: devguide.tex
	latex devguide.tex

devguide.ps: devguide.dvi
	$(DVIPS) $(TWOSIDEDSTUFF) -o devguide.ps devguide.dvi

devguide.pdf: devguide.tex
	pdflatex devguide.tex

devguide.html: devguide.tex
	tex2page devguide.tex

devguide.txt: devguide.html
	( echo "(converted from devguide.html with lynx -dump)" ; lynx -dump devguide.html ) | sed -e 's/\s\+$$//' | sed '/Last modified:/q' > $@

