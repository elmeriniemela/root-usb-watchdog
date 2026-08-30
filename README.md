# root-usb-watchdog

`root-usb-watchdog` powers off a Linux computer immediately when the physical
block device below its dm-crypt root mapping disappears. It is intended for a
portable Linux installation whose encrypted root filesystem is stored on a USB
drive.

The watchdog is a small, fully static C program. After resolving the kernel
sysfs path of the one block device below the mapping, it locks itself into RAM
and polls that path without launching commands or loading shared libraries.

## Important warning

This is an emergency hard power-off, not a clean shutdown. Removing the root
USB has already made an orderly root-filesystem sync impossible. Unsaved data
is lost, and other writable storage attached to the computer may be left in an
inconsistent state. The watchdog does not wipe RAM and cannot guarantee that
every computer firmware will complete a Linux power-off request.

The watchdog monitors only the device below the configured mapping. Removing
an unrelated USB device does not trigger it. It deliberately refuses to arm
when the mapping has zero or multiple backing devices.

## Build

Requirements are a C compiler, GNU make, glibc static libraries, `ldd`, and
`systemd-analyze`. On Arch Linux these are supplied by `base-devel`, `glibc`,
and `systemd`.

```bash
make
make test
```

The executable is written to `build/root-usb-watchdog`. The default build is a
static x86-64 Linux executable and uses no dynamic libraries at runtime.

Create a versioned release binary and checksum with:

```bash
make release VERSION=0.1.0
```

This creates:

```text
dist/root-usb-watchdog-0.1.0-x86_64-linux
dist/root-usb-watchdog-0.1.0-x86_64-linux.sha256
```

## Making a release

Choose a version without a leading `v`, for example `0.2.0`. Update that
version in both of these files:

- `root-usb-watchdog.c`: change `PROGRAM_VERSION`.
- `Makefile`: change the version checked by the `release` target and its error
  message.

The workflow does not need to be changed. It accepts a Git tag with a leading
`v` and removes the prefix before passing the version to `make`.

Build and test the release locally before tagging it:

```bash
make clean
make release VERSION=0.2.0
sha256sum --check dist/root-usb-watchdog-0.2.0-x86_64-linux.sha256
```

Commit the version change, push it, then create and push an annotated tag that
points to that commit:

```bash
git add root-usb-watchdog.c Makefile
git commit -m "Release v0.2.0"
git push
git tag -a v0.2.0 -m "Release v0.2.0"
git push origin v0.2.0
```

On GitHub, open **Releases**, choose **Draft a new release**, select the pushed
`v0.2.0` tag, and publish the release. Publishing triggers
`.github/workflows/release.yml`. The workflow builds and tests the static
x86-64 Linux executable, then attaches these files to the GitHub Release:

```text
root-usb-watchdog-0.2.0-x86_64-linux
root-usb-watchdog-0.2.0-x86_64-linux.sha256
```

If the version in the tag does not match `PROGRAM_VERSION` and the Makefile
check, the workflow fails instead of publishing incorrectly named files.

## Command line

The default mapping is `/dev/mapper/cryptroot`:

```bash
sudo ./build/root-usb-watchdog
```

Specify another mapping explicitly:

```bash
sudo ./build/root-usb-watchdog --device /dev/mapper/cryptroot
```

Resolve and print the physical backing-device sysfs path without arming:

```bash
./build/root-usb-watchdog --check --device /dev/mapper/cryptroot
```

`--check` is safe: it never requests power-off. Armed mode must run as root so
it can lock its memory and invoke the kernel power-off operation.

## systemd installation

Review the source and unit before installation, then run:

```bash
sudo install -Dm0755 build/root-usb-watchdog \
  /usr/local/sbin/root-usb-watchdog
sudo install -Dm0644 root-usb-watchdog.service \
  /etc/systemd/system/root-usb-watchdog.service
sudo systemctl daemon-reload
sudo systemctl enable --now root-usb-watchdog.service
```

Check that the service resolved the intended backing device:

```bash
systemctl status root-usb-watchdog.service
journalctl -u root-usb-watchdog.service
```

Do not perform the physical-removal test until work is saved and any other
writable storage is safely unmounted. First confirm that removing an unrelated
USB device has no effect. Then, on a disposable installation, remove the root
USB and confirm that the machine powers off immediately.

## Security model

The service runs as root and holds only the capabilities needed to lock memory
and request power-off. The example systemd unit makes the filesystem read-only
to the service, hides home directories, and enables additional systemd
hardening. A future installer should download a pinned release, verify a pinned
SHA-256 digest before installation, and never execute an unverified download.

The watcher handles a conventional single-device dm-crypt mapping. RAID, LVM
layouts with multiple physical slaves, graceful shutdown after removal, and
non-Linux systems are outside its scope.

## License

MIT. See `LICENSE`.
