#ifndef _FIMG2D_H_
#define _FIMG2D_H_

#include "fimg2d_reg.h"

typedef unsigned int G2dFixed;

typedef enum {
	G2D_INT_MODE_LEVEL,
	G2D_INT_MODE_EDGE,

	G2D_INT_MODE_MAX = G2D_INT_MODE_EDGE
} G2dIntMode;

typedef enum {
	G2D_TRANSPARENT_MODE_OPAQUE,
	G2D_TRANSPARENT_MODE_TRANSPARENT,
	G2D_TRANSPARENT_MODE_BLUESCREEN,
	G2D_TRANSPARENT_MODE_MAX
} G2dTransparentMode;

typedef enum {
	G2D_COLORKEY_MODE_DISABLE = 0,
	G2D_COLORKEY_MODE_SRC_RGBA = (1<<0),
	G2D_COLORKEY_MODE_DST_RGBA = (1<<1),
 	G2D_COLORKEY_MODE_SRC_YCbCr = (1<<2),			/* VER4.1 */
	G2D_COLORKEY_MODE_DST_YCbCr = (1<<3),			/* VER4.1 */
	
	G2D_COLORKEY_MODE_MASK = 15, 
} G2dColorKeyMode;

typedef enum {
	G2D_ALPHA_BLEND_MODE_DISABLE,
	G2D_ALPHA_BLEND_MODE_ENABLE,
	G2D_ALPHA_BLEND_MODE_FADING,			/* VER3.0 */
	G2D_ALPHA_BLEND_MODE_MAX
} G2dAlphaBlendMode;

typedef enum {
	G2D_SRC_NONPREBLAND_MODE_DISABLE,		/* VER3.0 */
	G2D_SRC_NONPREBLAND_MODE_CONSTANT,	/* VER3.0 */
	G2D_SRC_NONPREBLAND_MODE_PERPIXEL,		/* VER3.0 */
	G2D_SRC_NONPREBLAND_MODE_MAX,			/* VER3.0 */
} G2dSrcNonPreBlendMode;

typedef enum {
	G2D_SELECT_SRC_FOR_ALPHA_BLEND,	/* VER4.1 */
	G2D_SELECT_ROP_FOR_ALPHA_BLEND,	/* VER4.1 */
} G2dSelectAlphaSource;


typedef enum {								/* VER4.1 */
	G2D_COEFF_MODE_ONE,
	G2D_COEFF_MODE_ZERO,
	G2D_COEFF_MODE_SRC_ALPHA,
	G2D_COEFF_MODE_SRC_COLOR,
	G2D_COEFF_MODE_DST_ALPHA,
	G2D_COEFF_MODE_DST_COLOR,
	G2D_COEFF_MODE_GB_ALPHA,	/* Global Alpha : Set by ALPHA_REG(0x618) */
	G2D_COEFF_MODE_GB_COLOR,	/* Global Alpha : Set by ALPHA_REG(0x618) */
	G2D_COEFF_MODE_DISJOINT_S, /* (1-SRC alpha)/DST Alpha */
	G2D_COEFF_MODE_DISJOINT_D, /* (1-DST alpha)/SRC Alpha */
	G2D_COEFF_MODE_CONJOINT_S, /* SRC alpha/DST alpha */
	G2D_COEFF_MODE_CONJOINT_D, /* DST alpha/SRC alpha */
	G2D_COEFF_MODE_MASK
}G2dCoeffMode;

typedef enum {
    G2D_ACOEFF_MODE_A,          /*alpha*/
    G2D_ACOEFF_MODE_APGA,   /*alpha + global alpha*/
    G2D_ACOEFF_MODE_AMGA,   /*alpha * global alpha*/
    G2D_ACOEFF_MODE_MASK
}G2dACoeffMode;

typedef enum {
	G2D_SELECT_MODE_NORMAL = (0 << 0),
	G2D_SELECT_MODE_FGCOLOR = (1 << 0),
	G2D_SELECT_MODE_BGCOLOR = (2 << 0),
	G2D_SELECT_MODE_MAX = (3 << 0),
} G2dSelectMode;

