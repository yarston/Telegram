#include <jni.h>
#include <android/bitmap.h>
#include <android/log.h>
#include <cstring>
#include <cstdlib>

#ifdef __aarch64__
#include <arm_neon.h>
#endif

#define TAG "testlib"
#define  LOGD(...)  __android_log_print(ANDROID_LOG_DEBUG, TAG, __VA_ARGS__)

using i8 = int8_t;
using u8 = uint8_t;
using u16 = uint16_t;
using i32 = int32_t;
using u32 = uint32_t;

class Bitmap {
public:
    Bitmap(JNIEnv *env, jobject bitmap) : env(env), bmp(bitmap) {
        AndroidBitmap_getInfo(env, bitmap, &info);
        AndroidBitmap_lockPixels(env, bitmap, reinterpret_cast<void **>(&ptr));
    }
    ~Bitmap() {
        AndroidBitmap_unlockPixels(env, bmp);
    }
    u32 *ptr = nullptr;
    AndroidBitmapInfo info;
private:
    JNIEnv *env;
    jobject bmp;
};

class IntArray {
public:
    IntArray(JNIEnv* env, jintArray intArray) : env(env), intArray(intArray) {
        ptr = env->GetIntArrayElements(intArray, nullptr);
        len = env->GetArrayLength(intArray);
    }
    ~IntArray() {
        env->ReleaseIntArrayElements(intArray, ptr, 0);
    }
    jint* ptr = nullptr;
    jsize len = 0;
private:
    JNIEnv* env;
    jintArray intArray;
};

union bundle {
    u32 random;
    struct {
        i8 velocity_x;
        i8 velocity_y;
        u16 lifetime;
    };
} ;

const u32 cohesion = 4; //make efge between solid and dispersed part of image more smooth
const u32 cohesion_square = cohesion * cohesion;

void draw(u32 *dst, u32 *src, i32 width, i32 height, i32 xmin, i32 xmax, i32 ymin, u32 ymax, i32 step) {
    u32 xmax_dst = width - 1;
    u32 ymax_dst = height - 1;

    if(xmin < 0) xmin = 0;
    if(ymin < 0) ymin = 0;
    if(xmax > xmax_dst) xmax = xmax_dst;
    if(ymax > ymax_dst) ymax = ymax_dst;

    for (u32 y = ymin; y <= ymax; y += 2) {
        i32 distance = step - y + ymin;
        if (distance < 0) distance = 0;

        i32 shift_vertical = -((distance * distance) >> 15);
        distance = distance / 10 + cohesion;
        i32 step_alpha = distance * 4;

        u32 row = y * width;
        u32 *src_row_curr = src + y * width;
        u32 *src_row_next = src_row_curr + width;
        bool check_cohesion = distance <= cohesion;
        for (u32 x = xmin; x <= xmax; x += 2) {
            //preload
#ifdef __aarch64__
            uint8x8_t src_low = vld1_u8((u8*) (src_row_curr + x));
            uint8x8_t src_hight = vld1_u8((u8*) (src_row_next + x));
#else
            u32 p0 = src_row_curr[x];
            u32 p1 = src_row_curr[x + 1];
            u32 p2 = src_row_next[x];
            u32 p3 = src_row_next[x + 1];
#endif

            bundle pos = {.random = row + x};
            pos.random = (pos.random ^ 61) ^ (pos.random >> 16);
            pos.random *= 9;
            pos.random = pos.random ^ (pos.random >> 4);
            pos.random *= 0x27d4eb2d;
            pos.random = pos.random ^ (pos.random >> 15);

            i32 dx = ((distance * pos.velocity_x) >> 8) + shift_vertical;
            i32 dy = ((distance * pos.velocity_y) >> 8);

            if (check_cohesion && (dx * dx + dy * dy < cohesion_square)) {
                dx = 0;
                dy = 0;
            }

            i32 lifetime = 0x200 + (pos.lifetime & 0x1FF);
            i32 alpha = (lifetime - step_alpha);
            if(alpha > 255) alpha = 255;
            else if(alpha < 0) continue;

            u32 xd = x + dx;
            u32 yd = y + dy;
            if (xd >= xmax_dst || yd >= ymax_dst) continue;
            u32 *dst_row_curr = dst + (yd + xd * height);
            u32 *dst_row_next = dst_row_curr + height;

#ifdef __aarch64__
            uint8x8_t alpha_vec = vdup_n_u8((u8) alpha);
            uint16x8_t low = vmull_u8(src_low, alpha_vec);
            uint16x8_t high = vmull_u8(src_hight, alpha_vec);

            uint8x16_t vz = vuzp2q_u8(low, high);
            vst1_u8((u8*) dst_row_curr, vget_low_u8(vz));
            vst1_u8((u8*) dst_row_next, vget_high_u8(vz));
#else
            dst_row_curr[0] = ((((p0 & 0xFF000000) >> 8) * alpha) & 0xFF000000) | ((((p0 & 0xFF0000) >> 8) * alpha) & 0xFF0000) | ((((p0 & 0xFF00) >> 8) * alpha) & 0xFF00) | (((((p0 & 0xFF)) * alpha) >> 8) & 0xFF);
            dst_row_curr[1] = ((((p1 & 0xFF000000) >> 8) * alpha) & 0xFF000000) | ((((p1 & 0xFF0000) >> 8) * alpha) & 0xFF0000) | ((((p1 & 0xFF00) >> 8) * alpha) & 0xFF00) | (((((p1 & 0xFF)) * alpha) >> 8) & 0xFF);
            dst_row_next[0] = ((((p2 & 0xFF000000) >> 8) * alpha) & 0xFF000000) | ((((p2 & 0xFF0000) >> 8) * alpha) & 0xFF0000) | ((((p2 & 0xFF00) >> 8) * alpha) & 0xFF00) | (((((p2 & 0xFF)) * alpha) >> 8) & 0xFF);
            dst_row_next[1] = ((((p3 & 0xFF000000) >> 8) * alpha) & 0xFF000000) | ((((p3 & 0xFF0000) >> 8) * alpha) & 0xFF0000) | ((((p3 & 0xFF00) >> 8) * alpha) & 0xFF00) | (((((p3 & 0xFF)) * alpha) >> 8) & 0xFF);
#endif
        }
    }
}


extern "C"
JNIEXPORT void JNICALL
Java_org_telegram_ui_Components_Reactions_DustEffectLayout_draw(
    JNIEnv *env, jclass clazz, jobject dst_, jobject src_, jintArray areas_, jint width, jint height, jlong time) {
    Bitmap src(env, src_);
    Bitmap dst(env, dst_);
    IntArray areas(env, areas_);

    memset(dst.ptr, 0, width * height * 4);

    for(int i = 0; i < areas.len; i += 4) {
        auto rect = areas.ptr + i;
        //LOGD("img:%d y=[%d ; %d] x=[%d ; %d]", i, rect[1], rect[3], rect[0], rect[2]);
        draw(dst.ptr, src.ptr, width, height, rect[1], rect[3], rect[0], rect[2], time * 1.2f);
    }
}