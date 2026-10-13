# ADR-0004: The panel is one Canvas, skins are data (JSON)

Status: accepted.

**Context.** The six layouts rearrange four blocks; the keyboard stays the same and spans the full width.
Multitouch "hold a button + play keys" is mandatory, plus hints and LEDs on top of every element.

**Decision.** The whole panel is one Compose `Canvas` with one pointer handler and geometric hit-testing against
the layout computed from the JSON skin (a row/column tree with weights, unit u = height/440). The settings menu uses
regular Compose components.

**Consequences.** + skins are added without code, multitouch is predictable, drawing is cheap (no 60 composables);
− accessibility (TalkBack) has to be done by hand through region semantics (later).
