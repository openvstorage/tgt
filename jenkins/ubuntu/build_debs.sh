#!/bin/bash

SRCDIR="$1"

if [ $# -ne 1 ]
then
  echo "Usage: $0 <debianized_source_dir>"
  exit 1
fi

if [ ! -d "${SRCDIR}" ]
then
  echo "Sourcedir ${SRCDIR} not found?! Aborting build..."
  exit 1
fi

set -e

# the OVS apt repo hosts dependencies that are not yet available in Ubuntu
echo "deb http://apt.openvstorage.org unstable main" | sudo tee /etc/apt/sources.list.d/ovsaptrepo.list

echo 'debconf debconf/frontend select Noninteractive' | sudo debconf-set-selections
sudo apt-get update -qq

## we know we depend on the volumedriver-dev package, and we want latest greatest, so install from artifact

for p in volumedriver-base_*_amd64.deb volumedriver-server_*_amd64.deb volumedriver-dev_*_amd64.deb
do
  echo -n "Installing $p..."
  sudo dpkg -i $p &>/dev/null || sudo apt-get install --allow-unauthenticated -qq -y -f >/dev/null
  [ $? -ne 0 ] && echo 'FAILED' || echo 'OK'
done

cd ${SRCDIR}

echo ">>> INSTALLING BUILD DEPENDENCIES <<<"
sudo apt-get install -y $(dpkg-checkbuilddeps 2>&1 | sed -e 's/.*dependencies://' -e 's/ ([^)]*)/ /g')

echo ">>> BUILDING DEBIAN PACKAGES <<<"
dpkg-buildpackage -us -uc -b

## .deb && *.ddeb packages should be available now
