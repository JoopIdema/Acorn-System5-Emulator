## Browser version

`web/index.html` is the same emulator running in a web browser (the core is compiled to WebAssembly). It is a single self-contained file (about 640 KB) with the ROMs and the two default disks built in, so you can just open it (double-click), or put it on your own web server. No installation needed. It needs a recent browser (Chrome, Edge, Firefox or Safari).

* **Same two machine models** and the same options as the Windows version: model, VDU 40x25 / 80x25, System BASIC and floating point ROM on/off, CPU 1 or 2 MHz, 80x25 colour, monitor frame, smoothing.
* **Keys:** `F9` = Break, `Shift+F9` = Space-Break (boot), `F8` = smoothing, `Ctrl+V` = paste, cursor keys as in the table above. (`F11` and `F12` are used by the browser, so other keys are used here.) Click the picture first so the keyboard goes to the emulator. Browsers keep some shortcuts (for example `Ctrl+W` closes the tab), so use the cursor keys.
* **Disks:** use **Mount...**, or drop a `.ssd` file on the picture (hold Shift for drive 1). Changes the emulated machine writes to a disk are stored in the browser (IndexedDB) and are still there after a reload. **Download** saves a disk as a `.ssd` file. **Reset saved data** forgets all settings and disk changes. Settings are stored in the browser as well (instead of `system5.ini`).
* **Sound** (custom machine, SID) starts after the first click or key press, because browsers do not allow sound before that. Printer output is collected and can be downloaded with the **Printer output** button.

