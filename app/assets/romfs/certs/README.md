# HTTPS trust roots

Mozilla public CA certificates extracted by curl's mk-ca-bundle, downloaded from
https://curl.se/ca/cacert.pem on 2026-09-29. This is public trust data, not a client
key or authentication credential. Bundle data date: 2026-09-25.

SHA256: a41b5d356aea97a529fe27e0f7316d2f9d946d75927476cf9cf1b90637d00505

Source and extraction details: https://curl.se/docs/caextract.html
Mozilla source notices: https://www.mozilla.org/MPL/2.0/

The USB Loader updater requires hostname and certificate verification. Never
disable either check to work around an incorrect console date or network error.
