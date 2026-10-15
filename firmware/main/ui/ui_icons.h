#pragma once

#include "hud_state.h"

/* Icon lấy từ Material Symbols Rounded (Apache-2.0), đã trích ra font riêng
 * trong assets/font_icon_*.c. Mỗi codepoint viết dưới dạng chuỗi UTF-8 vì
 * lv_label nhận chuỗi chứ không nhận số. */

#define ICO_STRAIGHT     "\xEE\xAE\x95" /* U+EB95 straight            */
#define ICO_LEFT         "\xEE\xAE\xA6" /* U+EBA6 turn_left           */
#define ICO_RIGHT        "\xEE\xAE\xAB" /* U+EBAB turn_right          */
#define ICO_SLIGHT_LEFT  "\xEE\xAE\xA4" /* U+EBA4 turn_slight_left    */
#define ICO_SLIGHT_RIGHT "\xEE\xAE\x9A" /* U+EB9A turn_slight_right   */
#define ICO_SHARP_LEFT   "\xEE\xAE\xA7" /* U+EBA7 turn_sharp_left     */
#define ICO_SHARP_RIGHT  "\xEE\xAE\xAA" /* U+EBAA turn_sharp_right    */
#define ICO_UTURN_LEFT   "\xEE\xAE\xA1" /* U+EBA1 u_turn_left         */
#define ICO_UTURN_RIGHT  "\xEE\xAE\xA2" /* U+EBA2 u_turn_right        */
#define ICO_ROUND_LEFT   "\xEE\xAE\x99" /* U+EB99 roundabout_left     */
#define ICO_ROUND_RIGHT  "\xEE\xAE\xA3" /* U+EBA3 roundabout_right    */
#define ICO_FORK_LEFT    "\xEE\xAE\xA0" /* U+EBA0 fork_left           */
#define ICO_FORK_RIGHT   "\xEE\xAE\xAC" /* U+EBAC fork_right          */
#define ICO_RAMP_LEFT    "\xEE\xAE\x9C" /* U+EB9C ramp_left           */
#define ICO_RAMP_RIGHT   "\xEE\xAE\x96" /* U+EB96 ramp_right          */
#define ICO_ARRIVE       "\xEF\x81\xAE" /* U+F06E sports_score        */
#define ICO_FERRY        "\xEE\xBF\xB5" /* U+EFF5 directions_boat     */

#define ICO_POLICE       "\xEE\xBD\x96" /* U+EF56 local_police        */
#define ICO_CAM_SPEED    "\xEF\x91\xB0" /* U+F470 speed_camera        */
#define ICO_CAM_REDLIGHT "\xEE\x95\xA5" /* U+E565 traffic             */
#define ICO_WARNING      "\xEF\x82\x83" /* U+F083 warning             */
#define ICO_ACCIDENT     "\xEE\xAF\xB2" /* U+EBF2 car_crash           */
#define ICO_JAM          "\xEF\x91\xAF" /* U+F46F traffic_jam         */
#define ICO_CLOSED       "\xEF\x82\x8C" /* U+F08C block               */
#define ICO_SPEED_DROP   "\xEE\xA7\xA4" /* U+E9E4 speed               */
#define ICO_NO_PASSING   "\xEF\x82\x8F" /* U+F08F do_not_disturb_on   */
#define ICO_END_NO_PASS  "\xEF\x82\xBE" /* U+F0BE check_circle        */
#define ICO_RAILWAY      "\xEE\xA7\x91" /* U+E9D1 railway_alert       */
#define ICO_TOLL         "\xEE\xA3\xA0" /* U+E8E0 toll                */
#define ICO_STOPPED      "\xEE\xBF\xB7" /* U+EFF7 directions_car      */
#define ICO_NO_LINK      "\xEE\x86\xA9" /* U+E1A9 bluetooth_disabled  */

/* Mã rẽ lạ trả chuỗi rỗng — §4.1 yêu cầu không vẽ gì, tuyệt đối không đoán
 * thành một hướng khác. */