typedef enum {
	/* COLOR FORMAT */
	G2D_COLOR_FMT_XRGB8888,
	G2D_COLOR_FMT_ARGB8888,
	G2D_COLOR_FMT_RGB565,
	G2D_COLOR_FMT_XRGB1555,
	G2D_COLOR_FMT_ARGB1555,
	G2D_COLOR_FMT_XRGB4444,
	G2D_COLOR_FMT_ARGB4444,
	G2D_COLOR_FMT_PRGB888,
	G2D_COLOR_FMT_YCbCr444,
	G2D_COLOR_FMT_YCbCr422,
	G2D_COLOR_FMT_YCbCr420=10,     
	G2D_COLOR_FMT_A8,                       /* alpha 8bit */
	G2D_COLOR_FMT_L8,                       /* Luminance 8bit: gray color */
	G2D_COLOR_FMT_A1,                       /* alpha 1bit */
	G2D_COLOR_FMT_A4,                       /* alpha 4bit */
	G2D_COLOR_FMT_MASK = (15 << 0),		/* VER4.1 */

	/* COLOR ORDER */
	G2D_ORDER_AXRGB = (0 << 4),			/* VER4.1 */
	G2D_ORDER_RGBAX = (1 << 4),			/* VER4.1 */
	G2D_ORDER_AXBGR = (2 << 4),			/* VER4.1 */
	G2D_ORDER_BGRAX = (3 << 4),			/* VER4.1 */
	G2D_ORDER_MASK = (3 << 4),				/* VER4.1 */

	/* Number of YCbCr plane */
	G2D_YCbCr_1PLANE = (0 << 8),			/* VER4.1 */
	G2D_YCbCr_2PLANE = (1 << 8),			/* VER4.1 */
	G2D_YCbCr_PLANE_MASK = (3 << 8),		/* VER4.1 */
	
	/* Order in YCbCr */
	G2D_YCbCr_ORDER_CrY1CbY0 = (0 << 12),	/* VER4.1 */
	G2D_YCbCr_ORDER_CbY1CrY0 = (1 << 12),	/* VER4.1 */
	G2D_YCbCr_ORDER_Y1CrY0Cb = (2 << 12),	/* VER4.1 */
	G2D_YCbCr_ORDER_Y1CbY0Cr = (3 << 12),	/* VER4.1 */
	G2D_YCbCr_ORDER_CrCb = G2D_YCbCr_ORDER_CrY1CbY0,	/* VER4.1, for 2 plane */
	G2D_YCbCr_ORDER_CbCr = G2D_YCbCr_ORDER_CbY1CrY0,	/* VER4.1, for 2 plane */
	G2D_YCbCr_ORDER_MASK = (3 < 12),		/* VER4.1 */
	
	/* CSC */
	G2D_CSC_601 = (0 << 16),				/* VER4.1 */
	G2D_CSC_709 = (1 << 16),				/* VER4.1 */
	G2D_CSC_MASK = (1 << 16),				/* VER4.1 */
	
	/* Valid value range of YCbCr */
	G2D_YCbCr_RANGE_NARROW = (0 << 17),	/* VER4.1 */
	G2D_YCbCr_RANGE_WIDE = (1 << 17),		/* VER4.1 */
	G2D_YCbCr_RANGE_MASK= (1 << 17),		/* VER4.1 */

	G2D_COLOR_MODE_MASK = 0xFFFFFFFF
}G2dColorMode;

typedef enum {
	G2D_SCALE_MODE_NONE = 0,
	G2D_SCALE_MODE_NEAREST,
	G2D_SCALE_MODE_BILINEAR,

	G2D_SCALE_MODE_MAX
} G2dScaleMode;

typedef enum {
    G2D_REPEAT_MODE_REPEAT,
    G2D_REPEAT_MODE_PAD,
    G2D_REPEAT_MODE_REFLECT,
    G2D_REPEAT_MODE_CLAMP,
    G2D_REPEAT_MODE_NONE,
} G2dRepeatMode;

