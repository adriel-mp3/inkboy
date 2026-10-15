# InkBoy architecture decisions

## FBInk is currently vendored

InkBoy uses a minimal, statically linked FBInk build for direct Kindle e-ink
rendering. The source is kept under `third_party/fbink/` and is built into the
InkBoy executable; it is not installed on the Kindle root filesystem.

This decision was made because the tested KOReader installation provides the
FBInk executable at `/mnt/us/koreader/fbink`, but does not provide a linkable
core `libfbink.so`. The available `libfbink_input.so.1` is an input helper and
does not replace the rendering API. Invoking the KOReader executable from
InkBoy would also add a runtime process dependency and would not provide the C
API used by the renderer.

The vendored source is pinned to upstream commit `886f25f`, built with the
minimal image and drawing features required by InkBoy. Its upstream license
and notices are preserved in `third_party/fbink/LICENSE` and the related
third-party license files.

## Conditions for removing `third_party/fbink`

The directory may be removed only after a replacement has been built and
verified on the physical PW5. Acceptable alternatives are:

1. a compatible linkable FBInk library already present on the device;
2. a separately packaged FBInk library supplied through the Kindle SDK and
   deployed without modifying the root filesystem; or
3. a replacement InkBoy renderer that safely handles the PW5 framebuffer,
   pixel format, refresh operations, and restoration behavior.

The replacement must preserve the current guarantees: no direct framebuffer
access in application code unless explicitly justified, one controlled
refresh for the static screen, and restoration after normal termination.
Compilation on the host alone is not sufficient evidence for removal.

## Why it is not removed now

Keeping the dependency in the repository makes the executable reproducible,
avoids relying on undocumented KOReader installation details, and keeps the
current workflow independent of system files. It also lets the project pin
device-specific FBInk behavior while the Kindle target is still being
validated.

This is an implementation decision, not a permanent commitment to FBInk. A
future dependency cleanup can replace it with a documented alternative once
the conditions above are met.
