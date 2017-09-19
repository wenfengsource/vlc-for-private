#!/bin/sh

OPTIONS="
      --enable-lua
      --enable-faad
      --enable-flac
      --enable-theora
      --enable-twolame
      --enable-quicktime
      --enable-avcodec --enable-merge-ffmpeg
      --enable-live555
      --enable-caca
      --disable-sdl
      --enable-qt
      --enable-sse --enable-mmx
      --enable-libcddb
      --disable-telx
      --disable-upnp 
      --disable-mtp 
      --disable-bonjour 
      --disable-update-check 
      --disable-libxml2 
		--disable-vorbis 
		--disable-speex 
		--disable-opus 
		--disable-theora 
		--disable-schroedinger 
		--disable-x265 
		--disable-x26410b 
		--disable-x264 
		--disable-mfx 
		--disable-zvbi 
		--disable-telx 
		--disable-kate
      --enable-nls"

if gcc -v 2>/dev/null -a echo | gcc -mno-cygwin -E -2>/dev/null 2>&1
then
    echo Cygwin detected, adjusting options
    export CC="gcc -mno-cygwin"
    export CXX="g++ -mno-cygwin"
    OPTIONS="${OPTIONS} --disable-taglib --disable-mkv"
fi

sh "$(dirname $0)"/../../../configure ${OPTIONS} $*
