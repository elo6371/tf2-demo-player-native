# Native entity state diagnostics

The packet message walker now follows the Source demo format: each packet
message has a six-bit type and consumes its own schema; there is no generic
message-length field.

The native probe separates an entity update that cannot be applied because its
prior class/state is unavailable (`entity_unknown_state_failures`) from actual
SendProp failures (`entity_prop_index_failures`, `entity_prop_value_failures`)
and missing schemas. This matters when scanning a demo without replaying its
initial signon state: Preserve updates are then expected to be unresolvable,
but must not be reported as malformed wire data.

Observed after the packet-boundary fix:

- `73.dem`: `malformed_packets=1`, `temp_events=129815`, `temp_failures=0`,
  `entity_prop_index_failures=0`, `entity_prop_value_failures=0`.
- `autorecord_2026-05-14_16-09-14.dem`: `malformed_packets=3`,
  `temp_events=33190`, `temp_failures=14970`,
  `entity_unknown_state_failures=58795`, `entity_prop_index_failures=8`,
  `entity_prop_value_failures=0`.

The remaining state failures require a signon/entity-baseline replay path; they
are not evidence that the packet message boundary is malformed.