typedef enum {
    G2D_MASK_OP_ALPHA_ONLY,
    G2D_MASK_OP_CMPONENT,
    G2D_MASK_OP_CHANNEL,

    G2D_MASK_OP_MAX
} G2dMaskOpType;

typedef enum {
    G2D_MASK_ORDER_AXRGB,
    G2D_MASK_ORDER_RGBAX,
    G2D_MASK_ORDER_AXBGR,
    G2D_MASK_ORDER_BGRAX,
    
    G2D_MASK_ORDER_MAX
} G2dMaskOrder;

typedef enum {
	G2D_MASK_MODE_1BPP = 0,
	G2D_MASK_MODE_4BPP,
	G2D_MASK_MODE_8BPP,
	G2D_MASK_MODE_16BPP_565,
	G2D_MASK_MODE_16BPP_1555,
	G2D_MASK_MODE_16BPP_4444,
	G2D_MASK_MODE_32BPP,
	G2D_MASK_MODE_4BPP_FOR_WINCE, /* ([31:24]bit field of 32bit is used as mask) */
	
	G2D_MASK_MODE_MAX
} G2dMaskMode;

/*
Here are some examples on how to use the ROP3 value to perform the operations:
1)	Final Data = Source. Only the Source data matter, so ROP Value = "0xCC".
2)	Final Data = Destination. Only the Destination data matter, so ROP Value = "0xAA".
3)	Final Data = Pattern. Only the Pattern data matter, so ROP Value = "0xF0".
4)	Final Data = Source AND Destination.  ROP Value = "0xCC" & "0xAA" = "0x88"
5)	Final Data = Source OR Pattern. ROP Value = "0xCC" | "0xF0" = "0xFC".
*/
typedef enum G2D_ROP3_TYPE {
	G2D_ROP3_DST = 0xAA,
	G2D_ROP3_SRC = 0xCC,
	G2D_ROP3_3RD = 0xF0,

	G2D_ROP3_MASK = 0xFF
} G2dROP3Type;

typedef enum G2D_ALU {
    /*Common alu for x base*/    
    G2Dclear = 0x0,		/* 0 */
    G2Dand	=		0x1,		/* src AND dst */
    G2DandReverse =		0x2,		/* src AND NOT dst */
    G2Dcopy =			0x3,		/* src */
    G2DandInverted =		0x4,		/* NOT src AND dst */
    G2Dnoop =			0x5,		/* dst */
    G2Dxor =			0x6,		/* src XOR dst */
    G2Dor =			0x7,		/* src OR dst */
    G2Dnor =			0x8,		/* NOT src AND NOT dst */
    G2Dequiv =			0x9,		/* NOT src XOR dst */
    G2Dinvert =		0xa,		/* NOT dst */
    G2DorReverse =		0xb,		/* src OR NOT dst */
    G2DcopyInverted =		0xc,		/* NOT src */
    G2DorInverted =		0xd,		/* NOT src OR dst */
    G2Dnand =			0xe,		/* NOT src OR NOT dst */
    G2Dset =			0xf,		/* 1 */

    /*Special alu*/
    G2Dnegative =       0x20,       /*NOT src XOR 0x00FFFFFF*/
}G2dAlu;

typedef union _G2D_BITBLT_CMD_VAL {
	unsigned int val;
	struct {
		/* [0:3] */
		unsigned int maskROP4En:1;
		unsigned int maskingEn:1;
		G2dSelectAlphaSource ROP4AlphaEn:1;
		unsigned int ditherEn:1;
		/* [4:7] */
		unsigned int resolved1:4;
		/* [8:11] */
		unsigned int cwEn:4;
		/* [12:15] */
		G2dTransparentMode transparentMode:4;
		/* [16:19] */
		G2dColorKeyMode colorKeyMode:4;
		/* [20:23] */
		G2dAlphaBlendMode alphaBlendMode:4;
		/* [24:27] */
		unsigned int srcPreMultiply:1;
		unsigned int patPreMultiply:1;
		unsigned int dstPreMultiply:1;
		unsigned int dstDepreMultiply:1;
		/* [28:31] */
		unsigned int fastSolidColorFillEn:1;
		unsigned int reserved:3;
	}data;
} G2dBitBltCmdVal;

