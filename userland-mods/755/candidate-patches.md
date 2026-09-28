# Candidate patch order

1. First capture the exact OSD class/ID and current screen ID using a read-only Druid diagnostic; then invoke the existing `gotoPlaylistScreen`/playlist action through the local `uconntest` XML dispatcher if exposed.
2. If normal navigation is denied solely by the modal OSD, test a reversible OSD dismissal/navigation command. Do not alter authorization or satellite state.
3. Only if no dispatcher action exists, bind-mount one copied Druid artifact from `/var/opt/hr54/overlays` after hashing the original. No patch is approved yet.

Rollback for any future overlay is `umount <vendor-target>`; no flash write is involved.
