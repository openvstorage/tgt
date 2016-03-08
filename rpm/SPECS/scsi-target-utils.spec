%global oname tgt

%ifnarch s390 s390x
%global with_rdma 1
%endif

# Disable rbd on epel7 b/c deps are not present
%{!?rhel:%global with_rbd 1}

## %ifnarch %{power64} aarch64
## %global with_glfs 1
## %endif

%global with_glfs 0

Name:           scsi-target-utils-openvstorage
## Version:        1.0.55
Version:        1.0.43
Release:        4%{?dist}
Summary:        The SCSI target daemon and utility programs

Group:          System Environment/Daemons
License:        GPLv2
URL:            http://stgt.sourceforge.net/
Source0:        https://github.com/fujita/tgt/archive/v%{version}.tar.gz
Source1:        tgtd.service
Source2:        sysconfig.tgtd
Source3:        targets.conf
Source4:        sample.conf
Source5:        tgtd.conf
Patch0:         0001-redhatify-docs.patch
Patch1:         0002-remove-check-for-xsltproc.patch
Patch2:         0003-default-config.patch

Patch9999:      openvstorage_tgt.patch

BuildRoot:      %{_tmppath}/%{name}-%{version}-%{release}-root-%(%{__id_u} -n)

BuildRequires:  pkgconfig libxslt docbook-style-xsl systemd-units libaio-devel systemd-devel
%if 0%{?with_rdma}
BuildRequires:  libibverbs-devel librdmacm-devel
Requires:       libibverbs librdmacm
%endif
Requires: lsof sg3_utils
Requires(post): systemd-units

%description
The SCSI target package contains the daemon and tools to setup a SCSI targets.
Currently, software iSCSI targets are supported.


%if 0%{?with_rbd}
%package rbd
Summary:        Support for the Ceph rbd backstore to scsi-target-utils
Group:          System Environment/Daemons
BuildRequires: ceph-devel
Requires: %{name}%{?_isa} = %{version}-%{release}

%description rbd
Adds support for the Ceph rbd backstore to scsi-target-utils.
%endif


%if 0%{?with_glfs}
%package gluster
Summary:        Support for the Gluster backstore to scsi-target-utils
Group:          System Environment/Daemons
BuildRequires: glusterfs-api-devel
Requires: %{name}%{?_isa} = %{version}-%{release}

%description gluster
Adds support for the Gluster glfs backstore to scsi-target-utils.
%endif


%prep
%setup -q -n %{oname}-%{version}
%patch0 -p1
%patch1 -p1
%patch2 -p1

%patch9999 -p1
## git init
## git apply -v --whitespace=nowarn --exclude=rpm/* --exclude=debian/* --exclude=jenkins/* %{PATCH9999}

%build
%{__sed} -i -e 's|-g -O2 -Wall|%{optflags}|' Makefile
%{__make} %{?_smp_mflags} %{?with_rdma:ISCSI_RDMA=1} %{?with_rbd:CEPH_RBD=1} %{?with_glfs:GLFS_BD=1} SD_NOTIFY=1 libdir=%{_libdir}/tgt


%install
%{__rm} -rf %{buildroot}
%{__install} -d %{buildroot}%{_sbindir}
%{__install} -d %{buildroot}%{_mandir}/man5
%{__install} -d %{buildroot}%{_mandir}/man8
%{__install} -d %{buildroot}%{_unitdir}
%{__install} -d %{buildroot}%{_sysconfdir}/tgt
%{__install} -d %{buildroot}%{_sysconfdir}/tgt/conf.d
%{__install} -d %{buildroot}%{_sysconfdir}/sysconfig

