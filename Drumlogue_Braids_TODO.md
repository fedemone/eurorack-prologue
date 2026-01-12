# Drumlogue Braids Port - TODO List

This file tracks the step-by-step patches required to minimize and optimize the Braids-only Drumlogue port, following the boochow/eurorack_drumlogue (Lillian) minimal approach.

## TODO Steps

1. Remove all non-Braids engines, resources, and code.
2. Match build config and optimization flags to boochow/Lillian.
3. Only generate/include Braids resources.
4. Limit parameters to those in Lillian, with matching order/range.
5. Enable all binary size optimizations and strip the final binary.
6. Ensure header and manifest are minimal and only reference Braids.

---

Each step should be implemented as a focused patch. When you ask to resume, I will retrieve and execute the next step in this list.