static inline const char *ui_turn_icon(int turn)
{
    switch (turn) {
    case HUD_TURN_CONTINUE:           return ICO_STRAIGHT;
    case HUD_TURN_LEFT:               return ICO_LEFT;
    case HUD_TURN_RIGHT:              return ICO_RIGHT;
    case HUD_TURN_SLIGHT_LEFT:        return ICO_SLIGHT_LEFT;
    case HUD_TURN_SLIGHT_RIGHT:       return ICO_SLIGHT_RIGHT;
    case HUD_TURN_SHARP_LEFT:         return ICO_SHARP_LEFT;
    case HUD_TURN_SHARP_RIGHT:        return ICO_SHARP_RIGHT;
    case HUD_TURN_U_TURN:             return ICO_UTURN_LEFT;
    case HUD_TURN_U_TURN_RIGHT:       return ICO_UTURN_RIGHT;
    /* VN đi bên phải nên vòng xuyến ngược chiều kim đồng hồ. */
    case HUD_TURN_ROUNDABOUT:         return ICO_ROUND_RIGHT;
    case HUD_TURN_ROUNDABOUT_LEFT:    return ICO_ROUND_LEFT;
    case HUD_TURN_ROUNDABOUT_RIGHT:   return ICO_ROUND_RIGHT;
    case HUD_TURN_KEEP_LEFT:          return ICO_FORK_LEFT;
    case HUD_TURN_KEEP_RIGHT:         return ICO_FORK_RIGHT;
    case HUD_TURN_EXIT_LEFT:          return ICO_RAMP_LEFT;
    case HUD_TURN_EXIT_RIGHT:         return ICO_RAMP_RIGHT;
    case HUD_TURN_ARRIVE:             return ICO_ARRIVE;
    case HUD_TURN_FERRY:              return ICO_FERRY;
    /* §4.1 nói rõ mã 19 phải vẽ hướng ĐI THẲNG qua vòng xoay, không phải icon
       vòng xoay chung. Material Symbols không có icon riêng cho tình huống này,
       nên dùng mũi tên thẳng cộng với số lối ra hiển thị kèm. */
    case HUD_TURN_ROUNDABOUT_STRAIGHT: return ICO_STRAIGHT;
    default:                          return "";
    }
}

/* Mã cảnh báo lạ (10..74 chưa biết) rơi về icon nguy hiểm chung — §4.2. */
static inline const char *ui_alert_icon(int alert)
{
    switch (alert) {
    case HUD_ALERT_POLICE:          return ICO_POLICE;
    case HUD_ALERT_CAMERA_SPEED:    return ICO_CAM_SPEED;
    case HUD_ALERT_CAMERA_REDLIGHT: return ICO_CAM_REDLIGHT;
    case HUD_ALERT_HAZARD:          return ICO_WARNING;
    case HUD_ALERT_ACCIDENT:        return ICO_ACCIDENT;
    case HUD_ALERT_TRAFFIC_JAM:     return ICO_JAM;
    case HUD_ALERT_ROAD_CLOSED:     return ICO_CLOSED;
    case HUD_ALERT_SPEED_DROP:      return ICO_SPEED_DROP;
    case HUD_ALERT_NO_PASSING:      return ICO_NO_PASSING;
    case HUD_ALERT_END_NO_PASSING:  return ICO_END_NO_PASS;
    case HUD_ALERT_RAILWAY:         return ICO_RAILWAY;
    case HUD_ALERT_TOLL_BOOTH:      return ICO_TOLL;
    case HUD_ALERT_STOPPED_VEHICLE: return ICO_STOPPED;
    case HUD_ALERT_BLOCKED_LANE: return ICO_CLOSED;
    case HUD_ALERT_EXPRESSWAY_EXIT: return ICO_RAMP_RIGHT;
    case HUD_ALERT_END_SPEED_RESTRICTION: return ICO_END_NO_PASS;
    case HUD_ALERT_END_ALL_PROHIBITIONS: return ICO_END_NO_PASS;
    case HUD_ALERT_NO_CAR: return ICO_CLOSED;
    case HUD_ALERT_NO_MOTORCYCLE: return ICO_CLOSED;
    case HUD_ALERT_NO_LEFT_TURN: return ICO_LEFT;
    case HUD_ALERT_NO_RIGHT_TURN: return ICO_RIGHT;
    case HUD_ALERT_NO_U_TURN: return ICO_UTURN_LEFT;
    case HUD_ALERT_NO_STRAIGHT: return ICO_STRAIGHT;
    case HUD_ALERT_MANDATORY_STRAIGHT: return ICO_STRAIGHT;
    case HUD_ALERT_MANDATORY_RIGHT: return ICO_RIGHT;
    case HUD_ALERT_MANDATORY_LEFT: return ICO_LEFT;
    case HUD_ALERT_ONE_WAY: return ICO_STRAIGHT;
    case HUD_ALERT_PROHIBITED_ROAD: return ICO_CLOSED;
    case HUD_ALERT_CAMERA_PHONE: return ICO_CAM_SPEED;
    case HUD_ALERT_CAMERA_DUMMY: return ICO_CAM_SPEED;
    case HUD_ALERT_CAMERA_SEATBELT: return ICO_CAM_SPEED;
    case HUD_ALERT_CAMERA_DISTANCE: return ICO_CAM_SPEED;
    case HUD_ALERT_CAMERA_BUS_LANE: return ICO_CAM_SPEED;
    case HUD_ALERT_CAMERA_NOISE: return ICO_CAM_SPEED;
    case HUD_ALERT_CAMERA_STOP_SIGN: return ICO_CAM_REDLIGHT;
    case HUD_ALERT_FORK: return ICO_FORK_RIGHT;
    case HUD_ALERT_BROKEN_LIGHT: return ICO_CAM_REDLIGHT;
    case HUD_ALERT_NO_STRAIGHT_AND_RIGHT: return ICO_RIGHT;
    case HUD_ALERT_NO_LEFT_AND_U_TURN: return ICO_LEFT;
    case HUD_ALERT_NO_STRAIGHT_AND_LEFT: return ICO_LEFT;
    case HUD_ALERT_NO_LEFT_AND_RIGHT: return ICO_WARNING;
    case HUD_ALERT_CAR_NO_LEFT_AND_U_TURN: return ICO_LEFT;
    case HUD_ALERT_CAR_NO_RIGHT_AND_U_TURN: return ICO_RIGHT;
    case HUD_ALERT_NO_RIGHT_AND_U_TURN: return ICO_RIGHT;
    case HUD_ALERT_CAR_NO_LEFT_TURN: return ICO_LEFT;
    case HUD_ALERT_CAR_NO_RIGHT_TURN: return ICO_RIGHT;
    case HUD_ALERT_CAR_NO_U_TURN: return ICO_UTURN_LEFT;
    case HUD_ALERT_NONE:            return "";
    default:                        return ICO_WARNING;
    }
}

