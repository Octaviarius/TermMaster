# Framing and extraction

Transport reads are arbitrary chunks. Put a Framer before packet operations when messages can span reads.

Delimiter Framer accumulates until its ending or size rule matches. Zero configured byte count disables size-based splitting. Ending inclusion and empty frames are explicit options. Retain appropriate resource limits.

Timed Framer accumulates until the quiet interval between incoming chunks exceeds its configured milliseconds. This is a silence detector, not a periodic sampler.

Pattern Framer matches PCRE2-8 byte patterns and can build output from captures. Empty Output returns the full match. Named groups use `(?<payload>...)`; `${payload}` inserts one into Output. Numbered groups follow the order of opening parentheses, including nested groups. Literal text and byte escapes can surround substitutions.

[Pattern Framer parameters](help:processor.native.pattern-framer)
