#!/bin/sh

# Create directory structure
echo Create directory structure
mkdir sdr4p_debian_amd64
mkdir sdr4p_debian_amd64/DEBIAN

# Create package info
echo Create package info
echo Package: sdr4p >> sdr4p_debian_amd64/DEBIAN/control
echo Version: 1.2.1$BUILD_NO >> sdr4p_debian_amd64/DEBIAN/control
echo Maintainer: Bas-W >> sdr4p_debian_amd64/DEBIAN/control
echo Architecture: all >> sdr4p_debian_amd64/DEBIAN/control
echo Description: SDR receiver software >> sdr4p_debian_amd64/DEBIAN/control
echo Depends: $2 >> sdr4p_debian_amd64/DEBIAN/control

# Copying files
ORIG_DIR=$PWD
cd $1
make install DESTDIR=$ORIG_DIR/sdr4p_debian_amd64
cd $ORIG_DIR

# Create package
echo Create package
dpkg-deb --build sdr4p_debian_amd64

# Cleanup
echo Cleanup
rm -rf sdr4p_debian_amd64
