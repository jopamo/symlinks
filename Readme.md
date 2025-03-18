# Symlinks

**Scan and fix symbolic links**  

Symlinks is a simple command-line utility that scans directories for symbolic links, identifying and classifying them into categories such as relative, absolute, dangling, messy, lengthy, and other_fs. It can also fix these links by:

- Converting absolute links to relative (within the same filesystem).
- Removing unnecessary path components (e.g., `./` or repeated `../`).
- Deleting links that point to nonexistent targets.

## Features

- **Recursive Directory Search**: With `-r`, it can descend into subdirectories (optionally crossing filesystems with `-o`).  
- **Selective Fixing**: Use `-c` to convert or tidy links, and `-s` to detect or reduce unneeded `../`.  
- **Test Mode**: `-t` shows what changes would be made without actually modifying anything.  
- **Verbose Output**: `-v` reveals all links, including otherwise “harmless” relative ones.  

## Installation

### From Source

```bash
git clone https://github.com/your-repo/symlinks.git
cd symlinks
make
sudo make install
```

Or specify your own install prefix:

```bash
make PREFIX=/usr
sudo make PREFIX=/usr install
```

## Usage

- **Basic Scan**  
  ```bash
  symlinks -r /path/to/check
  ```
  Recursively list all non-relative symlinks, broken symlinks, etc.

- **Convert Absolute to Relative**  
  ```bash
  symlinks -rc /path/to/check
  ```
  Converts absolute links to relative (within the same filesystem), cleans up messy links, and displays changes.

- **Remove Dangling Symlinks**  
  ```bash
  symlinks -rd /path/to/check
  ```
  Recursively delete all links with nonexistent targets.

For a full list of options, run `symlinks -h` or see the man page (`man symlinks`).

## Credits

Created by **Mark Lord** (<mlord@pobox.com>).

Contributions and bug reports are welcome.

## License

Symlinks is distributed under the terms of the [MIT License](LICENSE) or another license of your choosing. See the `LICENSE` file for details.
```
