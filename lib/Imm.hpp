#ifndef IMM_HPP
#define IMM_HPP

#include "common.hpp"
#include "math.hpp"

#define GL_SILENCE_DEPRECATION
#include "glad.h"

////////////////////////////////////////////////////////////////////////////////
///
////////////////////////////////////////////////////////////////////////////////

//TODO: split this stuff out into its own header / implementation file?

struct Texture {
	uint handle;
	int width;
	int height;
};

Texture load_texture(const char * imagePath);
GLuint create_shader(GLenum shaderType, const char * source);
GLuint create_program(const char * vertexShaderString, const char * fragmentShaderString);
GLuint create_program_from_files(const char * vertexShaderPath, const char * fragmentShaderPath);
void gl_error(const char * when);

struct Char {
	uint16_t x, y, width, height;
	int16_t xoffset, yoffset, xadvance;
};

struct Font {
	Char * chars;
	Texture tex;
	//TODO: since Texture struct stores its own width/height, is scaleW/scaleH unnecessary?
	uint16_t lineHeight, base, scaleW, scaleH;
	float kernBias;
};

Font load_font(const char * bmfont);
void free_font(Font &font);

////////////////////////////////////////////////////////////////////////////////
///
////////////////////////////////////////////////////////////////////////////////

enum Constants {
	//line cap
	SQUARE, PROJECT, ROUND,

	//line joint
	MITER, BEVEL, //ROUND,

	//line close
	CLOSE,
};

typedef struct Pixel {
	u8 r, g, b, a;
} Color;

static inline Vec4 vec4(Color c) {
	float f = 1.0f / 255;
	return { c.r * f, c.g * f, c.b * f, c.a * f };
}

static inline Color color(Vec4 v) {
	float f = 255;
	return { (u8)(v.x * f), (u8)(v.y * f), (u8)(v.z * f), (u8)(v.w * f) };
}

//TODO: do proper sRGB conversion here, not just gamma 2.2!
static inline Vec4 srgb_to_linear(Color c) {
    Vec4 n = vec4(c.r, c.g, c.b, c.a) * (1.0f / 255);
    return vec4(powf(n.x, 2.2f), powf(n.y, 2.2f), powf(n.z, 2.2f), n.w);
}

static inline Color linear_to_srgb(Vec4 v) {
	float exp = 1 / 2.2f;
	Vec4 n = vec4(powf(v.x, exp), powf(v.y, exp), powf(v.z, exp), v.w);
	return { (u8) (n.x * 255), (u8) (n.y * 255), (u8) (n.z * 255), (u8) (n.w * 255) };
}

struct Vertex {
	float x, y, z;
	Color c;
	float u, v;
	float f;
};

struct Imm {
	Vertex * data;

	//vertex counts
	int total;
	int used;

	//OpenGL IDs
	uint32_t program;
	uint32_t vao;
	uint32_t vbo;
	Texture tex;

	Mat4 matrix;
	int ww, wh;

	Font font;

	float depth;

	float r; //line width (radius)
	uint8_t cap;
	uint8_t join;
	Color lineColor;

	void init(int size);
	void finalize();

	void begin(int width, int height);
	void end() { flush(); glEnable(GL_CULL_FACE); }

	void backgroundGradient(Color c1, Color c2, float directions); //directions = radians
	void debugStencilVis(Color c, uint8_t val);

	void beginLine();
	void lineVertex(float x, float y);
	void endLine(bool close);
	void line(float x1, float y1, float x2, float y2, Color stroke);

	void drawText(const char * text, float x, float y, float s, Color color);
	void drawText(const char * text, float x, float y, float s, Color color, int len);
	void drawTextCentered(const char * text, float x, float y, float s, Color color);
	void useTexture(Texture texture);

	void drawImage(Texture texture, float x, float y);

	void arc(float x, float y, float dx1, float dy1, float dx2, float dy2);
	void circle(float x, float y, float r, Color fill, Color stroke);
	void rect(float x, float y, float w, float h, Color c);

	void flush();

private:
	//line drawing state
	int lineVertexCount;
	float fx, fy; //first vertex
	float sx, sy, sdx, sdy; //second vertex
	float px, py, pdx, pdy; //previous vertex
	float lx, ly; //last vertex

	int circleDetail(float radius);
	int circleDetail(float radius, float delta);
	void incrementDepth();
	void check(int newVerts);
	void vertex(float x, float y, Color color);
	void vertex(float x, float y, Color color, float u, float v);

	void triangle(float x1, float y1, float x2, float y2, float x3, float y3, Color fill);
	void arcJoin(float x, float y, float dx1, float dy1, float dx2, float dy2);
	void lineCap(float x, float y, float dx, float dy);
};

float text_width(Font font, float scale, const char * text);
float text_width(Font font, float scale, const char * text, int len);

inline Imm create_imm(int newVerts) {
	Imm imm;
	imm.init(newVerts);
	return imm;
}

#endif
