# How to build

### Packages to install

git zip unzip build-essential ccache bc libncurses5-dev flex bison python-is-python3 ca-certificates

### Build command

`./build_kernel.sh <device> <build DTB> <debug mode>`

Flags:

- `<device>`: Can be `a10`, `a20`, `a30`, `a30s`, `a40`
- `<build DTB>`: Leave empty if you don't want DTB to be built, otherwise `dtb`
- `<debug mode>`: Prints every command executed; leave empty if normal, otherwise `x`
