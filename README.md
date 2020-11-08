# PBSync

A CLI utility for syncing book positions between [PocketBook 740](https://pocketbook.ru/shop/ustroystva/pocketbook-740-chernyy/) e-reader and [zathura](https://pwmt.org/projects/zathura/) document viewer.

## Building from source

Install Qt libraries and use:

```bash
cmake .
make -j$(nproc)
```

## Usage

```
Usage: ./pbsync [options] mount-point zathura-history prefix
A CLI utility for syncing book positions between PocketBook e-reader and zathura.

Options:
  -h, --help     Displays help on commandline options.
  --help-all     Displays help including Qt specific options.
  -v, --version  Displays version information.

Arguments:
  mount-point     PocketBook mount point: path with "system" directory.
  zathura-history Zathura history file.
  prefix          Prefix to books location on this device.
```

Here is an example of usage in my environment:

```
$ pbsync /media/jubnzv/PB740 /home/jubnzv/.local/share/zathura/history /home/jubnzv/

Updated positions:
 "/home/jubnzv/Documents/C++/Stepanov_ From Mathematics to Generic Programming.pdf" page: 103

New zathura history was saved at /home/jubnzv/.local/share/zathura/history.new

Check the difference using:
 diff /home/jubnzv/.local/share/zathura/history{,.new}

To apply these changes use:
 mv /home/jubnzv/.local/share/zathura/history{,.new}
```

I store books in `~/Documents` on my desktop and keep this directory with all files on my PocketBook device.

## License
MIT
