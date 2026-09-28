# Signal-state experiment

No persistent signal spoof was applied. The only live injection was `dt simSignalLock -tunerNum 0 -session local`. It returned `Sent satellite signal lock event for tuner 0`, while the current OSD remained `36-775` and screen ID remained `2320` after three seconds. The actual failing provider was SWM detection (`swm_not_detected`), so a tuner-lock event does not alter the diagnostic result. Future work should target the local SWM status provider or diagnostic consumer if dismissal stops being stable; do not alter account authorization.
