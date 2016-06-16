#!/bin/bash

set -e

SPEC_FILE="scsi-target-utils.spec"

# add openvstorage repo for dependencies of volumedriver packages
echo '[openvstorage]
name=Open vStorage repo
baseurl=http://yum.openvstorage.org/CentOS/7/x86_64/dists/unstable
enabled=1
gpgcheck=0' | sudo tee /etc/yum.repos.d/openvstorage.repo

sudo yum install -y epel-release
sudo yum install -y wget gcc make yum-utils rpm-build rpmdevtools redhat-rpm-config

## if jenkins copied in volumedriver packages install them,
## else use the packages from the openvstorage repo
P=$(ls -d libovsvolumedriver*.rpm 2>/dev/null || true)
if [ -n "$P" ]
then
  sudo yum install -y libovsvolumedriver*.rpm
else
  sudo yum install -y libovsvolumedriver-devel
fi

## prepare the build env
chown -R jenkins:jenkins tgt
mkdir -p ./tgt/rpm/{BUILD,RPMS,SOURCES,SPECS,SRPMS}

## create patch of our sources for rpmbuild

cd tgt/rpm

echo '>>> GET OPENVSTORAGE PATCH FROM GITHUB <<<'
rm -f SOURCES/openvstorage_tgt.patch
wget -q -nc -O SOURCES/openvstorage_tgt.patch https://github.com/openvstorage/tgt/compare/upstream...master.patch

echo '>>> FETCHING UPSTREAM SOURCES <<<'
spectool -g -C SOURCES SPECS/${SPEC_FILE}

echo '>>> INSTALL BUILD DEPENDENCIES <<<'
sudo yum-builddep -y SPECS/${SPEC_FILE}

echo '>>> BUILD RPMS <<<'
rpmbuild -ba --define "_topdir ${PWD}" SPECS/${SPEC_FILE}
