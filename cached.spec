Summary: Cached daemon
Name: cached
Version: 0.4
Release: 1
Source0: %{name}-%{version}.tar.gz
BuildRoot: %{_tmppath}/%{name}-%{version}-%{release}-build
License: GPL
Group: Web/Tools
requires: mysql
requires: gamin
requires: curl
requires: nginx
%description
Cached1 - daemon for cacheing big files from backend to frontend
%prep
%setup -q
mkdir -p %buildroot/usr/bin
mkdir -p %buildroot/etc/cached
mkdir -p %buildroot/etc/init.d
mkdir -p %buildroot/etc/logrotate.d
mkdir -p %buildroot/var/log/cached
mkdir -p %buildroot/var/lock/cached
%build
cd %{_builddir}/%{name}-%{version}/build
make all
%install
cd %{_builddir}/%{name}-%{version}/build
make DESTDIR=%buildroot install
%files
%defattr(-,_nginx,_nginx)
%{_bindir}/%{name}
%dir %{_sysconfdir}/cached
%dir /var/log/cached
%dir /var/lock/cached
%{_sysconfdir}/init.d/cached
%{_sysconfdir}/logrotate.d/cached.logrotate
%config(noreplace) %{_sysconfdir}/cached/cachedc.conf
~                                                       
