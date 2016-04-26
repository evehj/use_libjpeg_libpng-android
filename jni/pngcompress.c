#include "png.h"
#include "pngcompress.h"
#include <stdio.h>
#include <stdlib.h>
#include <setjmp.h>
#include "jpeglib.h"
#include <assert.h>
#define tmin(a, b) ((a)>(b) ? (b):(a))
#define tmax(a,b)  ((a) > (b)?(a):(b))
//!1不要将这三个宏同时打开需要互斥

//#define USE_PREMULTIPLY_APLHA (0)//使用预乘
#define USE_RGBA (1)//使用 r g b a
//#define USE_ABGR (0)//使用 a b g r

//预乘 aplha
#define CC_RGB_PREMULTIPLY_APLHA(vr, vg, vb, va) \
 (unsigned)(((unsigned)((unsigned char)(vr) * ((unsigned char)(va) + 1)) >> 8) | \
 ((unsigned)((unsigned char)(vg) * ((unsigned char)(va) + 1) >> 8) << 8) | \
 ((unsigned)((unsigned char)(vb) * ((unsigned char)(va) + 1) >> 8) << 16) | \
 ((unsigned)(unsigned char)(va) << 24))

//r g b a
#define CC_RGB_PREMULTIPLY_APLHA_RGBA(vr, vg, vb, va)\
 ( (unsigned)(vr))|\
    ( (unsigned)(vg) << 8)|\
  ( (unsigned)(vb) << 16)|\
  ((unsigned)(va) << 24)

//a b g r
//使用该宏的时候 write png时需要调用函数 png_set_swap_alpha
#define CC_RGB_PREMULTIPLY_APLHA_ABGR(vr, vg, vb, va)\
 ( (unsigned)(vr) << 8)|\
 ( (unsigned)(vg) << 16)|\
 ( (unsigned)(vb) << 24)|\
 ((unsigned)(va))

