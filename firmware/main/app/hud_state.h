#pragma once

#include <stdbool.h>
#include <stdint.h>

/* Tên đường tối đa 40 code point (§3). Tiếng Việt dựng sẵn tốn tới 3 byte mỗi
   ký tự, nên 40×3+1 = 121; làm tròn lên 128. */
#define HUD_STREET_MAX 128

#define HUD_ALERTS_MAX 4  /* §3: alrs tối đa 4 phần tử */
#define HUD_LANES_MAX  12 /* §3: lan tối đa 12 làn      */

/* §4.1 — mã HUD ổn định. Mã lạ PHẢI xử như NONE, không đoán thành hướng khác. */
typedef enum {
    HUD_TURN_NONE = 0,
    HUD_TURN_CONTINUE = 1,
    HUD_TURN_LEFT = 2,
    HUD_TURN_RIGHT = 3,
    HUD_TURN_SLIGHT_LEFT = 4,
    HUD_TURN_SLIGHT_RIGHT = 5,
    HUD_TURN_SHARP_LEFT = 6,
    HUD_TURN_SHARP_RIGHT = 7,
    HUD_TURN_U_TURN = 8,
    HUD_TURN_U_TURN_RIGHT = 9,
    HUD_TURN_ROUNDABOUT = 10,
    HUD_TURN_ROUNDABOUT_LEFT = 11,
    HUD_TURN_ROUNDABOUT_RIGHT = 12,
    HUD_TURN_KEEP_LEFT = 13,
    HUD_TURN_KEEP_RIGHT = 14,
    HUD_TURN_EXIT_LEFT = 15,
    HUD_TURN_EXIT_RIGHT = 16,
    HUD_TURN_ARRIVE = 17,
    HUD_TURN_FERRY = 18,
    HUD_TURN_ROUNDABOUT_STRAIGHT = 19,
    HUD_TURN_MAX = 20,
} hud_turn_t;

/* §4.2 — mã 0..13 đã biết. Mã 14..74 là phần mở rộng: giữ nguyên số nhưng vẽ
   icon nguy hiểm chung. */
typedef enum {
    HUD_ALERT_NONE = 0,
    HUD_ALERT_POLICE = 1,
    HUD_ALERT_CAMERA_SPEED = 2,
    HUD_ALERT_CAMERA_REDLIGHT = 3,
    HUD_ALERT_HAZARD = 4,
    HUD_ALERT_ACCIDENT = 5,
    HUD_ALERT_TRAFFIC_JAM = 6,
    HUD_ALERT_ROAD_CLOSED = 7,
    HUD_ALERT_SPEED_DROP = 8,
    HUD_ALERT_NO_PASSING = 9,
    HUD_ALERT_END_NO_PASSING = 10,
    HUD_ALERT_RAILWAY = 11,
    HUD_ALERT_TOLL_BOOTH = 12,
    HUD_ALERT_STOPPED_VEHICLE = 13,
    /* 0..9 bị đóng băng để tương thích firmware cũ; 10..74 là phần mở rộng
       additive của HLP/1 (§4.2). Sinh từ đặc tả bằng script, đừng gõ tay. */
    HUD_ALERT_CONSTRUCTION = 14,
    HUD_ALERT_POTHOLE = 15,
    HUD_ALERT_WEATHER = 16,
    HUD_ALERT_BLOCKED_LANE = 17,
    HUD_ALERT_DANGEROUS_ROAD = 18,
    HUD_ALERT_EXPRESSWAY_EXIT = 19,
    HUD_ALERT_EXPRESSWAY_REST_STOP = 20,
    HUD_ALERT_REST_STOP = 21,
    HUD_ALERT_END_SPEED_RESTRICTION = 22,
    HUD_ALERT_RESIDENTIAL_START = 23,
    HUD_ALERT_RESIDENTIAL_END = 24,
    HUD_ALERT_END_ALL_PROHIBITIONS = 25,
    HUD_ALERT_NO_CAR = 26,
    HUD_ALERT_NO_MOTORCYCLE = 27,
    HUD_ALERT_NO_LEFT_TURN = 28,
    HUD_ALERT_NO_RIGHT_TURN = 29,
    HUD_ALERT_NO_U_TURN = 30,
    HUD_ALERT_NO_STRAIGHT = 31,
    HUD_ALERT_MANDATORY_STRAIGHT = 32,
    HUD_ALERT_MANDATORY_RIGHT = 33,
    HUD_ALERT_MANDATORY_LEFT = 34,
    HUD_ALERT_CAR_LANE = 35,
    HUD_ALERT_MOTORCYCLE_LANE = 36,
    HUD_ALERT_ONE_WAY = 37,
    HUD_ALERT_PROHIBITED_ROAD = 38,
    HUD_ALERT_COMBINED_TURN_RESTRICTION = 39,
    HUD_ALERT_CAMERA_PHONE = 40,
    HUD_ALERT_CAMERA_DUMMY = 41,
    HUD_ALERT_CAMERA_SEATBELT = 42,
    HUD_ALERT_CAMERA_DISTANCE = 43,
    HUD_ALERT_CAMERA_BUS_LANE = 44,
    HUD_ALERT_CAMERA_NOISE = 45,
    HUD_ALERT_CAMERA_STOP_SIGN = 46,
    HUD_ALERT_ANIMAL = 47,
    HUD_ALERT_OBJECT_ON_ROAD = 48,
    HUD_ALERT_ROADKILL = 49,
    HUD_ALERT_FLOOD = 50,
    HUD_ALERT_FOG = 51,
    HUD_ALERT_HAIL = 52,
    HUD_ALERT_SNOW = 53,
    HUD_ALERT_ICE = 54,
    HUD_ALERT_SLIPPERY_ROAD = 55,
    HUD_ALERT_SPEED_BUMP = 56,
    HUD_ALERT_SCHOOL_ZONE = 57,
    HUD_ALERT_LANES_MERGING = 58,
    HUD_ALERT_DANGEROUS_CURVE = 59,
    HUD_ALERT_FORK = 60,
    HUD_ALERT_BROKEN_LIGHT = 61,
    HUD_ALERT_CYCLIST = 62,
    HUD_ALERT_EMERGENCY_VEHICLE = 63,
    HUD_ALERT_PERSONAL_SAFETY = 64,
    HUD_ALERT_NO_STRAIGHT_AND_RIGHT = 65,
    HUD_ALERT_NO_LEFT_AND_U_TURN = 66,
    HUD_ALERT_NO_STRAIGHT_AND_LEFT = 67,
    HUD_ALERT_NO_LEFT_AND_RIGHT = 68,
    HUD_ALERT_CAR_NO_LEFT_AND_U_TURN = 69,
    HUD_ALERT_CAR_NO_RIGHT_AND_U_TURN = 70,
    HUD_ALERT_NO_RIGHT_AND_U_TURN = 71,
    HUD_ALERT_CAR_NO_LEFT_TURN = 72,
    HUD_ALERT_CAR_NO_RIGHT_TURN = 73,
    HUD_ALERT_CAR_NO_U_TURN = 74,
    HUD_ALERT_KNOWN_MAX = 75,
} hud_alert_t;

