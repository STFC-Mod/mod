# Windows DLL provenance

Windows builds publish a `stfc-identity-v1` marker in the DLL's standard `Comments` version resource. The marker is
minimal, descriptive, and self-declared. It helps identify a build, but it is not evidence of authenticity and must
not be used as an authorization or trust decision by itself.

For protected `main` and `dev` pushes in `STFC-Mod/mod`, the upstream Build workflow creates a GitHub/Sigstore
build-provenance attestation for the exact `version.dll` bytes after verifying the embedded marker. Pull requests,
fork runs, unprotected refs, tags, and other repositories do not create authoritative attestations.

After downloading and extracting an attested DLL, verify its byte integrity, expected upstream workflow identity,
protected source ref, and reviewed source commit with a current GitHub CLI. Replace `<expected-commit-sha>` with the
40-character commit for the artifact:

```shell
gh attestation verify version.dll \
  --repo STFC-Mod/mod \
  --signer-workflow STFC-Mod/mod/.github/workflows/ci.yaml \
  --source-ref refs/heads/main \
  --source-digest <expected-commit-sha>
```

The canonical repository is `STFC-Mod/mod` (GitHub repository ID `693298224`), transferred from `netniV/stfc-mod`.
For artifacts attested before the transfer, verify the original repository and signer-workflow identity recorded in
the attestation; the transfer does not rewrite those historical claims or authorize a broader signer policy.

For a reviewed `dev` artifact, use `--source-ref refs/heads/dev` instead. Do not omit the source ref or digest when
making a trust decision.

Successful verification establishes that the exact file digest is covered by an attestation issued for the named
repository and signer workflow, including source commit provenance. It does not establish that the source,
dependencies, build environment, or resulting behavior are free from malicious or unsafe behavior. It also does not,
by itself, authorize the artifact as an official release.

The first authoritative upstream attestation can exist only after the attestation workflow is merged and then runs on
an eligible protected upstream push. Existing releases are unattested. Downstream consumers, including STFC Mod
Bridge, must continue to use exact reviewed file size and SHA-256 allowlists until an attested upstream artifact is
actually available and their attestation policy is separately reviewed.