typedef union _G2D_BLEND_FUNCTION_VAL {
	unsigned int val;
	struct {
		/* [0:15] */
		G2dCoeffMode srcCoeff:4;
		G2dACoeffMode srcCoeffSrcA:2;
		G2dACoeffMode srcCoeffDstA:2;
		G2dCoeffMode dstCoeff:4;
		G2dACoeffMode dstCoeffSrcA:2;
		G2dACoeffMode dstCoeffDstA:2;
		/* [16:19] */
		unsigned int invSrcColorCoeff:1;
		unsigned int resoled1:1;
		unsigned int invDstColorCoeff:1;
		unsigned int resoled2:1;
		/* [20:23] */
		unsigned int lightenEn:1;
		unsigned int darkenEn:1;
		unsigned int winCESrcOverEn:2;
		/* [24:31] */
		unsigned int reserved:8;
	}data;
} G2dBlendFunctionVal;

typedef union _G2D_ROUND_MODE_VAL {
	unsigned int val;
	struct {
		/*
			Round Mode in Blending
			2'b00 : 
			Result = ((A + 1) * B) >>8;
			2'b01 :
			Result = ((A+ (A>>7)) * B) >>8;
			2'b10 :
			Result_tmp =  A * B + 0x80;
			Result = (Result_tmp + (Result_tmp>>8))>>8;
			2'b11 :
			   Result_A = A * B; (16 bpp)
			Result_B = C * D; (16 bpp)
			   Result_tmp = Result_A + Result_B + 0x80;
			Result = (Result_tmp + (Result_tmp>>8))>>8;
		*/
		unsigned int blend:4;
		/*
			Round Mode in Alpha Premultiply
			2'b00 : 
			Result = (A * B) >>8;
			2'b01 :
			Result = ((A + 1) * B) >>8;
			2'b10 :
			Result = ((A+ (A>>7)) * B) >>8;
			2'b11 : 
			Result_tmp =  A * B + 0x80;
			Result = (Result_tmp + (Result_tmp>>8))>>8;
		*/
		unsigned int alphaPremultiply:4;
		/*
			Round Mode in Alpha Depremultiply
			2'b00 : 
			Result = ((A + 1) * B) >>8;
			2'b01 :
			Result = ((A+ (A>>7)) * B) >>8;
			2'b10 :
			Result_tmp =  A * B + 0x80;
			Result = (Result_tmp + (Result_tmp>>8))>>8;
			2'b11 : Reserved
		*/
		unsigned int alphaDepremultiply:4;

		unsigned int reserved:20;
	}data;
} G2dRoundModeVal;

typedef union _G2D_ROTATE_VAL {
	unsigned int val;
	struct {
		/*
			0 : No rotation
			1 : 90 degree rotation
		*/
		unsigned int srcRotate:4;
		unsigned int patRotate:4;
		unsigned int maskRotate:4;

		unsigned int reserved:20;
	}data;
} G2dRotateVal;

typedef union _G2D_SRC_MASK_DIR_VAL {
	unsigned int val;
	struct {
		unsigned int dirSrcX:1;
		unsigned int dirSrcY:1;
		unsigned int reserved1:2;

		unsigned int dirMaskX:1;
		unsigned int dirMaskY:1;
		unsigned int reserved2:2;
		
		unsigned int reserved:24;
	}data;
} G2dSrcMaskDirVal;

typedef union _G2D_DST_PAT_DIR_VAL {
	unsigned int val;
	struct {
		unsigned int dirDstX:1;
		unsigned int dirDstY:1;
		unsigned int reserved1:2;

		unsigned int dirPatX:1;
		unsigned int dirPatY:1;
		unsigned int reserved2:2;

		unsigned int reserved:24;
	}data;
} G2dDstPatDirVal;

