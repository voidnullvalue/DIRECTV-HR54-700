# Observed recording metadata

An index directory is named `Rcrd-MM-DD-YYYY-HHMM-SS-<token>TransportMPEG-DIRECTV_A3_MPEG4_AC3-ch<channel>[-ad].mpg` under `/var/{backup/,}viewer/indexfile/`.

It holds `meta_man.xma`, `xmd`, `xmi`, and `xmv`. `xmd` identifies `Metadata Indexer XTV5.4.2.141`; the other payloads are binary rather than plain XML. The corresponding recording extents are under `/var/viewer/segments` on the realtime device.

The stock read-only `dt library -detail` output supplies human-readable title, description, programme identifiers, watch state, visibility, and copy-policy fields. The cataloguer matches it to an index directory by channel and timestamp; this snapshot has a five-hour index-name to `RecStartTime` offset.