%{__install} -p -m 0755 scripts/tgt-setup-lun %{buildroot}%{_sbindir}
%{__install} -p -m 0755 %{SOURCE1} %{buildroot}%{_unitdir}
%{__install} -p -m 0755 scripts/tgt-admin %{buildroot}/%{_sbindir}/tgt-admin
%{__install} -p -m 0644 doc/manpages/targets.conf.5 %{buildroot}/%{_mandir}/man5
%{__install} -p -m 0644 doc/manpages/tgtadm.8 %{buildroot}/%{_mandir}/man8
%{__install} -p -m 0644 doc/manpages/tgt-admin.8 %{buildroot}/%{_mandir}/man8
%{__install} -p -m 0644 doc/manpages/tgt-setup-lun.8 %{buildroot}/%{_mandir}/man8
%{__install} -p -m 0600 %{SOURCE2} %{buildroot}%{_sysconfdir}/sysconfig/tgtd
%{__install} -p -m 0600 %{SOURCE3} %{buildroot}%{_sysconfdir}/tgt
%{__install} -p -m 0600 %{SOURCE4} %{buildroot}%{_sysconfdir}/tgt/conf.d
%{__install} -p -m 0600 %{SOURCE5} %{buildroot}%{_sysconfdir}/tgt

pushd usr
%{__make} install %{?with_rdma:ISCSI_RDMA=1} %{?with_rbd:CEPH_RBD=1} %{?with_glfs:GLFS_BD=1} SD_NOTIFY=1 DESTDIR=%{buildroot} sbindir=%{_sbindir} libdir=%{_libdir}/tgt

%post
%systemd_post tgtd.service

%preun
%systemd_preun tgtd.service

%postun
# don't restart daemon on upgrade
%systemd_postun

%clean
%{__rm} -rf %{buildroot}


