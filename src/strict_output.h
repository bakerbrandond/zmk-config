#pragma once

#include <zmk/endpoints_types.h>

// The output the user selected (USB or Bluetooth). With strict output on,
// reports go only there, even while it isn't connected.
enum zmk_transport strict_output_selected(void);
