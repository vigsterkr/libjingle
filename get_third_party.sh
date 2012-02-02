#!/bin/sh

if [ ! -e talk/third_party/expat-2.0.1 ]; then
	if [ ! -e expat-2.0.1.tar.gz ]; then
		echo "Downloading expact..."
                wget http://sourceforge.net/projects/expat/files/expat/2.0.1/expat-2.0.1.tar.gz/download -O expat-2.0.1.tar.gz
	fi
	echo -n "Extracting expact 2.0.1..."
	tar zxpf expat-2.0.1.tar.gz -C talk/third_party
	echo "done"
fi

if [ ! -e talk/third_party/srtp ]; then
	echo -n "Getting latest srtp..."
	cd talk/third_party
	cvs -d:pserver:anonymous@srtp.cvs.sourceforge.net:/cvsroot/srtp co -P srtp
	echo "done"
	cd ../../
fi

if [ ! -e talk/third_party/gtest ]; then
  if [ ! -e gtest-1.6.0.zip ]; then
    echo "Downloading gtest..."
    wget http://googletest.googlecode.com/files/gtest-1.6.0.zip -O gtest-1.6.0.zip
  fi

  echo -n "Extracting gtest..."
  cd talk/third_party
  unzip -x ../../gtest-1.6.0.zip
  mv gtest-1.6.0 gtest
  echo "done"
  cd ../../
fi
