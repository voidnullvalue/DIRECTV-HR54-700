# Licensing and attribution

**No open-source licence is granted over the proprietary material in this
project, because none of it is ours to licence.** There is deliberately no
`LICENSE` file in this repository, and that is intentional rather than an
oversight.

## What is ours, and not yet licensed

The original source in this repository — the C backend, the JavaScript TV
frontend, the Python tooling, the shell scripts, the MIPS assembler in Python,
the documentation, and the generated `assets/screensaver-hax0r.png` artwork —
is original work by the author of this project.

It is **published without a licence grant.** All rights are reserved by
default. The author has deliberately not chosen a licence at the time of
publication.

If you want to reuse it, open an issue and ask. Please do not assume
permissive terms, and please do not assume the absence of a `LICENSE` file
means "public domain" or "do what you want" — it does not.

The reason is not secrecy about the code. It is that a licence file is a
legal instrument, and publishing one for a project whose whole subject is a
commercially licensed third-party device is a decision that should be made
deliberately by the rights holder, with the proprietary/proprietary boundaries
below settled first.

## What is NOT ours, and is NOT included

**No DIRECTV firmware, filesystem image, vendor binary, or copyrighted content
is redistributed here.** Specifically, none of the following is in this
repository:

- plugin SquashFS images, or any file extracted from them
- `dtv.car`, or any other vendor application archive
- vendor shared libraries (`libdvr.so`, `libcwebkit.so`, `libapg.so`,
  `libDtvNVRamMgr.so`, `sigtst`, and the rest)
- Broadcom firmware blobs and kernel modules
- disk images, partition dumps, MTD dumps, and extracted root filesystems
- the genuine `indexer` binary, preserved in the project as `indexer.real`
- DIRECTV certificates, private keys, or access-card data
- DIRECTV UI artwork, including the stock splash image
- any account-linked identifier (card ID, receiver ID) or the receiver's
  password hash

All of it remains the property of its respective owners. This repository
contains only the *transformation* — patchers, builders, offsets, hashes, and
verification gates — plus the author's own original source and analysis.

See [docs/PROPRIETARY_INPUTS.md](docs/PROPRIETARY_INPUTS.md) for how to obtain
each required input from your own device, and
[docs/NOT_INCLUDED.md](docs/NOT_INCLUDED.md) for the complete exclusion list.

### About the signature reuse

This project does **not** forge, break, or bypass any digital signature. It
reuses a genuine vendor signature verbatim, unmodified, because a
filename/path confusion in the loader makes the verifier check a different
file from the one that gets executed. See [docs/ROOT.md](docs/ROOT.md), which
explains this at length, because it is the single most important thing to
understand about the project.

## Trademarks

**DIRECTV, HR54, Pace, Broadcom, Jellyfin, and all other product, company, and
project names referenced in this repository are the trademarks of their
respective owners.** They are used here for identification only, to describe
the hardware and software this project interoperates with.

Their use in this repository is **not** a claim of endorsement, sponsorship,
affiliation, or partnership, and implies no endorsement by their owners of this
project or its author.

## Generated artwork

`assets/screensaver-hax0r.png` is original artwork created for this project. It
is **not** a DIRECTV asset. It is deliberately sized to the stock slot
(360×286) so it can be bind-mounted over it, and it is 8-bit RGBA where the
stock image is 8-bit colormapped — which is how you can tell them apart. The
stock DIRECTV artwork is not included.

## Security research context

This work was performed on hardware the author physically owned. It is
published to document a real vulnerability in a shipped product and to let
owners of that product understand and mitigate it on their own devices.

It is **not** published to enable piracy. Specifically:

- No paid-content decryption is performed or documented.
- No access control is bypassed. The DVR entitlement path was found to be
  closed and was **abandoned**, not circumvented — see
  [docs/DEAD-ENDS.md](docs/DEAD-ENDS.md).
- No tuner or subscriber authorization state is modified.
- No protected content is played. The appliance plays your own Jellyfin
  library.

If you are the maintainer of a product affected by the issue described in
[docs/ROOT.md](docs/ROOT.md) and want the details privately, the disclosure
path is preferable to a public issue.
