#
# Copyright (C)  Heinz Mauelshagen, 2004-2006 Red Hat GmbH. All rights reserved.
#
# See file LICENSE at the top of this source tree for license information.
#

Summary: dmraid (Device-mapper RAID tool and library)
Name: dmraid
Version: 1.0.0.rc15-pre1
Release: 1
License: GPL
Group: System Environment/Base
URL: http://people.redhat.com/heinzm/sw/dmraid
BuildRoot: %{_tmppath}/%{name}-%{version}-%{release}-buildroot
Source: ftp://people.redhat.com/heinzm/sw/dmraid/src/dmraid-%{version}.tar.bz2

ExcludeArch:	s390
ExcludeArch:	s390x

%define dmraid_build_dso 0

%description
DMRAID supports RAID device discovery, RAID set activation and display of
properties for ATARAID on Linux >= 2.4 using device-mapper.

%package -n dmraid-devel
Summary: Development libraries and headers for dmraid.
Group: Development/Libraries

%description -n dmraid-devel
dmraid-devel provides a library interface for RAID device discovery,
RAID set activation and display of properties for ATARAID volumes.

%prep
%setup -q -n dmraid/%{version}

%build
%configure --prefix=${RPM_BUILD_ROOT}/usr --sbindir=${RPM_BUILD_ROOT}/sbin --libdir=${RPM_BUILD_ROOT}/%{_libdir} --mandir=${RPM_BUILD_ROOT}/%{_mandir} --includedir=${RPM_BUILD_ROOT}/%{_includedir} --enable-debug --enable-libselinux --enable-static_link
make DESTDIR=$RPM_BUILD_ROOT
mv tools/dmraid tools/dmraid.static
make clean
%configure --prefix=${RPM_BUILD_ROOT}/usr --sbindir=${RPM_BUILD_ROOT}/sbin --libdir=${RPM_BUILD_ROOT}/%{_libdir} --mandir=${RPM_BUILD_ROOT}/%{_mandir} --includedir=${RPM_BUILD_ROOT}/%{_includedir} --enable-debug --enable-libselinux --disable-static_link
%if "%{dmraid_build_dso}" == "1"
(cd lib ; make DESTDIR=$RPM_BUILD_ROOT libdmraid.so)
make DESTDIR=$RPM_BUILD_ROOT
%else
make DESTDIR=$RPM_BUILD_ROOT
%endif

