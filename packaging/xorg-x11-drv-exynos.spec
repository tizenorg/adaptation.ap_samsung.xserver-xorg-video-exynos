# >> macros
# << macros

Name:       xorg-x11-drv-exynos
Summary:    X.Org X server driver for exynos
Version:    0.2.79
Release:    1
ExclusiveArch:  %arm
Group:      System/X Hardware Support
License:    Samsung
Source0:    %{name}-%{version}.tar.gz
BuildRequires:  prelink

%description
This package provides the driver for the Samsung display device exynos


%prep
%setup -q


%build

%reconfigure

make %{?jobs:-j%jobs}

%install
rm -rf %{buildroot}
%make_install

execstack -c %{buildroot}%{_libdir}/xorg/modules/drivers/exynos_drv.so

%files
%defattr(-,root,root,-)
%{_libdir}/xorg/modules/drivers/*.so

