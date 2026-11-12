# Packet processing

A Processor accepts an owned byte packet and can publish zero, one or multiple packets, including asynchronous output. Invocation completion and later autonomous output are distinct. Cancellation must stop owned work before acknowledgement.

**Executors preserve packet boundaries.** Codecs normally process one invocation as a complete packet and never silently join transport reads. Use an explicitly configured Framer to establish messages.

- [Lineflow](help:processing.lineflow)
- [Framing](help:processing.framing)
- [Codecs and envelopes](help:processing.codecs)
- [Timestamp templates](help:processing.timestamp)
- [Parameter reference](help:processing.nodes)

Bypass passes unchanged packets. Bypass (Duplex) passes two independent lanes. Stub drops traffic; Gate optionally suppresses packets and defaults to open.
