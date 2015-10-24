# >> macros
# << macros
?include_ftests?
%bcond_with ftests=1

Name:       xorg-x11-drv-exynos
Summary:    X.Org X server driver for exynos
Version:    1.0.0
Release:    3
ExclusiveArch:  %arm
Group:      System/X Hardware Support
License:    MIT
Source0:    %{name}-%{version}.tar.gz

BuildRequires:  prelink
BuildRequires:  pkgconfig(xorg-macros)
BuildRequires:  pkgconfig(xorg-server)
BuildRequires:  pkgconfig(xproto)
BuildRequires:  pkgconfig(fontsproto)
BuildRequires:  pkgconfig(randrproto)
BuildRequires:  pkgconfig(renderproto)
BuildRequires:  pkgconfig(videoproto)
BuildRequires:  pkgconfig(resourceproto)
BuildRequires:  pkgconfig(libudev)
BuildRequires:  pkgconfig(libtbm)
BuildRequires:  pkgconfig(xdbg)
BuildRequires:  pkgconfig(libdrm)
BuildRequires:  pkgconfig(libpng)
BuildRequires:  pkgconfig(dri3proto)
BuildRequires:  pkgconfig(presentproto)
BuildRequires:  pkgconfig(ttrace)
BuildRequires:  pkgconfig(xcb)
BuildRequires:  pkgconfig(xcb-util)
BuildRequires:  pkgconfig(xrandr)
BuildRequires:  pkgconfig(hwaproto)

%description
This package provides the driver for the Samsung display device exynos

%prep
%setup -q


%build
rm -rf %{buildroot}

%if %{?tizen_profile_name} == "wearable"
export CFLAGS+=" -D_F_WEARABLE_FEATURE_ "
%endif

%if %{with ftests}
export FTESTS="--enable-ftests"
%endif

?SUBSTITUTE_PARAMETERS?

make %{?jobs:-j%jobs}

%install
rm -rf %{buildroot}
mkdir -p %{buildroot}/usr/share/license
cp -af COPYING %{buildroot}/usr/share/license/%{name}
%make_install

%if %{without ftests}
 # >> install post
 execstack -c %{buildroot}%{_libdir}/xorg/modules/drivers/exynos_drv.so
 # << install post
%endif

%files
%defattr(-,root,root,-)
%if %{without ftests}
 %{_libdir}/xorg/modules/drivers/*.so
 %{_datadir}/man/man4/*
 /usr/share/license/%{name}
%else
 /usr/share/license/%{name}

 # to build functional tests simple add option to gbs "--define with_ftests=1"
 # Note: if you build functional tests, ddx driver willn't be built
 %{_libdir}/libdri2_dri3.so
 %{_bindir}/test_xv
 %{_bindir}/hwc-sample
 %{_bindir}/square-bubbles
 %{_bindir}/clock
 %{_bindir}/snowflake
 %{_bindir}/wander-stripe
 %{_bindir}/hwa_sample
 %{_bindir}/pixmap_copy
 %{_datadir}/launch.sh

%post
 chmod +x %{_datadir}/launch.sh
%endif