typedef union G2D_POINT_VAL {
	unsigned int val;
	struct {
		/*
			X Coordinate of Source Image
			Range: 0 ~ 8000 (Requirement: SrcLeftX < SrcRightX)
			In YCbCr 422 and YCbCr 420 format, this value should be even number.
		*/
		unsigned int x:16;
		/*
			Y Coordinate of Source Image
			Range: 0 ~ 8000 (Requirement: SrcTopY < SrcBottomY)
			In YCbCr 420 format, this value should be even number.
		*/
		unsigned int y:16;
	}data;
} G2dPointVal;

typedef union G2D_BOX_SIZE_VAL {
	unsigned int val;
	struct {
		/*
			Width of box. Range: 1 ~ 8000.
		*/
		unsigned int width:16;
		/*
			Height of box. Range: 1 ~ 8000.
		*/
		unsigned int height:16;
	}data;
} G2dBoxSizeVal;

typedef union _G2D_SELECT_3RD_VAL {
	unsigned int val;
	struct{
		G2dSelectMode unmasked:4;
		G2dSelectMode masked:4;

		unsigned int reserved:24;
	}data;
} G2dSelect3rdVal;

typedef union _G2D_ROP4_VAL {
	unsigned int val;
	struct{
		G2dROP3Type unmaskedROP3:8;
		G2dROP3Type maskedROP3:8;

		unsigned int reserved:16;
	}data;
} G2dROP4Val;

typedef union _G2D_MASK_MODE_VAL {
    unsigned int val;
    struct{
        /* [0:7] */
        G2dMaskMode maskMode:4;
        G2dMaskOrder maskOrder:4;
        /* [8:11] */
        G2dMaskOpType maskOp:4;
        /* [12:31] */
        unsigned int reserved:20;
    }data;
} G2dMaskModeVal;

typedef union _G2D_GLOBAL_ARGB_VAL {
	unsigned int val;
	struct{
		unsigned int alpha:8;
		unsigned int color:24;
	}data;
} G2dGlovalARGBVal;

typedef union _G2D_COLORKEY_CTL_VAL {
	unsigned int val;
	struct{
		/*
			0: Stencil Test Off for each RGBA value
			1: Stencil Test On for each RGBA value
		*/
		unsigned int stencilOnB:4;
		unsigned int stencilOnG:4;
		unsigned int stencilOnR:4;
		unsigned int stencilOnA:4;
		unsigned int stencilInv:4;

		unsigned int reserved:12;
	}data;
} G2dColorkeyCtlVal;

typedef union _G2D_COLORKEY_DR_MIN_VAL {
	unsigned int val;
	struct{
		/*
		The color format of source colorkey decision reference register is generally the same as the source color format. 
		If the color format of source is YCbCr format, the color format of this register is ARGB_8888 format.
		But if the source color is selected as the foreground color or the background color, 
		the source colorkey operation is not activated because the colorkeying for the foreground color 
		and the background color set by user is meaningless.
		*/
		unsigned int drMinB:8;
		unsigned int drMinG:8;
		unsigned int drMinR:8;
		unsigned int drMinA:8;
	}data;
} G2dColorkeyDrMinVal;

typedef union _G2D_COLORKEY_DR_MAX_VAL {
	unsigned int val;
	struct{
		/*
		The color format of source colorkey decision reference register is generally the same as the source color format. 
		If the color format of source is YCbCr format, the color format of this register is ARGB_8888 format.
		But if the source color is selected as the foreground color or the background color, 
		the source colorkey operation is not activated because the colorkeying for the foreground color and 
		the background color set by user is meaningless.
		*/
		unsigned int drMaxB:8;
		unsigned int drMaxG:8;
		unsigned int drMaxR:8;
		unsigned int drMaxA:8;
	}data;
} G2dColorkeyDrMaxVal;

