# Kindle PW5 compatibility notes

## Result of the investigation

The project target is the Paperwhite 5 (11th generation) on firmware 5.19.2.
For firmware at or above 5.16.3, the KindleModding development documentation
selects the `kindlehf` target. The corresponding toolchain uses the
`arm-kindlehf-linux-gnueabihf-*` compiler family and produces a 32-bit,
little-endian ARM hard-float executable. This is the correct target for PW5
5.19.2; `kindlepw2` is the older target for firmware below 5.16.3.

The device-side checks to run before installation are:

```sh
uname -m                 # expected: armv7l
uname -r                 # record the exact Kindle kernel
readelf -l ./inkboy | grep interpreter
```

The executable should use the device's hard-float ARM loader
(`/lib/ld-linux-armhf.so.3`, as provided by the firmware) and must not be
linked against libraries copied from the host.

## ARM and GTK2 dependencies

The C source only uses GTK2, GLib and the GTK2 X11 backend. The final runtime
set is determined by the Kindle firmware's `gtk+-2.0.pc`; it normally includes
the GTK/GDK/ATK/Pango/Cairo/GLib stack and the X11 libraries needed by GDK.
Typical SONAMEs are:

- `libgtk-x11-2.0.so.0`, `libgdk-x11-2.0.so.0`, `libatk-1.0.so.0`
- `libpango-1.0.so.0`, `libpangocairo-1.0.so.0`, `libpangoft2-1.0.so.0`
- `libcairo.so.2`, `libgdk_pixbuf-2.0.so.0`, `libglib-2.0.so.0`,
  `libgobject-2.0.so.0`, `libgio-2.0.so.0`
- the firmware's matching font, image, X11, XCB, bzip2, zlib, pthread and C
  runtime libraries

These names are a dependency description, not a license to bundle Ubuntu
ARM libraries. The cross-build uses the Kindle SDK sysroot and its pkg-config
metadata. The Docker image includes `libgtk2.0-dev` only for the optional host
smoke build and build tooling; it is not used as the Kindle runtime sysroot.

## Toolchain and SDK dependencies

The isolated setup uses:

- Docker and privileged containers for the SDK's firmware-image mount step
- the prebuilt KindleModding `kindlehf` koxtoolchain
- the KindleModding Kindle SDK, which adds firmware libraries and pkg-config
  metadata to the toolchain sysroot
- Make, GCC/binutils, pkg-config, GTK2 development headers, Git, curl/wget,
  aria2, and the SDK's archive/mount utilities

No emulator, QEMU setup, or device-side compiler is included in v0.1.

## Compatibility blockers

1. There is no PW5 attached to this workspace, so launch, touchscreen events,
   dynamic loading, window-manager placement, and e-ink refresh have not been
   verified on hardware.
2. The current Kindle SDK installer uses a 5.16.3 firmware baseline for its
   `kindlehf` sysroot. That establishes the ABI family but is not a byte-for-
   byte 5.19.2 runtime match. For release-quality validation, generate the
   sysroot from the exact 5.19.2 PW5 firmware or compare every `NEEDED` library
   with the device before deployment.
3. GTK2 requires the Kindle's X11/Awesome windowing environment. The app is
   not a framebuffer or emulator application; it assumes the normal GUI
   session is available when KPM launches it.
4. KPM packaging/launcher metadata is intentionally not part of this
   milestone. The raw executable is the v0.1 artifact; packaging and a device
   launch script can be added after hardware validation.

## References

- [KindleModding Kindle SDK](https://kindlemodding.org/kindle-dev/kindle-sdk.html)
- [KindleModding GTK tutorial prerequisites](https://kindlemodding.org/kindle-dev/gtk-tutorial/prerequisites.html)
- [KindleModding GTK project setup](https://kindlemodding.org/kindle-dev/gtk-tutorial/setting-up.html)
- [KindleModding Awesome WM window-title format](https://kindlemodding.org/kindle-dev/awesome-window-manager/)