//读取png图片，并返回宽高，若出错则返回NULL
unsigned char* ReadPng(const char* path, int* width, int* height) {
	FILE* file = fopen(path, "rb");
	png_structp png_ptr = png_create_read_struct(PNG_LIBPNG_VER_STRING, 0, 0,
			0);
	png_infop info_ptr = png_create_info_struct(png_ptr);
	setjmp(png_jmpbuf(png_ptr));
	png_init_io(png_ptr, file);
	png_read_png(png_ptr, info_ptr, PNG_TRANSFORM_EXPAND, 0);
	int m_width = *width = png_get_image_width(png_ptr, info_ptr);
	int m_height = *height = png_get_image_height(png_ptr, info_ptr);
	int color_type = png_get_color_type(png_ptr, info_ptr);
	int bytesPerComponent = 3, i = 0, j = 0, p = 0;
	if (color_type & PNG_COLOR_MASK_ALPHA) {
		bytesPerComponent = 4;
		p = 1;
	}
	int size = m_height * m_width * bytesPerComponent;
	unsigned char *pImateRawData = (unsigned char *) malloc(size);
	png_bytep* rowPointers = png_get_rows(png_ptr, info_ptr);
	int bytesPerRow = m_width * bytesPerComponent;
	if (p == 1) {
		unsigned int *tmp = (unsigned int *) pImateRawData;
		for (i = 0; i < m_height; i++) {
			for (j = 0; j < bytesPerRow; j += 4) {
#if USE_PREMULTIPLY_APLHA
				*tmp++ = CC_RGB_PREMULTIPLY_APLHA( rowPointers[i][j], rowPointers[i][j + 1],
						rowPointers[i][j + 2], rowPointers[i][j + 3] );
#elif USE_RGBA
				*tmp++ = CC_RGB_PREMULTIPLY_APLHA_RGBA(rowPointers[i][j],
						rowPointers[i][j + 1], rowPointers[i][j + 2],
						rowPointers[i][j + 3]);
#elif USE_ABGR
				*tmp++ = CC_RGB_PREMULTIPLY_APLHA_ABGR( rowPointers[i][j], rowPointers[i][j + 1],
						rowPointers[i][j + 2], rowPointers[i][j + 3] );
#endif
			}
		}
	} else {
		for (j = 0; j < m_height; ++j) {
			memcpy(pImateRawData + j * bytesPerRow, rowPointers[j],
					bytesPerRow);
		}
	}
	return pImateRawData;
}
int png_to_jpeg(const char *pngfile, const char *jpegfile, int jpegquality) {
	FILE *fpin = fopen(pngfile, "rb");
	if (!fpin) {
		//perror(pngfile);
		return 1;
	}

	unsigned char header[8];
	fread(header, 1, 8, fpin);
	if (png_sig_cmp(header, 0, 8)) {
		//fprintf(stderr, "this is not a PNG file\n");
		return 2;
	}

	int ret = 0;

	png_structp png_ptr = png_create_read_struct(PNG_LIBPNG_VER_STRING, 0, 0,
			0);
	assert(png_ptr);

	png_infop info_ptr = png_create_info_struct(png_ptr);
	assert(info_ptr);

	png_infop end_info = png_create_info_struct(png_ptr);
	assert(end_info);

	if (setjmp(png_jmpbuf(png_ptr))) {
		//fprintf(stderr, "failed.\n");
		ret = 3;
		goto error_png;
	}

	png_init_io(png_ptr, fpin);
	png_set_sig_bytes(png_ptr, 8);
	png_read_png(png_ptr, info_ptr, PNG_TRANSFORM_IDENTITY, 0);
	png_bytep * row_pointers = png_get_rows(png_ptr, info_ptr);

	png_uint_32 width, height;
	int bit_depth, color_type;
	png_get_IHDR(png_ptr, info_ptr, &width, &height, &bit_depth, &color_type, 0,
			0, 0);

	if (color_type != PNG_COLOR_TYPE_RGB_ALPHA) {
		//fprintf(stderr, "input PNG must be RGB+Alpha\n");
		ret = 4;
		goto error_png;
	}
	if (bit_depth != 8) {
		//fprintf(stderr, "input bit depth must be 8bit!\n");
		ret = 5;
		goto error_png;
	}

	//printf("png is %ldx%ld\n", width, height);
	int channels = png_get_channels(png_ptr, info_ptr);
	if (channels != 4) {
		//fprintf(stderr, "channels must be 4.\n");
		ret = 6;
		goto error_png;
	}

	/* now write jpeg */
	struct jpeg_compress_struct cinfo;
	struct jpeg_error_mgr jerr;
	JSAMPROW jrow_pointer[1];
	FILE *outfp;

	outfp = fopen(jpegfile, "wb");
	if (!outfp) {
		//perror(jpegfile);
		ret = 7;
		goto error_png;
	}

	cinfo.err = jpeg_std_error(&jerr);
	jpeg_create_compress(&cinfo);
	jpeg_stdio_dest(&cinfo, outfp);

	cinfo.image_width = width;
	cinfo.image_height = height;
	cinfo.input_components = 3;
	cinfo.in_color_space = JCS_RGB;
	jpeg_set_defaults(&cinfo);
	jpeg_set_quality(&cinfo, jpegquality, 1);
	jpeg_start_compress(&cinfo, 1);

	unsigned char *row = malloc(width * 3);
	while (cinfo.next_scanline < cinfo.image_height) {
		int x;
		jrow_pointer[0] = row;
		unsigned char *source = row_pointers[cinfo.next_scanline];
		for (x = 0; x < width; ++x) {
			row[x * 3 + 0] = source[0];
			row[x * 3 + 1] = source[1];
			row[x * 3 + 2] = source[2];
			source += 4;
		}
		jpeg_write_scanlines(&cinfo, jrow_pointer, 1);
	}

	error_jpeg: if (row)
		free(row);

	jpeg_finish_compress(&cinfo);
	jpeg_destroy_compress(&cinfo);
	if (outfp)
		fclose(outfp);
	error_png: if (fpin)
		fclose(fpin);
	png_destroy_read_struct(&png_ptr, &info_ptr, &end_info);
	return ret;
}

