#ifndef __MYIMAGEDOT_H__
#define __MYIMAGEDOT_H__

extern const unsigned char g_image_dot_lock_48x48[288];
extern const unsigned char g_image_dot_unlock_48x48[288];
extern const unsigned char g_image_dot_lock_24bit_32x32[128];
extern const unsigned char g_image_dot_unlock_24bit_32x32[128];

// 图像信息结构体
typedef struct {
    const char* name;              // 图像名字，方便索引指定图像数据
    const unsigned char* address;  // 图像数组入口地址
    unsigned int width;            // 图像宽度
    unsigned int height;           // 图像高度
    unsigned int size;             // 图像数组大小
} image_info_t;

static const image_info_t g_image_dot_tbl[4] = {
    {"lock", g_image_dot_lock_48x48, 48, 48, 288 },
    {"unlock", g_image_dot_unlock_48x48, 48, 48, 288 },
    {"lock_24bit", g_image_dot_lock_24bit_32x32, 32, 32, 128 },
    {"unlock_24bit", g_image_dot_unlock_24bit_32x32, 32, 32, 128 },
};

#endif //__MYIMAGEDOT_H__
