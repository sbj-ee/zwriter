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
   Do not also create the release by hand: the workflow does it, and a release without
   `SHA256SUMS.minisig` can never be installed by the in-app updater.

## Signing key

The in-app updater (Help → Check for Updates) installs only releases whose `SHA256SUMS`
verifies against the minisign public key compiled into the app, so a compromised GitHub
release alone cannot push malware to users.

zwriter has its **own release key**: key ID `DBC46B23C3AF0EF3` (public key below), generated
with `minisign -G` in October 2026. Its public half is in `src/UpdateSigningKey.cpp`. The secret
key lives only on the maintainer's machine (`~/.minisign/minisign.key`) and, with its password, in
the two repository secrets `MINISIGN_SECRET_KEY` and `MINISIGN_PASSWORD` that `release.yml`
uses; it is never committed or pasted into a chat. To set or replace the secrets (the owner only):

```sh
gh secret set MINISIGN_SECRET_KEY -R sbj-ee/zwriter < ~/.minisign/minisign.key
gh secret set MINISIGN_PASSWORD   -R sbj-ee/zwriter    # paste the password when asked
```

To change the key: generate one on your own machine (not in CI, not on a shared box),
`minisign -G -p zwriter-release.pub -s zwriter-release.key`, set the two secrets from that key,
paste the second line of the `.pub` file (it starts with `RW`) into `kUpdateSigningPublicKey` in
`src/UpdateSigningKey.cpp`, update the key ID in this file, the README and
`tests/minisign_test.cpp` (`compiledReleaseKeyIsValid`), and release.
`tests/data/minisign/release-key/` is a real signature by a *different* key (zterminal's, whose
public key is a constant in the test); it only proves the verifier reads real minisign output
and does not need to change.

Keep the secret key and its password in a password manager plus an offline backup. If it is
lost, installed copies cannot verify later releases (install the next one by hand); if it
leaks, rotate it: new key pair, new secrets, new public key in the source, release, and tell
users to install that release by hand.

## Checking a release by hand

```sh
minisign -Vm SHA256SUMS -P RWTzDq/DI2vE2xMeF01QWTxP2lwwlxGL9M1UxQE4EFV5kVmTF84wwyi2
sha256sum -c --ignore-missing SHA256SUMS          # Linux
shasum -a 256 -c --ignore-missing SHA256SUMS      # macOS
```

The first command proves `SHA256SUMS` was signed by the release key; the second proves your
package matches it.

## Transition

Releases up to 1.2.1 have no `SHA256SUMS` or signature and the app before 1.3.1 cannot
install updates itself. Install the first signed release (1.3.1 or later) by hand, verifying it
with `minisign -Vm` as above; updates after that are checked in the app. A release that
lacks `SHA256SUMS.minisig` is never installed by the app: it says so and points at the
release page.

## Local checks

`ctest` runs the updater tests (`minisign_test`, `update_test`) against fixtures and a mock
GitHub on a local socket. A debug build (`-DCMAKE_BUILD_TYPE=Debug`, or
`-DZWRITER_DEV_OVERRIDES=ON`) also honours `$ZWRITER_UPDATE_URL` to point the update check at
a test server; release builds never read it.
