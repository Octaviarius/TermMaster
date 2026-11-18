# Lineflow editor

Choose a processor from the left palette, place it in the center sequence and configure its occurrence on the right. Drag operations to reorder. A unary node occupies one lane; a duplex node spans both. Port labels identify inputs/outputs. Reversing a duplex node rotates the port mapping by 180 degrees.

Blocks have unique catalog IDs and mutable names/groups. Nodes have unique names within a block. The first occurrence uses its plain name; later occurrences receive a spaced numeric suffix. Reusable blocks may include other blocks with bounded depth and cycle validation.

Apply commits drafts. Reusable blocks are separate workspace YAML files. A terminal-local Inplace definition stays in that terminal session configuration as one paired diagram.

Terminals select Bypass or one paired Lineflow. An unused lane passes packets unchanged. Reusable one-line blocks remain available inside either lane. Catalogs filter by topology. Select a node and press F1 or Help for its compiled reference. Missing plugin documentation falls back to its metadata/schema without instantiation.

Initial/Device and Final/Terminal boundary controls select recording settings without adding palette entries. [Boundary recording](help:processing.boundaries) distinguishes raw bytes from rendered terminal lines.

Double-click a composition, or press its compact editor button, to open its editor. [Tap](help:processing.tap) processes copies in a private single-line branch while forwarding original packets unchanged.
