# Termaster icon variants

The orange variant is the application icon: App embeds the PNG for Qt window
icons and the Windows executable embeds the multi-resolution ICO. The blue
variant is retained without an assigned runtime mode.

- [Bright blue](IconBlue.png): bright sky-blue mark on dark navy.
- [Orange](IconOrange.png): warm orange mark on dark navy.

Windows icon containers: [Blue](IconBlue.ico) and [Orange](IconOrange.ico).
Each ICO contains 16, 24, 32, 48, 64, 128 and 256 pixel square images with alpha
transparency, resized from its corresponding PNG using Pillow.

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
Both ICO files were reopened and all seven embedded sizes decoded successfully.
The orange variant is now integrated into Qt window resources and the Windows
executable. The embedded native images were verified against the source ICO;
see [application integration](../docs/application.md#application-icon).
