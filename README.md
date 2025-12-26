Rufux: Simple USB formatting utility for Linux
==========================================
![Rufus logo](https://raw.githubusercontent.com/pbatard/rufus/master/res/icons/rufus-128.png)

A Linux port of [Rufus](https://github.com/pbatard/rufus), the reliable USB formatting utility.

## Features

- Create bootable USB drives from ISO images
- Format USB drives to FAT32, NTFS, exFAT, or ext4
- Create MBR or GPT partition tables
- Safe defaults to prevent accidental data loss (filters USB-only devices)

## Current Behavior (v0.1.0)

- ISO mode uses a raw block write (`dd`) to the whole device.
- This works for hybrid Linux ISOs (e.g., most Ubuntu/Zorin/Fedora images).
- ISO file copy mode (UEFI only) is available when `xorriso`, `bsdtar`, or `7z` is installed.

## Known Limitations

- No "extract ISO contents" mode yet (file copy + bootloader install).
- ISO file copy does not install bootloaders; BIOS boot is not supported yet.
- Windows ISOs are not supported (no WIM handling or UEFI:NTFS).
- Target system selection does not change behavior yet.
- No persistence creation for Linux ISOs.

## Building

### Dependencies

```bash
# Debian/Ubuntu
sudo apt install meson ninja-build pkg-config \
    libgtk-4-dev libudev-dev libblkid-dev libfdisk-dev

# Fedora
sudo dnf install meson ninja-build pkg-config \
    gtk4-devel libudev-devel libblkid-devel libfdisk-devel

# Arch Linux
sudo pacman -S meson ninja pkgconf gtk4 systemd-libs util-linux
```

### Runtime Dependencies

```bash
# For formatting (required)
sudo apt install dosfstools ntfs-3g exfatprogs e2fsprogs

# For ISO file copy mode (optional, pick one)
sudo apt install xorriso  # or bsdtar (libarchive-tools) or p7zip-full
```

### Build

```bash
meson setup build
meson compile -C build
```

### Run

```bash
./build/rufux
```

## License

GPL-3.0-or-later

Based on Rufus by Pete Batard.
