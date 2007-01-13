default: docs

clean:
	rm -f *.[ch].example
	rm -f fusd.dvi

docs:
	latex fusd
	latex fusd

html:
	latex2html fusd
	rm -f fusd/*.pl
	rm -f fusd/images.*
	rm -f fusd/WARNINGS
	tar czvf fusd-html-docs.tar.gz fusd

pdf: docs
	dvips -t Letter -Ppdf -G0 fusd.dvi
	ps2pdf fusd.ps

