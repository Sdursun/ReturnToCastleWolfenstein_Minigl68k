
dist:
	rm -rf dist
	mkdir dist
	(cd dist && cvs -d `cat ../CVS/Root` checkout rtcw)
	mv dist/rtcw dist/rtcw-source
	find dist/rtcw-source -type d -name "CVS" | xargs rm -rf
	(cd dist && tar -cf rtcw-source.tar rtcw-source/sp rtcw-source/mp rtcw-source/Makefile)
	bzip2 dist/rtcw-source.tar
	(cd dist/rtcw-source/installer && make) || exit 1
	(cd dist/rtcw-source/sp/morphos && make) || exit 1
	(cd dist/rtcw-source/mp/morphos && make) || exit 1
	mkdir dist/RTCW dist/RTCW/main
	cp dist/rtcw-source/installer/rtcw-installer dist/RTCW/Installer
	cp dist/rtcw-source/sp/morphos/objects/rtcw-sp/rtcw-sp dist/RTCW/Single\ Player
	cp dist/rtcw-source/sp/morphos/objects/*/*.so dist/RTCW/main
	cp dist/rtcw-source/mp/morphos/objects/rtcw-mp/rtcw-mp dist/RTCW/Multiplayer
	cp dist/rtcw-source/mp/morphos/objects/*/*.so dist/RTCW/main
	(cd distfiles && tar -c *) | (cd dist && tar -x)
	find dist -name CVS | xargs rm -rf
	(cd dist && /c/lha -r a morphosrtcw.lha RTCW RTCW.info)


.PHONY: dist

