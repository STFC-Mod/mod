# Verify a Windows DLL

A DLL's identity marker is self-declared. It identifies a build but proves no authenticity.
An artifact attestation binds exact file bytes to a workflow and source commit.

## Check an attested download

1. Download and extract `version.dll`.
2. Identify the reviewed source commit for that artifact.
3. Run the command below with a current GitHub CLI. Replace `EXPECTED_COMMIT` with the full 40-character commit.

```sh
gh attestation verify version.dll \
  --repo STFC-Mod/mod \
  --signer-workflow STFC-Mod/mod/.github/workflows/ci.yaml \
  --source-ref refs/heads/main \
  --source-digest EXPECTED_COMMIT
```

The canonical repository is `STFC-Mod/mod` (GitHub repository ID `693298224`), transferred from `netniV/stfc-mod`.
For artifacts attested before the transfer, verify the original repository and signer-workflow identity recorded in
the attestation. The transfer does not rewrite those historical claims or authorize a broader signer policy.

For a reviewed `dev` artifact, use `--source-ref refs/heads/dev`.
Keep both the source ref and digest checks.

Success verifies the file digest and the stated workflow and source provenance.
It does not prove that the code, dependencies, build environment, or behavior are safe.
It also does not make the artifact an official release.

## Attestation policy

The upstream [Build workflow](../.github/workflows/ci.yaml) verifies the embedded marker before attesting `version.dll`.
Its attestation job runs only for pushes to protected `main` or `dev` in `STFC-Mod/mod`.
Pull requests, forks, tags, unprotected refs, and other repositories do not qualify.
An authoritative attestation requires a successful eligible run of that workflow.

The marker is `stfc-identity-v1` in the DLL's `Comments` version resource.
Never use the marker alone for authorization or trust.

Downstream consumers, including STFC Mod Bridge, must retain exact reviewed file-size and SHA-256 allowlists until
an attested upstream artifact is available and their attestation policy has been reviewed separately.
