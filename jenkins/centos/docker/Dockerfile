FROM centos:7

# last update: 20160303 10:00
#  -- change previous line to trigger rebuild of docker image on jenkins; eg. when docker-entrypoint.sh changes

RUN echo '%_install_langs C' >> /etc/rpm/macros && \
    yum -y install epel-release && \
    yum -y update && \
    yum -y install sudo wget iproute supervisor \
                   rpm-build git yum-utils rpmdevtools redhat-rpm-config && \
    yum clean all && \
    rm -rf /usr/share/doc/*

# add jenkins user -- this is required for git to work
# note: jenkins has uid 1001 & gid 1002 on our build hosts, this needs to be reflected in here!

ENV UID 1001

RUN groupadd -g 1002 jenkins && \
    useradd jenkins -s /bin/bash -g jenkins -m -u ${UID} -g 1002 -d /home/jenkins && \
    echo "jenkins ALL=NOPASSWD: ALL" >/etc/sudoers.d/jenkins

RUN echo 'BatchMode yes' >>/etc/ssh/ssh_config && \
    echo 'StrictHostKeyChecking no' >>/etc/ssh/ssh_config

# /etc/lsb-release is not present any more;
# systemd provides /etc/os-release but internal names have changed,
# add a hardcoded one for now

RUN echo -e "DISTRIB_ID=CentOS\nDISTRIB_RELEASE=7" > /etc/lsb-release

ADD supervisord.conf /etc/supervisord.conf
ADD docker-entrypoint.sh /sbin/docker-entrypoint.sh

ENTRYPOINT ["/sbin/docker-entrypoint.sh"]

CMD [ "/usr/bin/supervisord" ]
