# Releasing zwriter

1. Make sure `project(zwriter VERSION x.y.z)` in `CMakeLists.txt` has the version to
   release, and the `CHANGELOG.md` heading says the release date instead of "unreleased".
   (Every pull request bumps the version and adds its changelog entry; change nothing else.)
2. Merge to `main`, then tag and push: `git tag vX.Y.Z && git push origin vX.Y.Z`.
3. `.github/workflows/release.yml` builds and tests on Linux and macOS, packages
   `zwriter-X.Y.Z-Linux-amd64.deb` and `zwriter-X.Y.Z-Darwin.dmg`, writes `SHA256SUMS`,
   **signs it** with minisign (`SHA256SUMS.minisig`), checks that signature against the public
   key in `src/UpdateSigningKey.cpp` at the tagged commit, and creates the GitHub Release.
   Without the signing secrets, or with no public key in the source, the publish job stops
   and nothing is released.

## Signing key

The in-app updater (Help → Check for Updates) installs only releases whose `SHA256SUMS`
verifies against the minisign public key compiled into the app, so a compromised GitHub
release alone cannot push malware to users.

zwriter uses the **same release key as zterminal** (key ID `32960BBA77B36F1F`; its public key is
in `src/UpdateSigningKey.cpp`). The secret key stays on Stephen's own machine and in zterminal's
repository secrets; it was never handled in this repository's development. To let this
repository's release workflow sign with it, once:

```sh
gh secret set MINISIGN_SECRET_KEY -R sbj-ee/zwriter < zterminal-release.key
gh secret set MINISIGN_PASSWORD   -R sbj-ee/zwriter    # paste the password when asked
```

To use a separate key for zwriter instead, generate one on your own machine (not in CI, not
on a shared box), `minisign -G -p zwriter-release.pub -s zwriter-release.key`, set the two
secrets from that key, paste the second line of the `.pub` file (it starts with `RW`) into
`kUpdateSigningPublicKey` in `src/UpdateSigningKey.cpp`, and release.

Keep the secret key and its password in a password manager plus an offline backup. If it is
lost, installed copies cannot verify later releases (install the next one by hand); if it
leaks, rotate it: new key pair, new secrets, new public key in the source, release, and tell
users to install that release by hand.

## Checking a release by hand

```sh
minisign -Vm SHA256SUMS -P RWQfb7N3uguWMhJ7jsE/Z0hZ7pDWHQ9OEeWSBTlN/R5bxQ8HWSMxp/l9
sha256sum -c --ignore-missing SHA256SUMS          # Linux
shasum -a 256 -c --ignore-missing SHA256SUMS      # macOS
```

The first command proves `SHA256SUMS` was signed by the release key; the second proves your
package matches it.

## Transition

Releases up to 1.2.0 have no `SHA256SUMS` or signature and the app before 1.3.0 cannot
install updates itself. Install the first signed release (1.3.0 or later) by hand, verifying it
with `minisign -Vm` as above; updates after that are checked in the app. A release that
lacks `SHA256SUMS.minisig` is never installed by the app: it says so and points at the
release page.

## Local checks

`ctest` runs the updater tests (`minisign_test`, `update_test`) against fixtures and a mock
GitHub on a local socket. A debug build (`-DCMAKE_BUILD_TYPE=Debug`, or
`-DZWRITER_DEV_OVERRIDES=ON`) also honours `$ZWRITER_UPDATE_URL` to point the update check at
a test server; release builds never read it.
