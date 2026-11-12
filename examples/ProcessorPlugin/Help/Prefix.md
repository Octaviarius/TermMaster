# Prefix processor example

`sample.prefix` prepends the configured UTF-8 prefix to each complete incoming
byte packet. The default prefix is `[sample] `. Binary payload bytes, including
NUL, remain unchanged. An empty input still produces one prefixed packet.
With an empty prefix, an empty input produces an explicit empty output packet.

Enable **Prefix processor tutorial** in the plugin manager, then add
**Examples / Prefix packet (example)** in Lineflow. Edit **Prefix** through the
generated settings form. The processor has one input and one output and does
not connect devices or interpret terminal escape sequences.

Output must fit the configured byte limit. An oversized packet or invalid port
is rejected without callbacks. Cancellation permanently stops an instance;
create a new instance to restart. Prepared factories and live instances prevent
plugin removal until they are released and cancellation is acknowledged.

See [First custom processor](help:developer.processor) for build and test commands.
