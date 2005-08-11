Summary: IceCube MOAT testing applications
Name: moat
Version: %{VER}
Release: %{REL}
URL: http://docushare.icecube.wisc.edu/docushare/dsweb/View/Collection-511
Source0: %{name}-%{version}.tgz
License: Copyright 2003 LBNL/IceCube collaboration (sorry, *NOT* GPL)
Group: System Environment/Base
BuildRoot: %{_tmppath}/%{name}-root
Prefix: %{_prefix}
Requires: perl > 5.0
Requires: domhub-tools >= 100

%description
IceCube MOAT testing applications

%prep

%setup -q

%build
make clean; make

%install
install -d ${RPM_BUILD_ROOT}/usr/local/share
install -d ${RPM_BUILD_ROOT}/usr/local/bin

install moat-version ${RPM_BUILD_ROOT}/usr/local/share
install readwrite ${RPM_BUILD_ROOT}/usr/local/bin
install dtest ${RPM_BUILD_ROOT}/usr/local/bin
install tcaltest ${RPM_BUILD_ROOT}/usr/local/bin
install echo-loop ${RPM_BUILD_ROOT}/usr/local/bin
install readgps ${RPM_BUILD_ROOT}/usr/local/bin
install rndpkt ${RPM_BUILD_ROOT}/usr/local/bin
install watchcomms ${RPM_BUILD_ROOT}/usr/local/bin
install moat ${RPM_BUILD_ROOT}/usr/local/bin
install moat14 ${RPM_BUILD_ROOT}/usr/local/bin
install stagedtests.pl ${RPM_BUILD_ROOT}/usr/local/bin
install se.pl ${RPM_BUILD_ROOT}/usr/local/bin
install sb.pl ${RPM_BUILD_ROOT}/usr/local/bin
install anamoat ${RPM_BUILD_ROOT}/usr/local/bin

%clean
rm -rf $RPM_BUILD_ROOT

%files
%defattr(-,root,root)
/usr/local/share/moat-version
/usr/local/bin/readwrite
/usr/local/bin/dtest
/usr/local/bin/tcaltest
/usr/local/bin/echo-loop
/usr/local/bin/readgps
/usr/local/bin/rndpkt
/usr/local/bin/watchcomms
/usr/local/bin/moat
/usr/local/bin/moat14
/usr/local/bin/stagedtests.pl
/usr/local/bin/se.pl
/usr/local/bin/sb.pl
/usr/local/bin/anamoat

%changelog
* Tue Jul 12 2005 John E. Jacobsen <jacobsen@npxdesigns.com>
- Tweaks to URL
* Mon Jun 20 2005 Martin C. Stoufer <MCStoufer@lbl.gov>
- Initial build.


