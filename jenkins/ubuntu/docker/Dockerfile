FROM ubuntu:14.04

# last update: 20151203
#  -- change previous line to trigger rebuild of docker image on jenkins; eg. when docker-entrypoint.sh changes

# add jenkins user -- this is required for git to work
# note: jenkins has uid 1001 & gid 1002 on our build hosts, this needs to be reflected in here!

ENV UID 1001

RUN groupadd -g 1002 jenkins && \
    useradd jenkins -s /bin/bash -g jenkins -m -u ${UID} -g 1002 -d /home/jenkins && \
    echo "jenkins ALL=NOPASSWD: ALL" >/etc/sudoers.d/jenkins

# add required packages

ENV DEBIAN_FRONTEND noninteractive

RUN apt-get update && \
    apt-get upgrade -y && \
    apt-get dist-upgrade -y && \
    apt-get install -y software-properties-common && \
    add-apt-repository -y ppa:ubuntu-toolchain-r/test && \
    add-apt-repository -y ppa:afrank/boost && \
    apt-get update && \
    apt-get install -y gcc-4.9 g++-4.9 libstdc++-4.9-dev \
                       libboost1.57-all-dev \
                       build-essential \
                       libc6-dbg pkg-create-dbgsym elfutils \
                       wget git supervisor rpcbind && \
    apt-get -y autoremove --purge && apt-get clean && \
    update-alternatives --install /usr/bin/gcc gcc /usr/bin/gcc-4.9 99 && \
    update-alternatives --install /usr/bin/g++ g++ /usr/bin/g++-4.9 99

RUN echo 'BatchMode yes' >>/etc/ssh/ssh_config && \
    echo 'StrictHostKeyChecking no' >>/etc/ssh/ssh_config

RUN usermod -a -G sudo jenkins && \
    usermod -a -G adm  jenkins

ADD supervisord.conf /etc/supervisor/supervisord.conf
ADD docker-entrypoint.sh /sbin/docker-entrypoint.sh

ENTRYPOINT ["/sbin/docker-entrypoint.sh"]

CMD [ "/usr/bin/supervisord" ]
