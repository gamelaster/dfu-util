SUBDIRS = src doc

EXTRA_DIST = autogen.sh TODO DEVICES.txt dfuse-pack.py

www/dfu-util.1.html: doc/dfu-util.1
	pandoc -f man -t html -s -o $@ $^
