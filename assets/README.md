# Termaster icon variants

## Logo identity

The Termaster logo is a jellyfish wearing a sombrero with exactly three tentacles.
The broad brim, peaked crown and three flowing tentacles define its recognizable
silhouette. Color, material and rendering style may vary for different contexts;
no single illustrative treatment is required.

[Logo exploration sheet](LogoExploration.png) preserves sixteen numbered visual
treatments, including flat graphics, pixel art, realism, clay, ink and 3D styles.
It is retained as a design reference. Treatment 16 is the basis of the current
orange application icon. The retained blue arrow/T icon is an earlier abstract
interpretation of this identity.

The sheet was generated with the built-in imagegen tool using the prompt intent:
sixteen concise jellyfish-in-sombrero variations on one 4-by-4 sheet, spanning
realism through pixel graphics, each retaining exactly three tentacles.
The copied PNG was decoded successfully and verified byte-for-byte against the
generated original; its sixteen treatments were visually reviewed.

## Character exploration

[Stoic mascot](MascotStoic.png) preserves a humorous Chuck Norris-inspired
interpretation: a sombrero, narrowed blue eyes and a graphic russet beard
integrated into the jellyfish bell, with exactly three tentacles. It is an
illustrative exploration, not a replacement for the application icon.

Generated with the built-in imagegen tool. Prompt intent: a cohesive simplified
jellyfish character with an understated action-hero expression, smooth graphic
beard patch, orange/navy palette and transparent background. The PNG was decoded
and verified byte-for-byte against the generated original.

## Application icon assets

The orange variant is the application icon: App embeds the PNG for Qt window
icons and the Windows executable embeds the multi-resolution ICO. The blue
variant is retained without an assigned runtime mode.

- [Bright blue](IconBlue.png): bright sky-blue mark on dark navy.
- [Orange](IconOrange.png): orange sombrero and three tentacles with a charcoal
  hatband and bell, on a transparent background without a tile.

Windows icon containers: [Blue](IconBlue.ico) and [Orange](IconOrange.ico).
Each ICO contains 16, 24, 32, 48, 64, 128 and 256 pixel square images with alpha
transparency, resized from its corresponding PNG using Pillow.

Both PNGs are 1254 by 1254 pixels with RGBA transparency. The orange icon is the
selected jellyfish-in-sombrero design; the blue icon retains the earlier abstract
arrow/T design on a navy tile. These are generated raster assets, not vector
masters.

## Generation

Created with the built-in imagegen tool. Final prompt intent:

- Bright blue: recolor the selected wide T-arrow and three-stream icon to bright,
  saturated sky blue (target `#50C8FA`); preserve the dark navy tile, geometry and
  transparent exterior.
- Orange: extract treatment 16 from the exploration sheet, refine it into a
  standalone minimalist orange-and-charcoal jellyfish in a sombrero with exactly
  three tentacles, clean fills and transparent background; remove the label and
  background tile.

Color values are prompt targets, not guaranteed exact pixel values.

## Verification

Both files were visually inspected and decoded successfully with Pillow. Their
square dimensions match and both contain transparent and opaque pixels.
Both ICO files were reopened and all seven embedded sizes decoded successfully.
The current orange PNG was visually reviewed and its ICO regenerated from it.
This asset replacement was not followed by a fresh application build or live GUI
check; the integration verification below describes the earlier asset integration.
The orange variant is now integrated into Qt window resources and the Windows
executable. The embedded native images were verified against the source ICO;
see [application integration](../docs/application.md#application-icon).
