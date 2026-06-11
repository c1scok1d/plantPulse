# Firmware dev setup (ESP-IDF, ESP32-S3)

How to build, flash, and monitor the PlantPulse firmware on this host. Target chip
is **esp32s3**. This box has ESP-IDF **v5.3.2** checked out at `~/esp/esp-idf`.

## One-time toolchain install

```bash
cd ~/esp/esp-idf
./install.sh esp32s3        # downloads the xtensa-esp32s3 toolchain into ~/.espressif
```

## Per-shell activation

`idf.py` is not on `PATH` by default — source the export script in each new shell:

```bash
. ~/esp/esp-idf/export.sh
idf.py --version            # confirm it's active
```

## Build / flash / monitor

```bash
cd ~/Desktop/plantPulse/firmware
idf.py set-target esp32s3   # only on a fresh build/ (sdkconfig is gitignored)
idf.py build
idf.py -p <PORT> flash monitor   # Ctrl-] to exit the monitor
idf.py fullclean            # when CMake/config gets wedged
```

The ESP32-S3 uses **native USB-CDC**, so the port is typically `/dev/ttyACM0`.
Confirm the actual node after plugging in:

```bash
ls /dev/ttyACM* /dev/ttyUSB*
```

## Serial port permissions

This user is **not** in the `dialout` group, so opening the serial port needs one
of:

- **Persistent (preferred):** `sudo usermod -aG dialout $USER` — then log out/in
  (group membership is applied at login). After that, `idf.py flash` works without
  sudo.
- **One-off:** run the flash/monitor under sudo, sourcing export first:
  `sudo -E env "PATH=$PATH" idf.py -p /dev/ttyACM0 flash monitor`

## Notes

- A **provisioned** board deep-sleeps for 8 h, so its USB-CDC won't enumerate
  while asleep — press reset (or the GPIO6 button) to wake it. An **unprovisioned**
  board stays awake advertising BLE as `Plant Pulse <last-4-of-MAC>`.
- IDF v5.3.2 may differ from the version the committed `sdkconfig` was generated
  with; if the build complains, `idf.py set-target esp32s3` regenerates it from
  `sdkconfig.defaults`.
- See `CLAUDE.md` for the architecture and the BLE provisioning contract, and
  `docs/ROADMAP.md` for what to verify first.
