# TI-92 Brainwasher firmware
This part of the repo contains, the firmware running on the Brainwasher bluepill.

The HAL code was genereted using the STM32CubeMX software, provided by ST.

## How to use
To put the firmware on the board, you'll first need to compile it. You'll need `make` and the `gcc-arm-none-eabi` toolchain installed.

Then build the firmware
```bash
cd firmware
make
```

After that you can send the firmware to the board using a ST-Link connected to the 4 debug pins on the bluepill. You'll need `st-flash`
```bash
st-flash write build/brainwasher-5000.bin 0x8000000
```
Note that you might be able to do it using the GUI app from ST, but I never tried.

You can finally connect the board using USB and use the PC software to connect to it.

## License
This part of the repo is licensed under the Beerware License, you can find more information in LICENSE.txt