typedef struct {
    int kind;      /* `k` — cùng enum với alr, KHÔNG phải `t` */
    int distance;  /* `d` — mét, -1 = không có */
    int value;     /* `v` — km/h, chỉ với biển mang giá trị */
    bool has_value;
    int traffic_severity; /* 1..5, 0 = unknown; traffic jam only */
    int traffic_delay_min; /* -1 = unknown */
} hud_alert_entry_t;

typedef struct {
    uint8_t directions; /* bitmask các hướng làn cho phép */
    uint8_t selected;   /* tập con của directions; 0 = Waze không khuyến nghị */
} hud_lane_t;

/* Snapshot đầy đủ, KHÔNG phải delta. Mọi message `s` dựng lại toàn bộ struct. */
typedef struct {
    bool nav;
    int speed;      /* spd  — km/h */
    int limit;      /* lim  — km/h, 0 = ẩn biển */
    bool over;
    int turn;       /* trn  */
    int turn_next;  /* trn2 */
    int distance;   /* dst  — mét, -1 = không có */
    int exit;       /* exit — số lối ra vòng xuyến */

    hud_lane_t lanes[HUD_LANES_MAX];
    int lane_count;
    bool lanes_present; /* phân biệt "mảng rỗng ⇒ xoá" với "thiếu key" */

    char street[HUD_STREET_MAX];      /* st  */
    char street_next[HUD_STREET_MAX]; /* st2 */

    char eta[8];    /* "HH:MM" */
    int remain_min; /* rmin */
    int remain_m;   /* rm — mét, chính xác và có thẩm quyền */

    bool avg_zone;  /* avg  — vùng CẤM VƯỢT ở VN, không phải camera */
    int avg_left;   /* avgL — mét còn lại */
    int avg_speed;  /* avgR — km/h khuyến nghị */
    int avg_pct;    /* avgP — 0..100 */

    int alert;          /* alr  — mirror alerts[0].kind */
    int alert_distance; /* alrD — mét, -1 = không có */
    int alert_value;    /* alrV — km/h */
    bool alert_has_value;
    int alert_traffic_severity;
    int alert_traffic_delay_min;

    hud_alert_entry_t alerts[HUD_ALERTS_MAX];
    int alert_count;
    bool alerts_present;

    uint32_t ts; /* uptime bên phát, chỉ so được trong cùng `sess` */
} hud_state_t;

/* Đưa struct về đúng giá trị mặc định của §3.
   PHẢI gọi trước mỗi lần decode: Android bỏ hẳn field đang ở giá trị mặc định,
   nên tái dùng struct cũ sẽ khiến giá trị cũ dính lại. */
void hud_state_reset(hud_state_t *state);

/* Mũi tên vẽ được không? Mã lạ trả false để renderer không vẽ gì. */
bool hud_turn_known(int turn);

/* Cảnh báo có icon riêng không? false ⇒ dùng icon nguy hiểm chung. */
bool hud_alert_known(int alert);

/* Quãng đường còn lại tính bằng mét, đã ưu tiên `rm` hơn `rkm`. */
int hud_remaining_metres(const hud_state_t *state);