%install
rm -rf $RPM_BUILD_ROOT
install -m 755 -d $RPM_BUILD_ROOT{%{_libdir},/sbin,%{_sbindir},%{_bindir},%{_libdir},%{_includedir}/dmraid/,/var/lock/dmraid}
make DESTDIR=$RPM_BUILD_ROOT install
install -m 755 tools/dmraid.static $RPM_BUILD_ROOT/sbin/dmraid.static
install -m 644 include/dmraid/*.h $RPM_BUILD_ROOT%{_includedir}/dmraid/

# install the static library
install -m 755 lib/libdmraid.a $RPM_BUILD_ROOT%{_libdir}

# if requested, install the dso
%if "%{dmraid_build_dso}" == "1"
install -m 755 lib/libdmraid.so \
	$RPM_BUILD_ROOT%{_libdir}/libdmraid.so.%{version}
(cd $RPM_BUILD_ROOT/%{_libdir} ; ln -sf libdmraid.so.%{version} libdmraid.so)
%endif

%clean
rm -rf $RPM_BUILD_ROOT

%files
%defattr(-,root,root)
%doc CHANGELOG CREDITS KNOWN_BUGS LICENSE LICENSE_GPL LICENSE_LGPL README TODO doc/dmraid_design.txt
/%{_mandir}/man8/*
/sbin/*
%if "%{dmraid_build_dso}" == "1"
%{_libdir}/libdmraid.so.*
%endif

%files -n dmraid-devel
%defattr(-,root,root)
%dir %{_includedir}/dmraid
%{_includedir}/dmraid/*
%{_libdir}/libdmraid.a

%if "%{dmraid_build_dso}" == "1"
%{_libdir}/libdmraid.so
%endif

%changelog
* Fri Feb 17 2006 Heinz Mauelshagen <jheinzm@redhat.com> - 1.0.0.rc9-FC5_5.3
- add doc/dmraid_design.txt to %doc (#181885)
- add --enable-libselinux to configure
- not build yet

* Fri Feb 10 2006 Jesse Keating <jkeating@redhat.com> - 1.0.0.rc9-FC5_5.2
- bump again for double-long bug on ppc(64)

* Tue Feb 07 2006 Jesse Keating <jkeating@redhat.com> - 1.0.0.rc9-FC5_5.1
- rebuilt for new gcc4.1 snapshot and glibc changes

* Sun Jan 22 2006 Peter Jones <pjones@redhat.com> 1.0.0.rc9-FC5_5
- Add selinux build deps
- Don't set owner during make install

* Fri Dec  9 2005 Jesse Keating <jkeating@redhat.com> 1.0.0.rc9-FC5_4.1
- rebuilt

* Sun Dec  3 2005 Peter Jones <pjones@redhat.com> 1.0.0.rc9-FC5_4
- rebuild for device-mapper-1.02.02-2

* Fri Dec  2 2005 Peter Jones <pjones@redhat.com> 1.0.0.rc9-FC5_3
- rebuild for device-mapper-1.02.02-1

* Thu Nov 10 2005 Peter Jones <pjones@redhat.com> 1.0.0.rc9-FC5_2
- update to 1.0.0.rc9
- make "make install" do the right thing with the DSO
- eliminate duplicate definitions in the headers
- export more symbols in the DSO
- add api calls to retrieve dm tables
- fix DESTDIR for 'make install' 
- add api calls to identify degraded devices
- remove several arch excludes

* Sat Oct 15 2005 Florian La Roche <laroche@redhat.com>
- add -lselinux -lsepol for new device-mapper deps

* Fri May 20 2005 Heinz Mauelshagen <heinzm@redhat.com> 1.0.0.rc8-FC4_2
- specfile change to build static and dynamic binray into one package
- rebuilt

* Thu May 19 2005 Heinz Mauelshagen <heinzm@redhat.com> 1.0.0.rc8-FC4_1
- nv.c: fixed stripe size
- sil.c: avoid incarnation_no in name creation, because the Windows
         driver changes it every time
- added --ignorelocking option to avoid taking out locks in early boot
  where no read/write access to /var is given

* Wed Mar 16 2005 Elliot Lee <sopwith@redhat.com>
- rebuilt

* Tue Mar 15 2005 Heinz Mauelshagen <heinzm@redhat.com> 1.0.0.rc6.1-4_FC4
- VIA metadata format handler
- added RAID10 to lsi metadata format handler
- "dmraid -rD": file device size into {devicename}_{formatname}.size
- "dmraid -tay": pretty print multi-line tables ala "dmsetup table"
- "dmraid -l": display supported RAID levels + manual update
- _sil_read() used LOG_NOTICE rather than LOG_INFO in order to
  avoid messages about valid metadata areas being displayed
  during "dmraid -vay".
- isw, sil filed metadata offset on "-r -D" in sectors rather than in bytes.
- isw needed dev_sort() to sort RAID devices in sets correctly.
- pdc metadata format handler name creation. Lead to
  wrong RAID set grouping logic in some configurations.
- pdc RAID1 size calculation fixed (rc6.1)
- dos.c: partition table code fixes by Paul Moore
- _free_dev_pointers(): fixed potential OOB error
- hpt37x_check: deal with raid_disks = 1 in mirror sets
- pdc_check: status & 0x80 doesn't always show a failed device;
  removed that check for now. Status definitions needed.
- sil addition of RAID sets to global list of sets
- sil spare device memory leak
- group_set(): removal of RAID set in case of error
- hpt37x: handle total_secs > device size
- allow -p with -f
- enhanced error message by checking target type against list of
  registered target types

* Fri Jan 21 2005 Alasdair Kergon <agk@redhat.com> 1.0.0.rc5f-2
- Rebuild to pick up new libdevmapper.

* Fri Nov 26 2004 Heinz Mauelshagen <heinzm@redhat.com> 1.0.0.rc5f
- specfile cleanup

* Tue Aug 20 2004 Heinz Mauelshagen <heinzm@redhat.com> 1.0.0-rc4-pre1
- Removed make flag after fixing make.tmpl.in

* Tue Aug 18 2004 Heinz Mauelshagen <heinzm@redhat.com> 1.0.0-rc3
- Added make flag to prevent make 3.80 from looping infinitely

* Thu Jun 17 2004 Heinz Mauelshagen <heinzm@redhat.com> 1.0.0-pre1
- Created
