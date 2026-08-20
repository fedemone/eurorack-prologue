/*
 * File: drumlogue_param_route.h
 *
 * Where each panel parameter goes.
 *
 * The drumlogue hands the wrapper a parameter id and a value; the oscillator
 * expects an OSC_PARAM index and a value in its own units. That mapping used
 * to be a switch in drumlogue_unit_wrapper.cc — one case per parameter per
 * unit, around four hundred lines — sitting a file away from the descriptors
 * in header.c that give the same parameters their ranges and names. Two lists
 * of the same thing, kept in step by hand.
 *
 * They are one list now. header.c holds the route table immediately above the
 * descriptors it belongs to, the wrapper looks the id up, and `.num_params`
 * is the route table's length rather than a number anyone has to remember to
 * change. Renumbering a panel is one edit in one place.
 *
 * `make test-param-routing` is what makes that safe: it dumps every
 * parameter's destination for every unit and diffs it against
 * docs/param_routing.txt, which was captured from the switch before it was
 * replaced. A slip in a table like this does not crash and does not fail a
 * range check — it sends a knob somewhere else and waits to be found by ear.
 */

#ifndef DRUMLOGUE_PARAM_ROUTE_H_
#define DRUMLOGUE_PARAM_ROUTE_H_

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
  /* Not routed anywhere. Zero, so a parameter the table forgets is inert
   * rather than pointed at OSC_PARAM index 0. */
  k_route_none = 0,

  /* Kept in the wrapper: the note the trigger pad plays, and the rate of the
   * LFO the wrapper runs in place of the host's shape LFO. */
  k_route_base_note,
  k_route_lfo1_rate,

  /* Forwarded to OSC_PARAM at `osc`, with the value transformed: */
  k_route_osc,           /* as-is */
  k_route_osc_10bit,     /* 0-100 percent  -> 0-1023, what shape/shiftshape take */
  k_route_osc_signed24,  /* 0-48 semitones -> -24..+24, as a uint16 two's complement */
  k_route_osc_double,    /* value * 2, for the ports reading a half-scale knob */

  /* Forwarded to clouds_fx_set_param() at `osc`. The FX unit has no
   * oscillator behind it, so it takes its own ids; the route is the identity
   * and exists so this unit's .num_params comes from a table like the rest. */
  k_route_fx,
} unit_param_route_kind_t;

typedef struct {
  uint8_t kind; /* unit_param_route_kind_t */
  uint8_t osc;  /* OSC_PARAM index, for the k_route_osc* kinds */
} unit_param_route_t;

/* Defined in header.c, next to the descriptors. Hidden, because every unit
 * defines it and the drumlogue loads them all into one dynamic scope — see
 * the note on unit_header_own in drumlogue_guards.h. */
extern const unit_param_route_t unit_param_routes[]
    __attribute__((visibility("hidden")));

#ifdef __cplusplus
}
#endif

#endif /* DRUMLOGUE_PARAM_ROUTE_H_ */
