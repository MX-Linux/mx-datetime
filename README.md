# mx-datetime
Program for changing the date and time settings

[![latest packaged version(s)](https://repology.org/badge/latest-versions/mx-datetime.svg)](https://repology.org/project/mx-datetime/versions)
[![build result](https://build.opensuse.org/projects/home:mx-packaging/packages/mx-datetime/badge.svg?type=default)](https://software.opensuse.org//download.html?project=home%3Amx-packaging&package=mx-datetime)

![image](https://github.com/MX-Linux/mx-datetime/assets/418436/d60173d5-2ebb-4280-88a1-13582c69adf8)

## AppImage

Download the x86-64 AppImage from a GitHub release’s **Assets**, make it executable with `chmod +x`, and launch it as your desktop user. The image includes the date/time helper. The host supplies timezone data, Chrony for NTP, and `hwclock` for hardware clock updates. Administrative actions reopen the AppImage with the host’s elevation tool and require working FUSE. Run it from a location that the administrator can access.

To build locally on Debian 13 (trixie), install the build dependencies and run the packaging script:

```sh
sudo apt-get install \
    ca-certificates git build-essential cmake ninja-build dpkg-dev curl file \
    qt6-base-dev qt6-tools-dev qt6-tools-dev-tools qt6-svg-plugins qt6-translations-l10n \
    qt6-wayland
./make-appimage.sh
```

The script writes `build-appimage/mx-datetime-<version>-x86_64.AppImage` using the version in `debian/changelog`. GitHub Actions builds the same image and attaches it to each non-Arch release.
