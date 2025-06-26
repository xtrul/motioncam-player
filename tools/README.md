# Tools

## `mcraw_thumbnail`

A simple command-line utility to generate PNG previews from `.mcraw` files.

### Building

```
mkdir -p build
cd build
cmake ..
make -j$(nproc)
```

This produces the `mcraw_thumbnail` executable in `tools/build/`.

### Usage

```
./mcraw_thumbnail <file.mcraw> [output.png]
```

- If `output.png` is omitted, a PNG with the same base name will be written next to the source file.
- The first frame of the `.mcraw` is demosaiced with a simple bilinear algorithm.

The tool does not register thumbnails with your operating system; run it manually to generate preview images.

### Batch Generation

`mcraw_batch_thumbs.sh` processes all `.mcraw` files in a folder:

```
./mcraw_batch_thumbs.sh <path/to/folder>
```

Make sure `mcraw_thumbnail` is built (see above) before running the script.
