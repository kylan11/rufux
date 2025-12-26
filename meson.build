project('rufux', 'c',
  version: '0.1.0',
  license: 'GPL-3.0-or-later',
  default_options: [
    'c_std=gnu11',
    'warning_level=2',
  ],
)

gnome = import('gnome')

# Dependencies
gtk4_dep = dependency('gtk4', version: '>= 4.10')
libudev_dep = dependency('libudev')
libblkid_dep = dependency('blkid')
libfdisk_dep = dependency('fdisk')
threads_dep = dependency('threads')
openssl_dep = dependency('openssl')

# Source files
src_files = files(
  'src/main.c',
  'src/platform/platform.c',
  'src/device/device.c',
  'src/disk/partition.c',
  'src/disk/disk_io.c',
  'src/format/format.c',
  'src/iso/iso_analyzer.c',
  'src/iso/iso_extract.c',
  'src/iso/iso_writer.c',
  'src/ui/app.c',
  'src/ui/window.c',
  'src/ui/widgets.c',
  'src/common/utils.c',
  'src/common/hash.c',
)

# Compile resources
resources = gnome.compile_resources(
  'rufus-resources',
  'data/rufus.gresource.xml',
  source_dir: 'data',
)

executable('rufux',
  src_files,
  resources,
  dependencies: [
    gtk4_dep,
    libudev_dep,
    libblkid_dep,
    libfdisk_dep,
    threads_dep,
    openssl_dep,
  ],
  install: true,
)

# Install desktop file and icons
install_data('data/org.rufus.linux.desktop',
  install_dir: get_option('datadir') / 'applications',
)

install_data('data/org.rufus.linux.metainfo.xml',
  install_dir: get_option('datadir') / 'metainfo',
)
