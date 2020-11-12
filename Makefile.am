SUBDIRS = src doc

EXTRA_DIST = autogen.sh TODO DEVICES.txt dfuse-pack.py

wwwman: www/dfu-util.1.html www/dfu-suffix.1.html www/dfu-prefix.1.html

www/%.1.html : doc/%.1
	pandoc -f man -t html -s -o $@ $^
