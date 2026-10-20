# Runtime dependencies of the .deb, written out by hand so one package installs
# on Ubuntu 24.04+ (Qt libraries renamed with a "t64" suffix) and on Debian 13
# (no suffix). Each Qt library is "<debian name> | <ubuntu t64 name>".
# dpkg-shlibdeps is not used: on the Ubuntu runner it emits only the t64 names.
# tests/deb_depends_test.cmake checks this list.
set(ZWRITER_DEB_DEPENDS
  "libqt6core6 (>= 6.4) | libqt6core6t64 (>= 6.4)"
  "libqt6gui6 (>= 6.4) | libqt6gui6t64 (>= 6.4)"
  "libqt6widgets6 (>= 6.4) | libqt6widgets6t64 (>= 6.4)"
  "libqt6network6 (>= 6.4) | libqt6network6t64 (>= 6.4)"
  "libqt6printsupport6 (>= 6.4) | libqt6printsupport6t64 (>= 6.4)"
  "libqt6dbus6 (>= 6.4) | libqt6dbus6t64 (>= 6.4)"
  "libqt6multimedia6 (>= 6.4) | libqt6multimedia6t64 (>= 6.4)"
  "libhunspell-1.7-0"
  "hunspell-en-us"
  "libsodium23"
  "libc6"
  "libstdc++6"
)
list(JOIN ZWRITER_DEB_DEPENDS ", " ZWRITER_DEB_DEPENDS_STRING)
