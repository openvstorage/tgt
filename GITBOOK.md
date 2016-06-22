# TGT

## Building

### Ubuntu 14.04
```
    mkdir WORK
    cd WORK

    ## manually fetch latest libovsvolumedriver + libovsvolumedriver-dev packages here

    git clone https://github.com/openvstorage/tgt

    docker build -t bldenv tgt/jenkins/ubuntu/docker

    docker run -d -v ${PWD}:/home/jenkins/workspace -e UID=${UID} --name builder -h builder bldenv
    docker exec -u jenkins -i -t builder /bin/bash -l

    cd ~/workspace
    ./tgt/jenkins/ubuntu/build_debs.sh

    exit
    docker stop builder && docker rm builder
    docker rmi bldenv
```
Resulting debian packages will be in WORK directory


### CentOS
```

    mkdir WORK
    cd WORK

    git clone https://github.com/openvstorage/tgt

    docker build -t bldenv tgt/jenkins/centos/docker

    docker run -d -v ${PWD}:/home/jenkins/workspace -e UID=${UID} --name builder -h builder bldenv
    docker exec -u jenkins -i -t builder /bin/bash -l

    cd ~/workspace
    ./tgt/jenkins/centos/build_rpms.sh

    exit
    docker stop builder && docker rm builder
    docker rmi bldenv
```

Resulting rpm files will be in WORK/rpm/RPMS/x86_64 and WORK/rpm/SPMS

## Usage
Open vStorage supports exposing vDisks as iSCSI disks through [TGT](http://stgt.sourceforge.net/).

Install the Open vStorage TGT package. For ubuntu
```
sudo apt-get install tgt
```

Create a new target device
```
tgtadm --lld iscsi --mode target --op new --tid=1 --targetname iqn.2016-01.com.openvstorage:for.all
```

Add a logical unit (LUN)
```
tgtadm --lld iscsi --op new --bstype openvstorage --mode logicalunit --tid 1 --lun 1 -b volumeName
```

Enable the target to accept initiators:
* Add IP wildcard to allow all initiators
```
tgtadm --lld iscsi --mode target --op bind --tid 1 -I ALL
```

* IP-based restrictions
    * If you've previously configured a target to accept ALL initiators, you need to remove that first.
```
tgtadm --lld iscsi --mode target --op unbind --tid 1 -I ALL
```
    * Now, restrict access to a specific IP …
```
tgtadm --lld iscsi --mode target --op bind --tid 1 -I 1.2.3.4
```
    * or restrict access to a specific IP subnet  …
```
tgtadm --lld iscsi --mode target --op bind --tid 1 -I 1.2.3.0/24
```