%files
%doc README doc/README.iscsi doc/README.iser doc/README.lu_configuration doc/README.mmc doc/README.ssc
%{_sbindir}/tgtd
%{_sbindir}/tgtadm
%{_sbindir}/tgt-setup-lun
%{_sbindir}/tgt-admin
%{_sbindir}/tgtimg
%{_mandir}/man5/*
%{_mandir}/man8/*
%{_unitdir}/tgtd.service
%attr(0600,root,root) %config(noreplace) %{_sysconfdir}/sysconfig/tgtd
%attr(0600,root,root) %config(noreplace) %{_sysconfdir}/tgt/targets.conf
%attr(0600,root,root) %config(noreplace) %{_sysconfdir}/tgt/tgtd.conf
%attr(0600,root,root) %config(noreplace) %{_sysconfdir}/tgt/conf.d/sample.conf

%{_libdir}/tgt/backing-store/bs_openvstorage.so

%if 0%{?with_rbd}
%files rbd
%{_libdir}/tgt/backing-store/bs_rbd.so
%doc doc/README.rbd
%endif

%if 0%{?with_glfs}
%files gluster
%{_libdir}/tgt/backing-store/bs_glfs.so
%doc doc/README.glfs
%endif

%changelog
* Tue Mar 8 2016 openvstorage - [1.0.43 based]
- openvstorage patch added
- reverted back to 1.0.43 as our code is on that level

* Tue Jan 12 2016 Andy Grover <agrover@redhat.com> - 1.0.55-4
- Rebuild for ppc64le

* Tue Dec 1 2015 Andy Grover <agrover@redhat.com> - 1.0.55-3
- Disable glfs for power and aarch64, fixes #1209472

* Mon Feb 16 2015 Andy Grover <agrover@redhat.com> - 1.0.55-2
- Fix for #1193043, make BuildDep on systemd-devel unconditional

* Tue Feb 10 2015 Andy Grover <agrover@redhat.com> - 1.0.55-1
- New upstream version

* Mon Aug 18 2014 Fedora Release Engineering <rel-eng@lists.fedoraproject.org> - 1.0.48-3
- Rebuilt for https://fedoraproject.org/wiki/Fedora_21_22_Mass_Rebuild

* Sun Jun 08 2014 Fedora Release Engineering <rel-eng@lists.fedoraproject.org> - 1.0.48-2
- Rebuilt for https://fedoraproject.org/wiki/Fedora_21_Mass_Rebuild

* Mon Jun 2 2014 Andy Grover <agrover@redhat.com> - 1.0.48-1
- New upstream version
- Add systemd sd_notify support

* Thu Apr 17 2014 Andy Grover <agrover@redhat.com> - 1.0.46-3
- Add switches to easily build w/o fancy backstores

* Fri Apr 4 2014 Andy Grover <agrover@redhat.com> - 1.0.46-1
- New upstream version
- Add support for gluster backend
- Use systemd scriptlets

* Mon Feb 3 2014 Andy Grover <agrover@redhat.com> - 1.0.44-1
- New upstream version

* Mon Dec 2 2013 Andy Grover <agrover@redhat.com> - 1.0.42-1
- New upstream version

* Fri Nov 1 2013 Andy Grover <agrover@redhat.com> - 1.0.41-1
- New upstream version
- Remove patches:
  * fix-no-module-build.patch
  * usr-Makefile-fix-typo-in-bs_aio-so-filename.patch
- Disable aio in a subpackage

* Fri Oct 4 2013 Andy Grover <agrover@redhat.com> - 1.0.40-1
- New upstream version
- Break out Ceph (bs_rbd) support into a subpackage
- Repackage patches based on git
- Add patches:
  * fix-no-module-build.patch
  * usr-Makefile-fix-typo-in-bs_aio-so-filename.patch
- Fix some weird date issues in changelog
- Enable aio in a subpackage
- Remove defattrs from file sections

* Tue Sep 3 2013 Andy Grover <agrover@redhat.com> - 1.0.39-1
- New upstream version
- Move with_rbd outside ifnarch, and add comment

* Sat Aug 03 2013 Petr Pisar <ppisar@redhat.com> - 1.0.38-2
- Perl 5.18 rebuild

* Wed Jul 31  2013 Andy Grover <agrover@redhat.com> - 1.0.38-1
- New upstream version

* Wed Jul 17 2013 Petr Pisar <ppisar@redhat.com> - 1.0.32-6
- Perl 5.18 rebuild

* Wed Apr 10 2013 Andy Grover <agrover@redhat.com> - 1.0.32-5
- Fix ExecStartPost line in tgtd.service to have full path

* Mon Mar 25 2013 Andy Grover <agrover@redhat.com> - 1.0.32-4
- Add workaround fo BZ 848942.

* Thu Feb 14 2013 Fedora Release Engineering <rel-eng@lists.fedoraproject.org> - 1.0.32-3
- Rebuilt for https://fedoraproject.org/wiki/Fedora_19_Mass_Rebuild


* Wed Nov  7 2012 Pádraig Brady <P@draigBrady.com> - 1.0.32-2
- Fix `tgt-admin --update` so that it uses /etc/tgt/tgtd.conf

* Mon Oct 15 2012 Andy Grover <agrover@redhat> - 1.0.32-1
- New upstream version
- Change .spec for new download.tgz format

* Thu Sep 27 2012 Pádraig Brady <P@draigBrady.com> - 1.0.30-4
- Allow config files to include (all files from) directories
- Support conf.d/ on upgrade even with modified targets.conf

* Tue Aug 7 2012 Andy Grover <agrover@redhat.com> - 1.0.30-3
- Add /etc/tgt/conf.d directory, and move samples from targets.conf
  into conf.d/samples.conf. See #643302, thanks ssato@redhat.com.

* Mon Aug 6 2012 Andy Grover <agrover@redhat.com> - 1.0.30-2
- Fix typo in service file

* Thu Aug 2 2012 Andy Grover <agrover@redhat.com> - 1.0.30-1
- New upstream release
- Refresh patches
- Drop separate package for iser

* Sat Jul 21 2012 Fedora Release Engineering <rel-eng@lists.fedoraproject.org> - 1.0.24-3
- Rebuilt for https://fedoraproject.org/wiki/Fedora_18_Mass_Rebuild

* Wed Feb 1 2012 Andy Grover <agrover@redhat.com> - 1.0.24-1
- New upstream release

* Tue Jan 3 2012 Andy Grover <agrover@redhat.com> - 1.0.23-1
- New upstream release

* Tue Dec 20 2011 Dan Horák <dan[at]danny.cz> - 1.0.22-4
- fix build on s390(x) by introducing new conditional

* Thu Dec 15 2011 Andy Grover <agrover@redhat.com> - 1.0.22-3
- Fix leftover fcoe references

* Thu Dec 15 2011 Andy Grover <agrover@redhat.com> - 1.0.22-2
- Convert to systemd init (based on fcoe-utils.spec)
- Correct arch excludes for subpackage only

* Sat Dec 3 2011 Andy Grover <agrover@redhat.com> - 1.0.22-1
- New upstream release
- Remove patch scsi-target-utils-hack-check-for-eventfd.patch,
    upstream now checks for eventfd support
- Refresh scsi-target-utils-dynamic-link-iser.patch
- Refresh scsi-target-utils-remove-xsltproc-check.patch

* Thu Nov 3 2011 Andy Grover <agrover@redhat.com> - 1.0.21-5
- iser subpackage requires base package

* Thu Nov 3 2011 Andy Grover <agrover@redhat.com> - 1.0.21-4
- Break out iser into a separate subpackage

* Wed Nov 2 2011 Andy Grover <agrover@redhat.com> - 1.0.21-3
- Use local stylesheets for manpage generation

* Wed Nov 2 2011 Andy Grover <agrover@redhat.com> - 1.0.21-2
- Include targets.conf.5 manpage in pkg
- Add libxslt as buildrequire for xsltproc (manpage generation)

* Wed Nov 2 2011 Andy Grover <agrover@redhat.com> - 1.0.21-1
- New upstream version
  - Refresh dynamic-link-iser.patch
  - Remove upstreamed fix-segfault-on-exit.patch

* Tue Oct 25 2011 Andy Grover <agrover@redhat.com> - 1.0.20-2
- Add missing requires lsof and sg3_utils

* Tue Oct 4 2011 Andy Grover <agrover@redhat.com> - 1.0.20-1
- New upstream version
  * New file location on Github
  * Update dynamic-link-iser.patch
- ISCSI=1 make param no longer needed, removed

* Thu Sep 29 2011 Andy Grover <agrover@redhat.com> - 1.0.18-2
- Add patch
  * scsi-target-utils-fix-segfault-on-exit.patch

* Wed Jun 29 2011 Andy Grover <agrover@redhat.com> - 1.0.18-1
- Update to new upstream release
- Remove git-sync patch

* Fri Apr 29 2011 Dan Horák <dan[at]danny.cz> - 1.0.14-3
- no InfiniBand on s390(x)

* Thu Mar 17 2011 Andy Grover <agrover@redhat.com> - 1.0.14-2
- Add git-sync patch to get up to 9c1cd78.

* Thu Mar 17 2011 Andy Grover <agrover@redhat.com> - 1.0.14-1
- Update to new upstream release

* Tue Feb 22 2011 Andy Grover <agrover@redhat.com> - 1.0.13-1
- Update to new upstream release
  - Drop merged snprintf-fix and fix-isns-of patches
  - Update dynamic-link-iser patch for new iser module
  - Small fixes to redhatify-docs

* Wed Feb 09 2011 Fedora Release Engineering <rel-eng@lists.fedoraproject.org> - 1.0.1-5
- Rebuilt for https://fedoraproject.org/wiki/Fedora_15_Mass_Rebuild

* Tue Jun 29 2010 Mike Christie <mchristie@redhat.com> - 1.0.1-4
- Fix iSNS scn pdu overflows (CVE-2010-2221).

* Thu Apr 8 2010 Mike Christie <mchristi@redhat.com> - 1.0.1-3
- Fix format string vulnerability  (CVE-2010-0743)

* Sun Feb 14 2010 Terje Rosten <terje.rosten@ntnu.no> - 1.0.1-2
- Update iser patch to build with new link rules
- Fix optflags

* Sun Jan 24 2010 Ruben Kerkhof <ruben@rubenkerkhof.com> - 1.0.1-1
- Update to new upstream release
- Upstream moved to Sourceforge
- Fix %%preun snippet (#532176)
- Start after network service but before iscsid. Prevents failed mounts from localhost at boot.
- Replace paths with macros in spec
- Add one more doc

* Mon Dec 21 2009 Hans de Goede <hdegoede@redhat.com> - 0.9.11-1.20091205snap
- Rebase to 0.9.11 + some fixes from git (git id
  97832d8dcd00202a493290b5d134b581ce20885c)
- Rewrite initscript, make it follow:
  http://fedoraproject.org/wiki/Packaging/SysVInitScript
  And merge in RHEL-5 initscript improvements:
  - Parse /etc/tgt/targets.conf, which allows easy configuration of targets
  - Better initiator status checking in stop
  - Add force-stop, to stop even when initiators are still connected
  - Make reload reload configuration from /etc/tgt/targets.conf without
    stopping tgtd (but only for unused targets)
  - Add force-reload (reloads configs for all targets including busy ones)

* Fri Aug 21 2009 Tomas Mraz <tmraz@redhat.com> - 0.9.5-3
- rebuilt with new openssl

* Sun Jul 26 2009 Fedora Release Engineering <rel-eng@lists.fedoraproject.org> - 0.9.5-2
- Rebuilt for https://fedoraproject.org/wiki/Fedora_12_Mass_Rebuild

* Mon Mar 16 2009 Terje Rosten <terje.rosten@ntnu.no> - 0.9.5-1
- 0.9.5
- remove patch now upstream
- add patch to fix mising destdir in usr/Makefile
- mktape and dump_tape has moved to tgtimg
- add more docs

* Wed Feb 25 2009 Fedora Release Engineering <rel-eng@lists.fedoraproject.org> - 0.9.2-3
- rebuilt for https://fedoraproject.org/wiki/Fedora_11_Mass_Rebuild

* Sat Jan 17 2009 Tomas Mraz <tmraz@redhat.com> - 0.9.2-2
- rebuild with new openssl

* Tue Dec 16 2008 Jarod Wilson <jarod@redhat.com> - 0.9.2-1
- update to 0.9.2 release

* Tue Oct 21 2008 Terje Rosten <terje.rosten@ntnu.no> - 0.0-6.20080805snap
- add tgt-admin man page, tgt-admin and tgt-core-test

* Fri Aug 22 2008 Terje Rosten <terje.rosten@ntnu.no> - 0.0-5.20080805snap
- update to 20080805 snapshot

* Sun Feb 10 2008 Terje Rosten <terje.rosten@ntnu.no> - 0.0-4.20071227snap
- update to 20071227 snapshot
- add patch to compile with newer glibc

* Sat Feb 9 2008 Terje Rosten <terje.rosten@ntnu.no> - 0.0-3.20070803snap
- rebuild

* Fri Dec 7 2007 Alex Lancaster <alexlan[AT]fedoraproject.org> - 0.0-2.20070803snap
- rebuild for new openssl soname bump

* Wed Sep 26 2007 Terje Rosten <terje.rosten@ntnu.no> - 0.0-1.20070803snap
- random cleanup

* Wed Sep 26 2007 Terje Rosten <terje.rosten@ntnu.no> - 0.0-0.20070803snap
- update to 20070803
- fix license tag
- use date macro
- build with correct flags (%%optflags)

* Tue Jul 10 2007 Mike Christie <mchristie@redhat.com> - 0.0-0.20070620snap
- first build