typedef union _G2D_COLORKEY_YCBCR_CTL_VAL {
	unsigned int val;
	struct{
		unsigned int stencilOnCr:4;
		unsigned int stencilOnCb:4;
		unsigned int stencilOnY:4;
		unsigned int stencilInv:4;

		unsigned int reserved:16;
	}data;
} G2dColorkeyYCbCrCtlVal;

typedef union _G2D_COLORKEY_YCBCR_DR_MIN_VAL {
	unsigned int val;
	struct{
		unsigned int drMinCr:8;
		unsigned int drMinCb:8;
		unsigned int drMinY:8;

		unsigned int reserved:8;
	}data;		
} G2dColorkeyYCbCrDrMinVal;

typedef union _G2D_COLORKEY_YCBCR_DR_MAX_VAL {
	unsigned int val;
	struct{
		unsigned int drMaxCr:8;
		unsigned int drMaxCb:8;
		unsigned int drMaxY:8;

		unsigned int reserved:8;
	}data;
} G2dColorkeyYCbCrDrMaxVal;

typedef enum {
	G2D_OP_CLEAR					= 0x00,
	G2D_OP_SRC						= 0x01,
	G2D_OP_DST					= 0x02,
	G2D_OP_OVER					= 0x03,
	G2D_OP_OVER_REVERSE			= 0x04,
	G2D_OP_IN						= 0x05,
	G2D_OP_IN_REVERSE				= 0x06,
	G2D_OP_OUT					= 0x07,
	G2D_OP_OUT_REVERSE			= 0x08,
	G2D_OP_ATOP					= 0x09,
	G2D_OP_ATOP_REVERSE			= 0x0a,
	G2D_OP_XOR					= 0x0b,
	G2D_OP_ADD					= 0x0c,
	G2D_OP_SATURATE				= 0x0d,

	G2D_OP_DISJOINT_CLEAR		= 0x10,
	G2D_OP_DISJOINT_SRC			= 0x11,
	G2D_OP_DISJOINT_DST			= 0x12,
	G2D_OP_DISJOINT_OVER			= 0x13,
	G2D_OP_DISJOINT_OVER_REVERSE	= 0x14,
	G2D_OP_DISJOINT_IN				= 0x15,
	G2D_OP_DISJOINT_IN_REVERSE		= 0x16,
	G2D_OP_DISJOINT_OUT				= 0x17,
	G2D_OP_DISJOINT_OUT_REVERSE		= 0x18,
	G2D_OP_DISJOINT_ATOP				= 0x19,
	G2D_OP_DISJOINT_ATOP_REVERSE	= 0x1a,
	G2D_OP_DISJOINT_XOR		= 0x1b,

	G2D_OP_CONJOINT_CLEAR	= 0x20,
	G2D_OP_CONJOINT_SRC		= 0x21,
	G2D_OP_CONJOINT_DST		= 0x22,
	G2D_OP_CONJOINT_OVER		= 0x23,
	G2D_OP_CONJOINT_OVER_REVERSE	= 0x24,
	G2D_OP_CONJOINT_IN				= 0x25,
	G2D_OP_CONJOINT_IN_REVERSE		= 0x26,
	G2D_OP_CONJOINT_OUT				= 0x27,
	G2D_OP_CONJOINT_OUT_REVERSE		= 0x28,
	G2D_OP_CONJOINT_ATOP				= 0x29,
	G2D_OP_CONJOINT_ATOP_REVERSE	= 0x2a,
	G2D_OP_CONJOINT_XOR				= 0x2b,
	
	G2D_N_OPERATORS,
	G2D_OP_NONE = G2D_N_OPERATORS
}G2dOp;
#define G2D_OP_DEFAULT G2D_OP_NONE

#define G2D_PLANE_MAX 2

typedef struct _G2D_IMAGE	G2dImage;
struct _G2D_IMAGE {
    G2dSelectMode select_mode;
    G2dColorMode color_mode;
    G2dRepeatMode repeat_mode;

