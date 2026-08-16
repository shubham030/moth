# mothc in five commands

```console
$ dart pub global activate mothc     # installs `moth` and `mothc`

$ moth create hello                  # an editor-ready project
$ cd hello

$ moth run                           # board auto-selected; simulator if none
Launching app.dart on /dev/cu.usbmodem2101

pushed in 174ms
r  hot restart (recompile + push; state resets)   h  this help   q  quit

$ moth check app.dart                # subset errors on every save
app.dart: ok

$ mothc app.dart --push 192.168.x.x:7621 --token   # one-shot push over WiFi
```

Edit `app.dart`, press `r` in the running session, and the board is drawing
your new code in about 173ms — no reflashing. See
[getting started](https://shubham030.github.io/moth/docs/getting-started)
for the whole path from nothing to a program on a panel.
