# >> macros
# << macros

Name:       xorg-x11-drv-exynos
Summary:    X.Org X server driver for exynos
Version:    1.0.0
Release:    2
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
BuildRequires:  pkgconfig(libhwc)
BuildRequires:  pkgconfig(xcomposite)
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

%autogen --disable-static --enable-dri3 --enable-hwc --disable-hwa --enable-ftests \
        CFLAGS="${CFLAGS} -Wall -Werror -mfpu=neon -DLAYER_MANAGER -DSIMPLE_DRI2 -DNO_CRTC_MODE -DUSE_PIXMAN_COMPOSITE -DLEGACY_INTERFACE -DHWC_USE_DEFAULT_LAYER -DHWC_ENABLE_REDRAW_LAYER -mfloat-abi=softfp" LDFLAGS="${LDFLAGS} -Wl,--hash-style=both -Wl,--as-needed"

make %{?jobs:-j%jobs}

%install
rm -rf %{buildroot}
mkdir -p %{buildroot}/usr/share/license
cp -af COPYING %{buildroot}/usr/share/license/%{name}
%make_install

cp tests/functional/hwc_test/launch.sh %{buildroot}/

# >> install post
execstack -c %{buildroot}%{_libdir}/xorg/modules/drivers/exynos_drv.so
# << install post

%files
%defattr(-,root,root,-)
%{_libdir}/xorg/modules/drivers/*.so
%{_datadir}/man/man4/*
/usr/share/license/%{name}

# to build functional test simple add configure's option (--enable-ftests) and uncomment below lines.
 %{_libdir}/libdri2_dri3.so
 %{_bindir}/test_xv
 %{_bindir}/hwc-sample
 %{_bindir}/square-bubbles
 %{_bindir}/clock
 %{_bindir}/snowflake
 %{_bindir}/wander-stripe
 %{_bindir}/hwa_sample
 /launch.sh
