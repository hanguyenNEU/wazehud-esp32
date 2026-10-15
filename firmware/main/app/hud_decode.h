#pragma once

#include "hud_state.h"
#include <stddef.h>

/* Decode một dòng HLP đã được hlp_core ghép hoàn chỉnh.
   Trả true khi đó là message `s` hợp lệ và `state` đã được dựng lại đầy đủ.
   Với mọi trường hợp khác (JSON hỏng, `v` khác 1, loại message khác) trả false
   và KHÔNG động vào `state`. */
bool hud_decode_state(const char *line, size_t length, hud_state_t *state);
