# 775 landing text patch

The stock English resource is `/opt/ui_assets/assetspack/language/englishtext.txt`. Its 775-specific entries are `S2412` (title) and `S2636` (dish communication message). `build-775-text.py` replaces exactly these entries with `Press Menu to begin` and refuses to run if either original entry is absent or duplicated. The host-prepared copy has SHA256 `6c2a62bb99965d4b31a02bfba91aa3cf2b39df34e054401787523652eef8ae72`; the extracted stock file has SHA256 `66c858dcb8ac8015bf22436752a82e8181770294c2f1c5e9d997c3185cbf54df`.

The receiver's stock resource matched the extracted file (MD5 `79422eec9924fb208c99f80a5318c5ed`). The modified copy was placed at `/var/hr54-persist/overlays/englishtext-775.txt` (MD5 `4d2b12d5459692cf7cb2391a3695904e`) and successfully bind mounted over the vendor file. The mounted file showed the two changed entries. The boot image had SHA256 `ed2e8bb7761d0ff95dc44083e480f8a196a987d22d659b128d941a8590e3b315` and receiver/host MD5 `9f9b493b4e5df5180605b0024c781fcd`. The user reported the old dish message after reboot, so this **did not solve** the visual issue.

The user redirected work to Jellyfin. The overlay was unmounted and `/var/hr54-persist/overlays/englishtext-775.txt` was deleted; the live stock file MD5 again matches `79422eec9924fb208c99f80a5318c5ed`. The current asset-7 hook checks for that file before mounting, so its leftover mount branch is inert. Automatic 775 dismissal remains disabled. `hr54-play-url` clears the overlay only when playback is invoked.

Rollback: `umount /opt/ui_assets/assetspack/language/englishtext.txt` for the live session; for persistent rollback, atomically restore `/var/hr54-persist/backup/7_6933_6840-v3.squashfs` to `/var/network/plugins/7_6933_6840.squashfs` and reboot. The underlying vendor SquashFS text file was never changed.

The latest user request treats the existing 775 panel as an acceptable landing screen if it prompts Menu. Once the new text is visible across reboot and Menu opens, the 775 task is complete. The prior automatic dismissal policy should then be disabled, so it does not hide the landing prompt.
