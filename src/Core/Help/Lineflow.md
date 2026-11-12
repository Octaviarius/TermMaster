# Lineflow editor

Choose a processor from the left palette, place it in the center sequence and configure its occurrence on the right. Drag operations to reorder. A unary node occupies one lane; a duplex node spans both. Port labels identify inputs/outputs. Reversing a duplex node rotates the port mapping by 180 degrees.

Blocks have unique catalog IDs and mutable names/groups. Nodes have unique names within a block. The first occurrence uses its plain name; later occurrences receive a spaced numeric suffix. Reusable blocks may include other blocks with bounded depth and cycle validation.

Apply commits drafts. Reusable blocks are separate workspace YAML files. Terminal-local Inplace definitions stay in the terminal session configuration, independently for simplex Rx, simplex Tx and duplex.

Bypass disables configured terminal processing. Simplex selects separate Rx/Tx blocks; Duplex selects one shared block. Catalogs filter by topology. Select a node and press F1 or Help for its compiled reference. Missing plugin documentation falls back to its metadata/schema without instantiation.
