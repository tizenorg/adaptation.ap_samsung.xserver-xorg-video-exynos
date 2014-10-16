# >> macros
# << macros

Name:       xorg-x11-drv-exynos
Summary:    X.Org X server driver for exynos
Version:    0.2.111
Release:    1
VCS:        magnolia/adaptation/ap_samsung/xserver-xorg-video-exynos#xorg-x11-drv-exynos-0.2.79-1-82-g61f4d4f70553099ecf87a5ca00a8dc9a08741871
ExclusiveArch:  %arm
Group:      System/X Hardware Support
License:    Samsung
Source0:    %{name}-%{version}.tar.gz

BuildRequires:  prelink
BuildRequires:  xorg-x11-xutils-dev
BuildRequires:  pkgconfig(xorg-server)
BuildRequires:  pkgconfig(x11)
BuildRequires:  pkgconfig(xproto)
BuildRequires:  pkgconfig(fontsproto)
BuildRequires:  pkgconfig(randrproto)
BuildRequires:  pkgconfig(renderproto)
BuildRequires:  pkgconfig(videoproto)
BuildRequires:  pkgconfig(resourceproto)
BuildRequires:  pkgconfig(libudev)
BuildRequires:  pkgconfig(libtbm)
BuildRequires:  pkgconfig(xdbg)
BuildRequires:  libdrm-devel
BuildRequires:  libdrm2

%description
This package provides the driver for the Samsung display device exynos


%prep
%setup -q

# >> setup
# << setup

%build
rm -rf %{buildroot}
# >> build pre
# << build pre

%if %{?tizen_profile_name} == "wearable" 
export CFLAGS+=" -D_F_WEARABLE_PROFILE_ "
%endif

%reconfigure --disable-static \
	CFLAGS="${CFLAGS} -Wall -Werror" LDFLAGS="${LDFLAGS} -Wl,--hash-style=both -Wl,--as-needed"


make %{?jobs:-j%jobs}

# >> build post
# << build post
%install
rm -rf %{buildroot}
# >> install pre
# << install pre
mkdir -p %{buildroot}/usr/share/license
cp -af COPYING %{buildroot}/usr/share/license/%{name}
%make_install

# >> install post
execstack -c %{buildroot}%{_libdir}/xorg/modules/drivers/exynos_drv.so
# << install post

%files
%defattr(-,root,root,-)
# >> files exynos
%{_libdir}/xorg/modules/drivers/*.so
%{_datadir}/man/man4/*
/usr/share/license/%{name}
# << files exynos
