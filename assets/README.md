# Termaster icon variants

Two selected raster assets are retained for possible use in different application
modes. Mode assignments and runtime integration are not implemented yet.

- [Bright blue](IconBlue.png): bright sky-blue mark on dark navy.
- [Orange](IconOrange.png): warm orange mark on dark navy.

Both PNGs are 1254 by 1254 pixels with RGBA transparency outside the rounded tile.
The shared design combines three incoming streams, an upward arrow and a T
crossbar. These are generated raster assets, not vector masters; minor pixel and
geometry differences between variants remain possible.

## Generation

Created with the built-in imagegen tool. Final prompt intent:

- Bright blue: recolor the selected wide T-arrow and three-stream icon to bright,
  saturated sky blue (target `#50C8FA`); preserve the dark navy tile, geometry and
  transparent exterior.
- Orange: recolor the bright-blue counterpart to warm orange (target `#FF8A24`);
  preserve the same geometry, dark navy tile and transparent exterior.

Color values are prompt targets, not guaranteed exact pixel values.

## Verification

Both files were visually inspected and decoded successfully with Pillow. Their
square dimensions match and both contain transparent and opaque pixels. No
application behavior changed; no build or device verification was needed.