    G2dFixed xscale;
    G2dFixed yscale;
    G2dScaleMode scale_mode;

    unsigned char rotate_90;
    unsigned char xDir;
    unsigned char yDir;
    unsigned char componentAlpha;
    
    unsigned int width;
    unsigned int height;
    unsigned int stride;

    unsigned int need_free;
    union{
        unsigned int	color;
        unsigned int 	bo[G2D_PLANE_MAX];
    }data;

    void *mapped_ptr[G2D_PLANE_MAX];
};

#define G2D_PT(_x, _y) ( (unsigned int)(_y<<16|_x<<0))

#define G2D_FIXED_1 (1<<16)
#define G2D_DOUBLE_TO_FIXED(_d) ((G2dFixed)((_d) * 65536.0))
#define G2D_INT_TO_FIXED(_i)    ((G2dFixed)((_i) << 16))
#define G2D_FIXED_TO_DOUBLE(_f) (double) ((_f) / (double) G2D_FIXED_1)
#define G2D_FIXED_TO_INT(_f)    ((int) ((_f) >> 16))

int g2d_init(int fd);
void g2d_fini(void);
int g2d_add_cmd(unsigned int cmd, unsigned int value);
void g2d_reset (unsigned int clear_reg);
int g2d_exec(void);
int g2d_flush(void);
void g2d_dump(void);

G2dImage* g2d_image_create_solid(unsigned int color);
G2dImage* g2d_image_create_bo(G2dColorMode format, 
                                                        unsigned int width, unsigned int height, 
                                                        unsigned int bo, unsigned int stride);
G2dImage* g2d_image_create_bo2 (G2dColorMode format,
                                unsigned int width, unsigned int height,
                                unsigned int bo1, unsigned int bo2, unsigned int stride);
G2dImage* g2d_image_create_data (G2dColorMode format, 
                                                        unsigned int width, unsigned int height, 
                                                        void* data, unsigned int stride);

void g2d_image_free (G2dImage* img);

int g2d_set_src(G2dImage* img);
int g2d_set_dst(G2dImage* img);
int g2d_set_mask(G2dImage* img);
unsigned int g2d_get_blend_op(G2dOp op);

/* UTIL Functions */
void util_g2d_fill(G2dImage* img,
                            int x, int y, unsigned int w, unsigned int h,
                            unsigned int color);
void util_g2d_fill_alu(G2dImage* img,
            int x, int y, unsigned int w, unsigned int h,
            unsigned int color, G2dAlu alu);
void util_g2d_copy(G2dImage* src, G2dImage* dst, 
                            int src_x, int src_y, 
                            int dst_x, int dst_y,
                            unsigned int width, unsigned int height);
void util_g2d_copy_alu(G2dImage* src, G2dImage* dst, 
            int src_x, int src_y, 
            int dst_x, int dst_y,
            unsigned int width, unsigned int height,
            G2dAlu alu);                            
void util_g2d_copy_with_scale(G2dImage* src, G2dImage* dst, 
                            int src_x, int src_y, unsigned int src_w, unsigned int src_h,
                            int dst_x, int dst_y, unsigned int dst_w, unsigned int dst_h, 
                            int negative);
void util_g2d_blend(G2dOp op, G2dImage* src, G2dImage* dst, 
                            int src_x, int src_y, 
                            int dst_x, int dst_y,
                            unsigned int width, unsigned int height);
void util_g2d_blend_with_scale(G2dOp op, G2dImage* src, G2dImage* dst,
                            int src_x, int src_y, unsigned int src_w, unsigned int src_h,
                            int dst_x, int dst_y, unsigned int dst_w, unsigned int dst_h,
                            int negative);
void util_g2d_composite(G2dOp op, G2dImage* src, G2dImage* mask, G2dImage* dst, 
            int src_x, int src_y, 
            int mask_x, int mask_y, 
            int dst_x, int dst_y,
            unsigned int width, unsigned int height);

#endif /* _FIMG2D3X_H_ */
