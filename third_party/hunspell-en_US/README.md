# en_US Hunspell dictionary (bundled in the macOS app)

`en_US.aff` / `en_US.dic` are copied unchanged from Debian's `hunspell-en-us`
package, version 1:2020.12.07-4 (built from SCOWL, http://wordlist.aspell.net/).
License and copyright: see `COPYRIGHT` (SCOWL's permissive licence, as shipped
by Debian).

Only the macOS build uses these files: they are copied into
`zwriter.app/Contents/Resources/hunspell/`, which the spell checker searches
first. Linux uses the system `hunspell-en-us` package.
