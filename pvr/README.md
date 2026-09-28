# HR54 PVR catalogue

The live `dvr_core` service owns `/var/viewer` (`--recordingLibrary /var/viewer`); the XFS realtime device holds recording extents. This cataloguer never reads or modifies those extents.

```sh
python3 tools/pvr_catalog.py live-20260927/extracted --library ../userland-mods/755/logs/recordingdt-library-detail.txt --json recordings.json --tsv recordings.tsv
```

`recording_id` is the vendor index-directory identifier. `uuid`, `recid`, and `longid` originate in the stock recording library and are retained for a future stock-API playback wrapper; they are not content keys.