int testPng() {
	static png_FILE_p fpin;
	static png_FILE_p fpout;
	//输入文件名
	char *inname = "/sdcard/test/1.png";
	char *outname = "/sdcard/test/1-1.png";
	//读：
	png_structp read_ptr;
	png_infop read_info_ptr, end_info_ptr;
	//写
	png_structp write_ptr;
	png_infop write_info_ptr, write_end_info_ptr;
	//
	png_bytep row_buf;
	png_uint_32 y;
	int num_pass, pass;
	png_uint_32 width, height; //宽度，高度
	int bit_depth, color_type; //位深，颜色类型
	int interlace_type, compression_type, filter_type; //扫描方式，压缩方式，滤波方式
	//读
	row_buf = NULL;
	//打开读文件
	if ((fpin = fopen(inname, "rb")) == NULL) {
		fprintf(stderr, "Could not find input file %s\n", inname);
		return (1);
	}
	//打开写文件
	if ((fpout = fopen(outname, "wb")) == NULL) {
		printf("Could not open output file %s\n", outname);
		fclose(fpin);
		return (1);
	}
	//我们这里不处理未知的块unknown chunk
	//初始化1
	read_ptr = png_create_read_struct(PNG_LIBPNG_VER_STRING, NULL, NULL, NULL);
	write_ptr = png_create_write_struct(PNG_LIBPNG_VER_STRING, NULL, NULL,
			NULL);
	read_info_ptr = png_create_info_struct(read_ptr);
	end_info_ptr = png_create_info_struct(read_ptr);
	write_info_ptr = png_create_info_struct(write_ptr);
	write_end_info_ptr = png_create_info_struct(write_ptr);
	//初始化2
	png_init_io(read_ptr, fpin);
	png_init_io(write_ptr, fpout);
	//读文件有high level(高层）和low level两种，我们选择从底层具体信息中读取。
	//这里我们读取所有可选。
	png_read_info(read_ptr, read_info_ptr);
	//（1）IHDR
	//读取图像宽度(width)，高度(height)，位深(bit_depth)，颜色类型(color_type)，压缩方法(compression_type)
	//滤波器方法(filter_type),隔行扫描方式(interlace_type)
	if (png_get_IHDR(read_ptr, read_info_ptr, &width, &height, &bit_depth,
			&color_type, &interlace_type, &compression_type, &filter_type)) {
		//我们采用默认扫描方式
		png_set_IHDR(write_ptr, write_info_ptr, width, height, bit_depth,
				color_type, PNG_INTERLACE_NONE, compression_type, filter_type);
	}
//	//（2）cHRM
//	//读取白色度信息  白/红/绿/蓝 点的x,y坐标，这里采用整形，不采用浮点数
//	png_fixed_point white_x, white_y, red_x, red_y, green_x, green_y, blue_x,
//			blue_y;
//
//	if (png_get_cHRM_fixed(read_ptr, read_info_ptr, &white_x, &white_y, &red_x,
//			&red_y, &green_x, &green_y, &blue_x, &blue_y)) {
//		png_set_cHRM_fixed(write_ptr, write_info_ptr, white_x, white_y, red_x,
//				red_y, green_x, green_y, blue_x, blue_y);
//	}
//	//（3）gAMA
//	png_fixed_point gamma;
//
//	if (png_get_gAMA_fixed(read_ptr, read_info_ptr, &gamma))
//		png_set_gAMA_fixed(write_ptr, write_info_ptr, gamma);
//	//（4）iCCP
//	png_charp name;
//	png_bytep profile;
//	png_uint_32 proflen;
//
//	if (png_get_iCCP(read_ptr, read_info_ptr, &name, &compression_type,
//			&profile, &proflen)) {
//		png_set_iCCP(write_ptr, write_info_ptr, name, compression_type, profile,
//				proflen);
//	}
//	//(5)sRGB
//	int intent;
//	if (png_get_sRGB(read_ptr, read_info_ptr, &intent))
//		png_set_sRGB(write_ptr, write_info_ptr, intent);
//	//(7)PLTE
//	png_colorp palette;
//	int num_palette;
//
//	if (png_get_PLTE(read_ptr, read_info_ptr, &palette, &num_palette))
//		png_set_PLTE(write_ptr, write_info_ptr, palette, num_palette);
//	//(8)bKGD
//	png_color_16p background;
//
//	if (png_get_bKGD(read_ptr, read_info_ptr, &background)) {
//		png_set_bKGD(write_ptr, write_info_ptr, background);
//	}
//	//(9)hist
//
//	png_uint_16p hist;
//
//	if (png_get_hIST(read_ptr, read_info_ptr, &hist))
//		png_set_hIST(write_ptr, write_info_ptr, hist);
//	//(10)oFFs
//	png_int_32 offset_x, offset_y;
//	int unit_type;
//
//	if (png_get_oFFs(read_ptr, read_info_ptr, &offset_x, &offset_y,
//			&unit_type)) {
//		png_set_oFFs(write_ptr, write_info_ptr, offset_x, offset_y, unit_type);
//	}
//	//(11)pCAL
//	png_charp purpose, units;
//	png_charpp params;
//	png_int_32 X0, X1;
//	int type, nparams;
//
//	if (png_get_pCAL(read_ptr, read_info_ptr, &purpose, &X0, &X1, &type,
//			&nparams, &units, &params)) {
//		png_set_pCAL(write_ptr, write_info_ptr, purpose, X0, X1, type, nparams,
//				units, params);
//	}
//	//(12)pHYs
//
//	png_uint_32 res_x, res_y;
//
//	if (png_get_pHYs(read_ptr, read_info_ptr, &res_x, &res_y, &unit_type))
//		png_set_pHYs(write_ptr, write_info_ptr, res_x, res_y, unit_type);
//	//(13)sBIT
//	png_color_8p sig_bit;
//
//	if (png_get_sBIT(read_ptr, read_info_ptr, &sig_bit))
//		png_set_sBIT(write_ptr, write_info_ptr, sig_bit);
//	//（14）sCAL
//	int unit;
//	png_charp scal_width, scal_height;
//
//	if (png_get_sCAL_s(read_ptr, read_info_ptr, &unit, &scal_width,
//			&scal_height)) {
//		png_set_sCAL_s(write_ptr, write_info_ptr, unit, scal_width,
//				scal_height);
//	}
//	//(15)iTXt
//	png_textp text_ptr;
//	int num_text;
//
//	if (png_get_text(read_ptr, read_info_ptr, &text_ptr, &num_text) > 0) {
//		png_set_text(write_ptr, write_info_ptr, text_ptr, num_text);
//	}
//	//(16)tIME,这里我们不支持RFC1123
//	png_timep mod_time;
//
//	if (png_get_tIME(read_ptr, read_info_ptr, &mod_time)) {
//		png_set_tIME(write_ptr, write_info_ptr, mod_time);
//	}
//	//(17)tRNS
//	png_bytep trans_alpha;
//	int num_trans;
//	png_color_16p trans_color;
//
//	if (png_get_tRNS(read_ptr, read_info_ptr, &trans_alpha, &num_trans,
//			&trans_color)) {
//		int sample_max = (1 << bit_depth);
//		/* libpng doesn't reject a tRNS chunk with out-of-range samples */
//		if (!((color_type == PNG_COLOR_TYPE_GRAY
//				&& (int) trans_color->gray > sample_max)
//				|| (color_type == PNG_COLOR_TYPE_RGB
//						&& ((int) trans_color->red > sample_max
//								|| (int) trans_color->green > sample_max
//								|| (int) trans_color->blue > sample_max))))
//			png_set_tRNS(write_ptr, write_info_ptr, trans_alpha, num_trans,
//					trans_color);
//	}
	//写进新的png文件中
	png_write_info(write_ptr, write_info_ptr);
	//读真正的图像数据
	num_pass = 1;
	for (pass = 0; pass < num_pass; pass++) {
		for (y = 0; y < height; y++) {
			//分配内存
			row_buf = (png_bytep) png_malloc(read_ptr,
					png_get_rowbytes(read_ptr, read_info_ptr));
			png_read_rows(read_ptr, (png_bytepp) &row_buf, NULL, 1);
			png_write_rows(write_ptr, (png_bytepp) &row_buf, 1);
			png_free(read_ptr, row_buf);
			row_buf = NULL;
		}
	}
	//结束
	png_read_end(read_ptr, end_info_ptr);
	//
//	//tTXt
//	if (png_get_text(read_ptr, end_info_ptr, &text_ptr, &num_text) > 0) {
//		png_set_text(write_ptr, write_end_info_ptr, text_ptr, num_text);
//	}
//	//tIME
//	if (png_get_tIME(read_ptr, end_info_ptr, &mod_time)) {
//		png_set_tIME(write_ptr, write_end_info_ptr, mod_time);
//	}
	//
	png_write_end(write_ptr, write_end_info_ptr);
	//回收
	png_free(read_ptr, row_buf);
	row_buf = NULL;
	png_destroy_read_struct(&read_ptr, &read_info_ptr, &end_info_ptr);
	png_destroy_info_struct(write_ptr, &write_end_info_ptr);
	png_destroy_write_struct(&write_ptr, &write_info_ptr);
	//
	fclose(fpin);
	fclose(fpout);
	//测试，比较两个PNG文件是否相同
	if ((fpin = fopen(inname, "rb")) == NULL) {
		printf("Could not find file %s\n", inname);
		return (1);
	}
	if ((fpout = fopen(outname, "rb")) == NULL) {
		printf("Could not find file %s\n", outname);
		fclose(fpin);
		return (1);
	}
	char inbuf[256], outbuf[256];
	for (;;) {
		png_size_t num_in, num_out;
		num_in = fread(inbuf, 1, 1, fpin);
		num_out = fread(outbuf, 1, 1, fpout);
		if (num_in != num_out) {
			printf("\nFiles %s and %s 大小不同\n", inname, outname);
			fclose(fpin);
			fclose(fpout);
			return (0);
		}
		if (!num_in)
			break;
		if (memcmp(inbuf, outbuf, num_in)) {
			printf("\nFiles %s and %s 内容不同\n", inname, outname);
			fclose(fpin);
			fclose(fpout);
			return (0);
		}
	}
	fclose(fpin);
	fclose(fpout);
	return (0);
}
