# FunKey OS Port

This directory contains the experimental native OPK port for FunKey OS
firmware, targeting the FunKey S and RG Nano with the FunKey SDK 2.3
toolchain and SDL 1.2.

## Build

```bash
FUNKEY_SDK_DIR=/path/to/FunKey-sdk-2.3.0 ./funkey/package-opk.sh
```

The package script builds the vendored engine texture baker, cross-compiles
ThumbyRogue, and writes the OPK to `dist/funkey/ThumbyRogue.opk` by default.
Set `OPK_OUT` to override the output path.

## Install

Copy `dist/funkey/ThumbyRogue.opk` to the SD card folder your launcher scans
for OPKs, then launch **ThumbyRogue**.

Saves and logs are stored under `/mnt/FunKey/.thumbyrogue`. Set
`THUMBYROGUE_HOME` in the launcher environment to override that directory.

## Controls

```text
D-pad       move
A           confirm / attack
B           cancel / secondary action
X or R      rotate / dodge modifier
Y or L      inventory / alternate action
START       menu
POWER/Q     save and quit
```
