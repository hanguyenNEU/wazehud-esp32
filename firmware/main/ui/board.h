#pragma once

/* LCDwiki ES3C28P — ILI9341V 2.8", SPI 4 dây, cảm ứng FT6336G.
 * Panel gốc 240×320 dựng đứng, xoay sang nằm ngang thành 320×240.
 *
 * Chỉ chứa độ phân giải, không kéo theo header nào của ESP-IDF — nhờ vậy
 * simulator SDL trên máy tính dùng chung được file này.
 */

#define DISPLAY_W 320
#define DISPLAY_H 240
/* Vùng hiển thị hiệu quả theo tài liệu: 43,20 x 57,60 mm ở chiều dọc,
   xoay ngang thành 57,60 x 43,20. Bước điểm ảnh 0,18 mm → 141 PPI. */
#define DISPLAY_MM_W 57.6f
#define DISPLAY_MM_H 43.2f
