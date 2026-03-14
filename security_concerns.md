/dev/input Access Explained
/dev/input/event* are Linux kernel device files that represent every input device — keyboards, mice, touchpads, game controllers. When whisper-typer reads these files to detect your global hotkey, it can technically see every keystroke from every keyboard, including passwords, credit card numbers, etc. 

  Why it's needed: There's no other way to capture global hotkeys on Linux outside of a desktop
  compositor. X11's XGrabKey is an alternative but doesn't work on Wayland. evdev is the universal
  approach.

  Why it's a concern: Any program in the input group has keylogger-level access. This is an inherent
  trade-off — not a bug in whisper-typer, but a property of how Linux input works. Every global hotkey
  tool (keyd, sxhkd, espanso) requires the same access.

What does it mean that Any program in the input group has keylogger-level access? what programs? why running this app then becomes a risk? does it expose key logs? if its 100% local, whats the risk?



