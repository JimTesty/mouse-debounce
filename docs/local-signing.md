# Stable local code signing

MouseDebounce needs Accessibility permission. If each development build is signed ad hoc (`codesign --sign -`), macOS can treat rebuilt binaries as different code identities and require Accessibility to be granted again.

For repeated local development, sign every build with the same persistent code-signing identity.

## One-time setup

In **Keychain Access**:

1. Open **Certificate Assistant -> Create a Certificate...**
2. Name it `MouseDebounce Local Signing`.
3. Choose **Self Signed Root** for Identity Type.
4. Choose **Code Signing** for Certificate Type.
5. Store it in your login keychain.

Then verify that `codesign` can see it:

```sh
security find-identity -v -p codesigning
```

## Build with the stable identity

```sh
make SIGN_IDENTITY="MouseDebounce Local Signing"
tools/mousedebouncectl install
```

The Makefile still defaults to ad-hoc signing when `SIGN_IDENTITY` is not supplied.

When switching from an existing ad-hoc build to the persistent identity, macOS may require one final Accessibility reset/regrant. After that, subsequent builds signed with the same identity should retain the same TCC/code identity.

If needed:

```sh
tools/mousedebouncectl reset-accessibility
tools/mousedebouncectl grant
```

Then enable Mouse Debounce in **System Settings -> Privacy & Security -> Accessibility** and start the service.

This self-signed identity is intended only for development on machines where you trust/install the certificate. Public distribution should use an appropriate Apple-issued Developer ID identity and normal notarization/signing practices.