/* Kind 8 is the next limit, not necessarily a reduction. Unknown is neutral. */
static inline const char *ui_limit_change(int current, int next)
{
    if (current <= 0 || next <= 0) return "";
    if (next > current) return "Tăng";
    if (next < current) return "Giảm";
    return "Không đổi";
}

static inline const char *ui_alert_name(int alert)
{
    switch (alert) {
    case HUD_ALERT_POLICE:          return "Cảnh sát";
    case HUD_ALERT_CAMERA_SPEED:    return "Camera tốc độ";
    case HUD_ALERT_CAMERA_REDLIGHT: return "Camera đèn đỏ";
    case HUD_ALERT_HAZARD:          return "Nguy hiểm";
    case HUD_ALERT_ACCIDENT:        return "Tai nạn";
    case HUD_ALERT_TRAFFIC_JAM:     return "Kẹt xe";
    case HUD_ALERT_ROAD_CLOSED:     return "Đường đóng";
    case HUD_ALERT_SPEED_DROP:      return "Giới hạn tốc độ tiếp theo";
    case HUD_ALERT_NO_PASSING:      return "Cấm vượt";
    case HUD_ALERT_END_NO_PASSING:  return "Hết cấm vượt";
    case HUD_ALERT_RAILWAY:         return "Đường sắt";
    case HUD_ALERT_TOLL_BOOTH:      return "Trạm thu phí";
    case HUD_ALERT_STOPPED_VEHICLE: return "Xe dừng";
    case HUD_ALERT_CONSTRUCTION: return "Công trường";
    case HUD_ALERT_POTHOLE: return "Ổ gà";
    case HUD_ALERT_WEATHER: return "Thời tiết xấu";
    case HUD_ALERT_BLOCKED_LANE: return "Làn bị chặn";
    case HUD_ALERT_DANGEROUS_ROAD: return "Đường nguy hiểm";
    case HUD_ALERT_EXPRESSWAY_EXIT: return "Lối ra cao tốc";
    case HUD_ALERT_EXPRESSWAY_REST_STOP: return "Trạm dừng cao tốc";
    case HUD_ALERT_REST_STOP: return "Trạm dừng";
    case HUD_ALERT_END_SPEED_RESTRICTION: return "Hết hạn chế tốc độ";
    case HUD_ALERT_RESIDENTIAL_START: return "Khu đông dân cư";
    case HUD_ALERT_RESIDENTIAL_END: return "Hết khu dân cư";
    case HUD_ALERT_END_ALL_PROHIBITIONS: return "Hết mọi lệnh cấm";
    case HUD_ALERT_NO_CAR: return "Cấm ô tô";
    case HUD_ALERT_NO_MOTORCYCLE: return "Cấm xe máy";
    case HUD_ALERT_NO_LEFT_TURN: return "Cấm rẽ trái";
    case HUD_ALERT_NO_RIGHT_TURN: return "Cấm rẽ phải";
    case HUD_ALERT_NO_U_TURN: return "Cấm quay đầu";
    case HUD_ALERT_NO_STRAIGHT: return "Cấm đi thẳng";
    case HUD_ALERT_MANDATORY_STRAIGHT: return "Chỉ đi thẳng";
    case HUD_ALERT_MANDATORY_RIGHT: return "Chỉ rẽ phải";
    case HUD_ALERT_MANDATORY_LEFT: return "Chỉ rẽ trái";
    case HUD_ALERT_CAR_LANE: return "Làn ô tô";
    case HUD_ALERT_MOTORCYCLE_LANE: return "Làn xe máy";
    case HUD_ALERT_ONE_WAY: return "Đường một chiều";
    case HUD_ALERT_PROHIBITED_ROAD: return "Đường cấm";
    case HUD_ALERT_COMBINED_TURN_RESTRICTION: return "Cấm rẽ / quay đầu";
    case HUD_ALERT_CAMERA_PHONE: return "Camera phạt nguội";
    case HUD_ALERT_CAMERA_DUMMY: return "Camera giả";
    case HUD_ALERT_CAMERA_SEATBELT: return "Camera dây an toàn";
    case HUD_ALERT_CAMERA_DISTANCE: return "Camera khoảng cách";
    case HUD_ALERT_CAMERA_BUS_LANE: return "Camera làn buýt";
    case HUD_ALERT_CAMERA_NOISE: return "Camera tiếng ồn";
    case HUD_ALERT_CAMERA_STOP_SIGN: return "Camera biển Stop";
    case HUD_ALERT_ANIMAL: return "Động vật";
    case HUD_ALERT_OBJECT_ON_ROAD: return "Vật cản";
    case HUD_ALERT_ROADKILL: return "Xác động vật";
    case HUD_ALERT_FLOOD: return "Ngập nước";
    case HUD_ALERT_FOG: return "Sương mù";
    case HUD_ALERT_HAIL: return "Mưa đá";
    case HUD_ALERT_SNOW: return "Tuyết";
    case HUD_ALERT_ICE: return "Băng đường";
    case HUD_ALERT_SLIPPERY_ROAD: return "Đường trơn";
    case HUD_ALERT_SPEED_BUMP: return "Gờ giảm tốc";
    case HUD_ALERT_SCHOOL_ZONE: return "Khu trường học";
    case HUD_ALERT_LANES_MERGING: return "Nhập làn";
    case HUD_ALERT_DANGEROUS_CURVE: return "Cua gấp";
    case HUD_ALERT_FORK: return "Đường chẽ";
    case HUD_ALERT_BROKEN_LIGHT: return "Đèn hỏng";
    case HUD_ALERT_CYCLIST: return "Xe đạp";
    case HUD_ALERT_EMERGENCY_VEHICLE: return "Xe ưu tiên";
    case HUD_ALERT_PERSONAL_SAFETY: return "An toàn cá nhân";
    case HUD_ALERT_NO_STRAIGHT_AND_RIGHT: return "Cấm thẳng & phải";
    case HUD_ALERT_NO_LEFT_AND_U_TURN: return "Cấm trái & quay đầu";
    case HUD_ALERT_NO_STRAIGHT_AND_LEFT: return "Cấm thẳng & trái";
    case HUD_ALERT_NO_LEFT_AND_RIGHT: return "Cấm trái & phải";
    case HUD_ALERT_CAR_NO_LEFT_AND_U_TURN: return "Ô tô: cấm trái & quay đầu";
    case HUD_ALERT_CAR_NO_RIGHT_AND_U_TURN: return "Ô tô: cấm phải & quay đầu";
    case HUD_ALERT_NO_RIGHT_AND_U_TURN: return "Cấm phải & quay đầu";
    case HUD_ALERT_CAR_NO_LEFT_TURN: return "Ô tô: cấm rẽ trái";
    case HUD_ALERT_CAR_NO_RIGHT_TURN: return "Ô tô: cấm rẽ phải";
    case HUD_ALERT_CAR_NO_U_TURN: return "Ô tô: cấm quay đầu";
    case HUD_ALERT_NONE:            return "";
    default:                        return "Nguy hiểm";
    }
}
