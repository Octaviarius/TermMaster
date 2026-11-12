# Timestamp templates

Timestamp generates one output packet per input. The template controls placement of time, packet and literal prefixes/suffixes. Preview shows the substituted result; colors appear only when actual output includes their escape sequences.

Named brace substitutions support formatting specifications. `datetime` uses its separate QDateTime format. Unix and steady clocks offer seconds and integer milli/micro/nanosecond variants; `index` is the packet counter. Use the exact slot names in the generated parameter reference.

Templates are parsed once. Unused slots avoid expensive updates; the packet index still advances. Double literal braces to escape them.

[Timestamp parameters](help:processor.native.timestamp)
