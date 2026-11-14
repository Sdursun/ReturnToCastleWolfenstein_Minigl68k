/*
 * OpenGL 1.x fixed function on top of vulkan.library (Vulkan 1.1), for the
 * Vulkan build of the renderer (jk2sp_vk). An experiment.
 *
 * The renderer is the Quake 3 one: it draws with client vertex arrays and
 * glDrawElements, at most two texture units with GL_MODULATE, GL_REPLACE,
 * GL_DECAL or GL_ADD, the alpha test, and glBegin/glEnd for the odd 2D or
 * debugging draw. Here that becomes three fragment shaders (no texture,
 * one, two) and one vertex shader, pipelines made on demand from the GL
 * state that Vulkan fixes in a pipeline (blending, depth, culling), the
 * texture environment, the alpha test and the matrix as push constants,
 * and the matrix stacks kept on the CPU.
 *
 * A frame is one command buffer and one render pass. Every draw copies its
 * vertexes and indexes into the frame's two buffers (the GPU reads little
 * endian: floats and indexes are swapped on the way). Texture uploads go
 * through a staging buffer and a command buffer of their own, submitted
 * before the frame that uses them.
 *
 * Only this file includes Vulkan's headers; the renderer sees vk_gl.h,
 * whose desktop GL names map to the vk_ functions here.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 */

#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <stdio.h>

#include <exec/exec.h>
#include <intuition/intuition.h>
#include <graphics/displayinfo.h>
#include <graphics/modeid.h>
#include <cybergraphx/cybergraphics.h>
#include <proto/exec.h>
#include <proto/intuition.h>
#include <proto/graphics.h>
#include <proto/cybergraphics.h>

#include <vulkan/vulkan.h>
#include <vulkan/vulkan_amiga.h>

#include "vk_gl.h"
#include "amiga_vk.h"

#include "gen/vk_gl_vert.h"
#include "gen/vk_gl0_frag.h"
#include "gen/vk_gl1_frag.h"
#include "gen/vk_gl2_frag.h"
#include "gen/vk_post_vert.h"
#include "gen/vk_post_frag.h"

#ifndef GL_CLAMP_TO_EDGE
#define GL_CLAMP_TO_EDGE 0x812F
#endif
#ifndef GL_MAX_TEXTURE_UNITS_ARB
#define GL_MAX_TEXTURE_UNITS_ARB 0x84E2
#endif
#ifndef GL_TEXTURE0_ARB
#define GL_TEXTURE0_ARB 0x84C0
#endif

/* ---- sizes -------------------------------------------------------------- */

#define FRAMES          2               /* frames the GPU may still be reading */
#define MAX_IMAGES      4
#define VERTEX_BYTES    32
#define FRAME_VERTEXES  262144          /* 8 MB a frame */
#define FRAME_INDEXES   1048576         /* 2 MB a frame */
#define STAGING_BYTES   ( 8 * 1024 * 1024 )
#define MAX_PIPELINES   256
#define MAX_SAMPLERS    32
#define TEX_SLOTS       8192            /* a power of two */
#define IMM_MAX         8192
#define STACK_DEPTH     32

/* What the GPU reads for a vertex (little endian). */
typedef struct {
	float xyz[3];
	GLubyte rgba[4];
	float st[2][2];
} vkvert_t;

typedef struct {
	GLint size;
	GLenum type;
	GLsizei stride;
	const void *ptr;
	int enabled;
} vkarray_t;

typedef struct {
	GLuint name;                /* 0: the slot is free */
	VkImage image;
	VkDeviceMemory memory;
	VkImageView view;
	int width, height, levels;
	int opaque;                 /* the GL format has no alpha: reads as 1 */
	GLenum minFilter, magFilter, wrapS, wrapT;
	VkDescriptorSet set;
	int setDirty;
} vktex_t;

typedef struct {
	unsigned int key;
	VkPipeline pipeline;
} vkpipe_t;

typedef struct {
	GLenum minFilter, magFilter, wrapS, wrapT;
	VkSampler sampler;
} vksampler_t;

typedef struct {
	float mvp[16];
	int env0, env1, afunc;
	float aref;
	/* GL fog (RTCW): mode 0 off, 1 linear, 2 exp, 3 exp2; the shaders use
	   the eye distance, which the projection leaves in w. std430 puts the
	   vec4 at offset 96, right after these four. */
	int fogMode;
	float fogStart, fogEnd, fogDensity;
	float fogColor[4];
} vkpush_t;

/* ---- GL state ----------------------------------------------------------- */

static struct {
	float stack[3][STACK_DEPTH][16];
	int depth[3];
	int mode;               /* 0 modelview, 1 projection, 2 texture */
	unsigned int serial;    /* bumped when modelview or projection change */
	float mvp[16];
	unsigned int mvpSerial;

	int activeUnit, clientUnit;
	int texEnabled[2];
	GLuint bound[2];
	int env[2];
	int alphaTest, afunc;
	float aref;

	int fog, fogMode;
	float fogStart, fogEnd, fogDensity, fogColor[4];

	int blend, depthTest, cullFace, polyOffset, scissorTest;
	GLenum blendSrc, blendDst, depthFunc, cullMode;
	int depthMask, colorMask;
	float offsetFactor, offsetUnits;
	float clearColor[4];
	float clearDepth;
	float depthNear, depthFar;
	int viewport[4], scissor[4];

	vkarray_t vertex, color, tc[2];
	int lockFirst, lockCount;

	float color4[4];
	float texcoord[2][2];
} gs;

/* ---- Vulkan state ------------------------------------------------------- */

static struct {
	struct Screen *screen;
	struct Window *window;
	int ownWindow;

	VkInstance instance;
	VkPhysicalDevice gpu;
	VkPhysicalDeviceMemoryProperties memoryProperties;
	VkDevice device;
	VkQueue queue;
	VkSurfaceKHR surface;
	VkSwapchainKHR swapchain;
	VkExtent2D extent;
	VkFormat colorFormat, depthFormat;
	uint32_t imageCount;
	VkImage images[MAX_IMAGES];
	VkImageView views[MAX_IMAGES];
	VkImage depthImage;
	VkDeviceMemory depthMemory;
	VkImageView depthView;
	VkRenderPass renderPass;

	/* RTCW: the scene is drawn into sceneImage (through msImage when
	   multisampled, resolved at the end of the pass), and a last pass puts it
	   on the screen through the gamma ramp. */
	VkSampleCountFlagBits samples;
	VkImage sceneImage, msImage;
	VkDeviceMemory sceneMemory, msMemory;
	VkImageView sceneView, msView;
	VkFramebuffer sceneFramebuffer;
	VkRenderPass postPass;
	VkFramebuffer postFramebuffers[MAX_IMAGES];
	VkShaderModule postVert, postFrag;
	VkPipeline postPipeline;
	VkSampler postSampler;
	VkDescriptorSet sceneSet, rampSet;
	vktex_t ramp;
	int rampDirty;

	VkDescriptorSetLayout setLayout;
	VkPipelineLayout layout;
	VkDescriptorPool pool;
	VkShaderModule vert, frag[3];

	VkCommandPool commandPool;
	VkCommandBuffer cmd[FRAMES], upCmd;
	VkFence fence[FRAMES], upFence;
	VkSemaphore acquired[FRAMES], rendered[FRAMES];
	VkBuffer vertexBuffer[FRAMES], indexBuffer[FRAMES], staging;
	VkDeviceMemory vertexMemory[FRAMES], indexMemory[FRAMES], stagingMemory;
	unsigned char *vertexData[FRAMES], *stagingData;
	unsigned short *indexData[FRAMES];
	uint32_t stagingSize, stagingUsed;
	int uploading;              /* upCmd is being recorded */

	int slot;                   /* which of the FRAMES this frame uses */
	int inFrame;
	uint32_t imageIndex;
	uint32_t vertexUsed, indexUsed;

	/* what the frame's command buffer has now */
	VkPipeline curPipeline;
	VkDescriptorSet curSet[2];
	vkpush_t curPush;
	int pushValid;
	int dynDirty;

	vkpipe_t pipelines[MAX_PIPELINES];
	int numPipelines;
	vksampler_t samplers[MAX_SAMPLERS];
	int numSamplers;
	vktex_t white;

	unsigned int statDraws, statVertexes, statUploads;
	char deviceName[128];
	char errorText[256];
} vk;

static vktex_t textures[TEX_SLOTS];

/* Asked for before the context is made (VKGL_SetOptions), and the game's
   gamma ramp (VKGL_SetGamma): kept outside vk, which is cleared then. */
static int optSamples = 1;
static unsigned char rampData[256 * 4];
static int rampSet;
static vkvert_t imm[IMM_MAX];
static int immCount;
static GLenum immMode;
static unsigned short scratchIndexes[IMM_MAX * 3];

struct Library *CyberGfxBase;

static int Fail( const char *what, VkResult r )
{
	if ( !vk.errorText[0] ) {
		sprintf( vk.errorText, "%s failed (%ld)", what, (long)r );
	}
	return 0;
}

#define NEED( call ) do { VkResult r_ = ( call ); if ( r_ != VK_SUCCESS ) return Fail( #call, r_ ); } while ( 0 )

static uint32_t MemoryType( uint32_t bits, VkMemoryPropertyFlags want )
{
	uint32_t i;

	for ( i = 0; i < vk.memoryProperties.memoryTypeCount; i++ ) {
		if ( ( bits & ( 1u << i ) ) && ( vk.memoryProperties.memoryTypes[i].propertyFlags & want ) == want ) {
			return i;
		}
	}
	return 0;
}

/* ---- matrices ----------------------------------------------------------- */

static float *CurMatrix( void )
{
	return gs.stack[gs.mode][gs.depth[gs.mode]];
}

static void Identity( float *m )
{
	memset( m, 0, 16 * sizeof( float ) );
	m[0] = m[5] = m[10] = m[15] = 1.0f;
}

/* r = a * b, column major */
static void MatMul( float *r, const float *a, const float *b )
{
	float t[16];
	int i, j;

	for ( i = 0; i < 4; i++ ) {
		for ( j = 0; j < 4; j++ ) {
			t[i * 4 + j] = a[0 * 4 + j] * b[i * 4 + 0] + a[1 * 4 + j] * b[i * 4 + 1]
				+ a[2 * 4 + j] * b[i * 4 + 2] + a[3 * 4 + j] * b[i * 4 + 3];
		}
	}
	memcpy( r, t, sizeof( t ) );
}

static void MatrixChanged( void )
{
	if ( gs.mode != 2 ) {
		gs.serial++;
	}
}

static void MultCurrent( const float *m )
{
	MatMul( CurMatrix(), CurMatrix(), m );
	MatrixChanged();
}

void vk_glMatrixMode( GLenum mode )
{
	gs.mode = mode == GL_PROJECTION ? 1 : mode == GL_TEXTURE ? 2 : 0;
}

void vk_glLoadIdentity( void )
{
	Identity( CurMatrix() );
	MatrixChanged();
}

void vk_glLoadMatrixf( const GLfloat *m )
{
	memcpy( CurMatrix(), m, 16 * sizeof( float ) );
	MatrixChanged();
}

void vk_glPushMatrix( void )
{
	if ( gs.depth[gs.mode] < STACK_DEPTH - 1 ) {
		memcpy( gs.stack[gs.mode][gs.depth[gs.mode] + 1], CurMatrix(), 16 * sizeof( float ) );
		gs.depth[gs.mode]++;
	}
}

void vk_glPopMatrix( void )
{
	if ( gs.depth[gs.mode] > 0 ) {
		gs.depth[gs.mode]--;
		MatrixChanged();
	}
}

void vk_glOrtho( GLdouble l, GLdouble r, GLdouble b, GLdouble t, GLdouble n, GLdouble f )
{
	float m[16];

	Identity( m );
	m[0] = (float)( 2.0 / ( r - l ) );
	m[5] = (float)( 2.0 / ( t - b ) );
	m[10] = (float)( -2.0 / ( f - n ) );
	m[12] = (float)( -( r + l ) / ( r - l ) );
	m[13] = (float)( -( t + b ) / ( t - b ) );
	m[14] = (float)( -( f + n ) / ( f - n ) );
	MultCurrent( m );
}

void vk_glFrustum( GLdouble l, GLdouble r, GLdouble b, GLdouble t, GLdouble n, GLdouble f )
{
	float m[16];

	memset( m, 0, sizeof( m ) );
	m[0] = (float)( 2.0 * n / ( r - l ) );
	m[5] = (float)( 2.0 * n / ( t - b ) );
	m[8] = (float)( ( r + l ) / ( r - l ) );
	m[9] = (float)( ( t + b ) / ( t - b ) );
	m[10] = (float)( -( f + n ) / ( f - n ) );
	m[11] = -1.0f;
	m[14] = (float)( -2.0 * f * n / ( f - n ) );
	MultCurrent( m );
}

void vk_glTranslatef( GLfloat x, GLfloat y, GLfloat z )
{
	float m[16];

	Identity( m );
	m[12] = x;
	m[13] = y;
	m[14] = z;
	MultCurrent( m );
}

void vk_glRotatef( GLfloat angle, GLfloat x, GLfloat y, GLfloat z )
{
	float m[16], len, c, s, ic;
	double rad = angle * ( 3.14159265358979323846 / 180.0 );

	len = (float)sqrt( x * x + y * y + z * z );
	if ( len == 0.0f ) {
		return;
	}
	x /= len;
	y /= len;
	z /= len;
	c = (float)cos( rad );
	s = (float)sin( rad );
	ic = 1.0f - c;

	Identity( m );
	m[0] = x * x * ic + c;
	m[1] = y * x * ic + z * s;
	m[2] = x * z * ic - y * s;
	m[4] = x * y * ic - z * s;
	m[5] = y * y * ic + c;
	m[6] = y * z * ic + x * s;
	m[8] = x * z * ic + y * s;
	m[9] = y * z * ic - x * s;
	m[10] = z * z * ic + c;
	MultCurrent( m );
}

/* ---- texture uploads ---------------------------------------------------- */

/* Sends what the upload command buffer holds and waits for it: the staging
   buffer is free again afterwards. */
static void FlushUploads( void )
{
	VkSubmitInfo submit = { VK_STRUCTURE_TYPE_SUBMIT_INFO };

	if ( !vk.uploading ) {
		return;
	}
	vkEndCommandBuffer( vk.upCmd );
	submit.commandBufferCount = 1;
	submit.pCommandBuffers = &vk.upCmd;
	vkResetFences( vk.device, 1, &vk.upFence );
	vkQueueSubmit( vk.queue, 1, &submit, vk.upFence );
	vkWaitForFences( vk.device, 1, &vk.upFence, VK_TRUE, 30000000000ull );
	vk.uploading = 0;
	vk.stagingUsed = 0;
}

static void BeginUploads( void )
{
	VkCommandBufferBeginInfo bi = { VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO };

	if ( vk.uploading ) {
		return;
	}
	bi.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
	vkResetCommandBuffer( vk.upCmd, 0 );
	vkBeginCommandBuffer( vk.upCmd, &bi );
	vk.uploading = 1;
}

static int MakeStaging( uint32_t size )
{
	VkBufferCreateInfo bi = { VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO };
	VkMemoryAllocateInfo ai = { VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO };
	VkMemoryRequirements req;

	if ( vk.staging ) {
		vkDestroyBuffer( vk.device, vk.staging, NULL );
		vkFreeMemory( vk.device, vk.stagingMemory, NULL );
		vk.staging = 0;
	}
	bi.size = size;
	bi.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
	NEED( vkCreateBuffer( vk.device, &bi, NULL, &vk.staging ) );
	vkGetBufferMemoryRequirements( vk.device, vk.staging, &req );
	ai.allocationSize = req.size;
	ai.memoryTypeIndex = MemoryType( req.memoryTypeBits,
		VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT );
	NEED( vkAllocateMemory( vk.device, &ai, NULL, &vk.stagingMemory ) );
	NEED( vkBindBufferMemory( vk.device, vk.staging, vk.stagingMemory, 0 ) );
	NEED( vkMapMemory( vk.device, vk.stagingMemory, 0, VK_WHOLE_SIZE, 0, (void **)&vk.stagingData ) );
	vk.stagingSize = size;
	vk.stagingUsed = 0;
	return 1;
}

static void LevelBarrier( vktex_t *t, int level, int count, VkImageLayout from, VkImageLayout to )
{
	VkImageMemoryBarrier b = { VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER };
	VkPipelineStageFlags src, dst;

	b.oldLayout = from;
	b.newLayout = to;
	b.srcQueueFamilyIndex = b.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
	b.image = t->image;
	b.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
	b.subresourceRange.baseMipLevel = level;
	b.subresourceRange.levelCount = count;
	b.subresourceRange.layerCount = 1;
	if ( to == VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL ) {
		b.srcAccessMask = from == VK_IMAGE_LAYOUT_UNDEFINED ? 0 : VK_ACCESS_SHADER_READ_BIT;
		b.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
		src = from == VK_IMAGE_LAYOUT_UNDEFINED ? VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT : VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
		dst = VK_PIPELINE_STAGE_TRANSFER_BIT;
	} else {
		b.srcAccessMask = from == VK_IMAGE_LAYOUT_UNDEFINED ? 0 : VK_ACCESS_TRANSFER_WRITE_BIT;
		b.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
		src = from == VK_IMAGE_LAYOUT_UNDEFINED ? VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT : VK_PIPELINE_STAGE_TRANSFER_BIT;
		dst = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
	}
	vkCmdPipelineBarrier( vk.upCmd, src, dst, 0, 0, NULL, 0, NULL, 1, &b );
}

static void FreeTexture( vktex_t *t )
{
	if ( !t->image ) {
		return;
	}
	/* the GPU may still be drawing with it */
	FlushUploads();
	vkDeviceWaitIdle( vk.device );
	vkDestroyImageView( vk.device, t->view, NULL );
	vkDestroyImage( vk.device, t->image, NULL );
	vkFreeMemory( vk.device, t->memory, NULL );
	t->image = 0;
	t->view = 0;
	t->memory = 0;
	t->setDirty = 1;
}

/* An RGBA8 image of that size with its whole mip chain, every level
   readable (the levels never uploaded stay unused: the sampler's LOD range
   follows the filter). */
static int MakeTexture( vktex_t *t, int width, int height )
{
	VkImageCreateInfo ii = { VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO };
	VkMemoryAllocateInfo ai = { VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO };
	VkImageViewCreateInfo vi = { VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO };
	VkMemoryRequirements req;
	int levels = 1, w = width, h = height;

	FreeTexture( t );
	while ( w > 1 || h > 1 ) {
		w = w > 1 ? w >> 1 : 1;
		h = h > 1 ? h >> 1 : 1;
		levels++;
	}
	ii.imageType = VK_IMAGE_TYPE_2D;
	ii.format = VK_FORMAT_R8G8B8A8_UNORM;
	ii.extent.width = width;
	ii.extent.height = height;
	ii.extent.depth = 1;
	ii.mipLevels = levels;
	ii.arrayLayers = 1;
	ii.samples = VK_SAMPLE_COUNT_1_BIT;
	ii.tiling = VK_IMAGE_TILING_OPTIMAL;
	ii.usage = VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;
	NEED( vkCreateImage( vk.device, &ii, NULL, &t->image ) );
	memset( &req, 0, sizeof( req ) );
	vkGetImageMemoryRequirements( vk.device, t->image, &req );
#ifdef VKGL_IMAGE_LOG
	{
		/* DEBUG (A1200 server crash in vkBindImageMemory, from the Jedi
		   Outcast port): the first images' memory requirements, to
		   vk_debug.txt in the current directory. Build with
		   -DVKGL_IMAGE_LOG to turn it on. */
		static int logged;

		if ( logged < 12 ) {
			FILE *f = fopen( "vk_debug.txt", "a" );

			logged++;
			if ( f ) {
				fprintf( f, "image %dx%d levels %d handle %08lx%08lx size %08lx%08lx alignment %08lx%08lx types %08lx\n",
					width, height, levels,
					(unsigned long)( (uint64_t)t->image >> 32 ), (unsigned long)(uint64_t)t->image,
					(unsigned long)( req.size >> 32 ), (unsigned long)req.size,
					(unsigned long)( req.alignment >> 32 ), (unsigned long)req.alignment,
					(unsigned long)req.memoryTypeBits );
				fclose( f );
			}
		}
	}
#endif
	/* the server divides by the alignment when the memory is bound */
	if ( !req.size || !req.alignment ) {
		vkDestroyImage( vk.device, t->image, NULL );
		t->image = 0;
		return Fail( "vkGetImageMemoryRequirements", VK_ERROR_UNKNOWN );
	}
	ai.allocationSize = req.size;
	ai.memoryTypeIndex = MemoryType( req.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT );
	NEED( vkAllocateMemory( vk.device, &ai, NULL, &t->memory ) );
	NEED( vkBindImageMemory( vk.device, t->image, t->memory, 0 ) );
	vi.image = t->image;
	vi.viewType = VK_IMAGE_VIEW_TYPE_2D;
	vi.format = ii.format;
	vi.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
	vi.subresourceRange.levelCount = levels;
	vi.subresourceRange.layerCount = 1;
	NEED( vkCreateImageView( vk.device, &vi, NULL, &t->view ) );
	t->width = width;
	t->height = height;
	t->levels = levels;
	t->setDirty = 1;

	BeginUploads();
	LevelBarrier( t, 0, levels, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL );
	return 1;
}

/* Pixels (RGBA, or RGB made RGBA) into a part of one level. */
static void UploadPixels( vktex_t *t, int level, int x, int y, int width, int height, GLenum format,
	const GLvoid *pixels )
{
	VkBufferImageCopy region;
	uint32_t bytes = (uint32_t)width * height * 4;
	unsigned char *to;

	if ( !t->image || !pixels || level >= t->levels || width <= 0 || height <= 0 ) {
		return;
	}
	if ( bytes > vk.stagingSize ) {
		FlushUploads();
		vkDeviceWaitIdle( vk.device );
		if ( !MakeStaging( bytes ) ) {
			return;
		}
	} else if ( vk.stagingUsed + bytes > vk.stagingSize ) {
		FlushUploads();
	}
	BeginUploads();

	to = vk.stagingData + vk.stagingUsed;
	if ( format == GL_RGB ) {
		const unsigned char *from = (const unsigned char *)pixels;
		long i, n = (long)width * height;

		for ( i = 0; i < n; i++, from += 3, to += 4 ) {
			to[0] = from[0];
			to[1] = from[1];
			to[2] = from[2];
			to[3] = 255;
		}
	} else {
		memcpy( to, pixels, bytes );
		if ( t->opaque ) {
			/* what the data has there does not count (the videos have 0) */
			uint32_t i;

			for ( i = 3; i < bytes; i += 4 ) {
				to[i] = 255;
			}
		}
	}

	memset( &region, 0, sizeof( region ) );
	region.bufferOffset = vk.stagingUsed;
	region.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
	region.imageSubresource.mipLevel = level;
	region.imageSubresource.layerCount = 1;
	region.imageOffset.x = x;
	region.imageOffset.y = y;
	region.imageExtent.width = width;
	region.imageExtent.height = height;
	region.imageExtent.depth = 1;
	LevelBarrier( t, level, 1, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL );
	vkCmdCopyBufferToImage( vk.upCmd, vk.staging, t->image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region );
	LevelBarrier( t, level, 1, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL );
	vk.stagingUsed += ( bytes + 3 ) & ~3u;
	vk.statUploads++;
}

/* The texture of that name; made when asked for and not there. */
static vktex_t *FindTexture( GLuint name, int create )
{
	unsigned int i = ( name * 2654435761u ) >> 19;      /* 13 bits */
	int n;

	if ( !name ) {
		return NULL;
	}
	for ( n = 0; n < TEX_SLOTS; n++, i = ( i + 1 ) & ( TEX_SLOTS - 1 ) ) {
		vktex_t *t = &textures[i];

		if ( t->name == name ) {
			return t;
		}
		if ( !t->name ) {
			if ( !create ) {
				return NULL;
			}
			t->name = name;
			t->minFilter = GL_NEAREST_MIPMAP_LINEAR;
			t->magFilter = GL_LINEAR;
			t->wrapS = t->wrapT = GL_REPEAT;
			t->setDirty = 1;
			return t;
		}
	}
	return NULL;
}

void vk_glBindTexture( GLenum target, GLuint tex )
{
	gs.bound[gs.activeUnit] = tex;
}

void vk_glDeleteTextures( GLsizei n, const GLuint *names )
{
	int i;

	for ( i = 0; i < n; i++ ) {
		vktex_t *t = FindTexture( names[i], 0 );

		if ( t ) {
			/* the slot keeps its name and its descriptor set */
			FreeTexture( t );
		}
	}
}

void vk_glTexImage2D( GLenum target, GLint level, GLint internalformat, GLsizei width, GLsizei height,
	GLint border, GLenum format, GLenum type, const GLvoid *pixels )
{
	vktex_t *t = FindTexture( gs.bound[gs.activeUnit], 1 );

	if ( !t || !vk.device ) {
		return;
	}
	if ( level == 0 && ( !t->image || t->width != width || t->height != height ) ) {
		if ( !MakeTexture( t, width, height ) ) {
			return;
		}
	}
	if ( level == 0 ) {
		t->opaque = internalformat == 3 || internalformat == GL_RGB || internalformat == GL_RGB8
			|| internalformat == GL_RGB5 || internalformat == GL_RGB4 || internalformat == GL_R3_G3_B2;
	}
	UploadPixels( t, level, 0, 0, width, height, format, pixels );
}

void vk_glTexSubImage2D( GLenum target, GLint level, GLint x, GLint y, GLsizei width, GLsizei height,
	GLenum format, GLenum type, const GLvoid *pixels )
{
	vktex_t *t = FindTexture( gs.bound[gs.activeUnit], 0 );

	if ( t ) {
		UploadPixels( t, level, x, y, width, height, format, pixels );
	}
}

void vk_glTexParameterf( GLenum target, GLenum pname, GLfloat param )
{
	vktex_t *t = FindTexture( gs.bound[gs.activeUnit], 1 );
	GLenum v = (GLenum)param;
	GLenum *field;

	if ( !t ) {
		return;
	}
	switch ( pname ) {
	case GL_TEXTURE_MIN_FILTER: field = &t->minFilter; break;
	case GL_TEXTURE_MAG_FILTER: field = &t->magFilter; break;
	case GL_TEXTURE_WRAP_S:     field = &t->wrapS; break;
	case GL_TEXTURE_WRAP_T:     field = &t->wrapT; break;
	default:                    return;
	}
	if ( *field != v ) {
		*field = v;
		t->setDirty = 1;
	}
}

void vk_glTexParameterfv( GLenum target, GLenum pname, const GLfloat *params )
{
	if ( pname != GL_TEXTURE_BORDER_COLOR ) {
		vk_glTexParameterf( target, pname, params[0] );
	}
}

static VkSampler SamplerFor( const vktex_t *t )
{
	VkSamplerCreateInfo si = { VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO };
	vksampler_t *s;
	int i, mips;

	for ( i = 0; i < vk.numSamplers; i++ ) {
		s = &vk.samplers[i];
		if ( s->minFilter == t->minFilter && s->magFilter == t->magFilter && s->wrapS == t->wrapS && s->wrapT == t->wrapT ) {
			return s->sampler;
		}
	}
	if ( vk.numSamplers == MAX_SAMPLERS ) {
		return vk.samplers[0].sampler;
	}
	s = &vk.samplers[vk.numSamplers];
	s->minFilter = t->minFilter;
	s->magFilter = t->magFilter;
	s->wrapS = t->wrapS;
	s->wrapT = t->wrapT;

	mips = t->minFilter != GL_NEAREST && t->minFilter != GL_LINEAR;
	si.magFilter = t->magFilter == GL_NEAREST ? VK_FILTER_NEAREST : VK_FILTER_LINEAR;
	si.minFilter = ( t->minFilter == GL_NEAREST || t->minFilter == GL_NEAREST_MIPMAP_NEAREST
		|| t->minFilter == GL_NEAREST_MIPMAP_LINEAR ) ? VK_FILTER_NEAREST : VK_FILTER_LINEAR;
	si.mipmapMode = ( t->minFilter == GL_NEAREST_MIPMAP_LINEAR || t->minFilter == GL_LINEAR_MIPMAP_LINEAR )
		? VK_SAMPLER_MIPMAP_MODE_LINEAR : VK_SAMPLER_MIPMAP_MODE_NEAREST;
	si.addressModeU = t->wrapS == GL_REPEAT ? VK_SAMPLER_ADDRESS_MODE_REPEAT : VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
	si.addressModeV = t->wrapT == GL_REPEAT ? VK_SAMPLER_ADDRESS_MODE_REPEAT : VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
	si.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
	si.maxLod = mips ? 1000.0f : 0.25f;
	si.maxAnisotropy = 1.0f;
	if ( vkCreateSampler( vk.device, &si, NULL, &s->sampler ) != VK_SUCCESS ) {
		return vk.samplers[0].sampler;
	}
	vk.numSamplers++;
	return s->sampler;
}

/* The descriptor set that shows the texture bound to a unit (the white
   texture when there is none). */
static VkDescriptorSet SetFor( GLuint name )
{
	vktex_t *t = FindTexture( name, 0 );

	if ( !t || !t->image ) {
		t = &vk.white;
	}
	if ( !t->set ) {
		VkDescriptorSetAllocateInfo ai = { VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO };

		ai.descriptorPool = vk.pool;
		ai.descriptorSetCount = 1;
		ai.pSetLayouts = &vk.setLayout;
		if ( vkAllocateDescriptorSets( vk.device, &ai, &t->set ) != VK_SUCCESS ) {
			t->set = 0;
			return vk.white.set;
		}
		t->setDirty = 1;
	}
	if ( t->setDirty ) {
		VkWriteDescriptorSet w = { VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET };
		VkDescriptorImageInfo info;

		info.sampler = SamplerFor( t );
		info.imageView = t->view;
		info.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
		w.dstSet = t->set;
		w.descriptorCount = 1;
		w.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
		w.pImageInfo = &info;
		vkUpdateDescriptorSets( vk.device, 1, &w, 0, NULL );
		t->setDirty = 0;
	}
	return t->set;
}

/* ---- pipelines ---------------------------------------------------------- */

static int BlendIndex( GLenum f )
{
	if ( f == GL_ZERO ) return 0;
	if ( f == GL_ONE ) return 1;
	if ( f >= GL_SRC_COLOR && f <= GL_SRC_ALPHA_SATURATE ) return 2 + (int)( f - GL_SRC_COLOR );
	return 1;
}

static const VkBlendFactor blendFactors[11] = {
	VK_BLEND_FACTOR_ZERO, VK_BLEND_FACTOR_ONE,
	VK_BLEND_FACTOR_SRC_COLOR, VK_BLEND_FACTOR_ONE_MINUS_SRC_COLOR,
	VK_BLEND_FACTOR_SRC_ALPHA, VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA,
	VK_BLEND_FACTOR_DST_ALPHA, VK_BLEND_FACTOR_ONE_MINUS_DST_ALPHA,
	VK_BLEND_FACTOR_DST_COLOR, VK_BLEND_FACTOR_ONE_MINUS_DST_COLOR,
	VK_BLEND_FACTOR_SRC_ALPHA_SATURATE
};

static const VkCompareOp compareOps[8] = {
	VK_COMPARE_OP_NEVER, VK_COMPARE_OP_LESS, VK_COMPARE_OP_EQUAL, VK_COMPARE_OP_LESS_OR_EQUAL,
	VK_COMPARE_OP_GREATER, VK_COMPARE_OP_NOT_EQUAL, VK_COMPARE_OP_GREATER_OR_EQUAL, VK_COMPARE_OP_ALWAYS
};

/* The key's fields. */
#define K_NTEX( k )     ( ( k ) & 3 )
#define K_BLEND( k )    ( ( ( k ) >> 2 ) & 1 )
#define K_SRC( k )      ( ( ( k ) >> 3 ) & 15 )
#define K_DST( k )      ( ( ( k ) >> 7 ) & 15 )
#define K_ZTEST( k )    ( ( ( k ) >> 11 ) & 1 )
#define K_ZFUNC( k )    ( ( ( k ) >> 12 ) & 7 )
#define K_ZMASK( k )    ( ( ( k ) >> 15 ) & 1 )
#define K_CULL( k )     ( ( ( k ) >> 16 ) & 3 )
#define K_OFFSET( k )   ( ( ( k ) >> 18 ) & 1 )
#define K_LINES( k )    ( ( ( k ) >> 19 ) & 1 )
#define K_NOCOLOR( k )  ( ( ( k ) >> 20 ) & 1 )

static unsigned int StateKey( int ntex, int lines )
{
	unsigned int k = (unsigned int)ntex;

	if ( gs.blend ) {
		k |= 1u << 2 | (unsigned int)BlendIndex( gs.blendSrc ) << 3 | (unsigned int)BlendIndex( gs.blendDst ) << 7;
	}
	if ( gs.depthTest ) {
		k |= 1u << 11 | ( ( gs.depthFunc - GL_NEVER ) & 7u ) << 12 | (unsigned int)( gs.depthMask ? 1 : 0 ) << 15;
	}
	if ( gs.cullFace ) {
		k |= ( gs.cullMode == GL_FRONT ? 1u : 2u ) << 16;
	}
	if ( gs.polyOffset ) {
		k |= 1u << 18;
	}
	if ( lines ) {
		k |= 1u << 19;
	}
	if ( !gs.colorMask ) {
		k |= 1u << 20;
	}
	return k;
}

static VkPipeline MakePipeline( unsigned int key )
{
	VkPipelineShaderStageCreateInfo stages[2];
	VkVertexInputBindingDescription binding = { 0, VERTEX_BYTES, VK_VERTEX_INPUT_RATE_VERTEX };
	VkVertexInputAttributeDescription attrs[4] = {
		{ 0, 0, VK_FORMAT_R32G32B32_SFLOAT, 0 },
		{ 1, 0, VK_FORMAT_R8G8B8A8_UNORM, 12 },
		{ 2, 0, VK_FORMAT_R32G32_SFLOAT, 16 },
		{ 3, 0, VK_FORMAT_R32G32_SFLOAT, 24 }
	};
	VkPipelineVertexInputStateCreateInfo vi = { VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO };
	VkPipelineInputAssemblyStateCreateInfo ia = { VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO };
	VkPipelineViewportStateCreateInfo vp = { VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO };
	VkPipelineRasterizationStateCreateInfo rs = { VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO };
	VkPipelineMultisampleStateCreateInfo ms = { VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO };
	VkPipelineDepthStencilStateCreateInfo dss = { VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO };
	VkPipelineColorBlendAttachmentState att;
	VkPipelineColorBlendStateCreateInfo cb = { VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO };
	VkDynamicState dyn[3] = { VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR, VK_DYNAMIC_STATE_DEPTH_BIAS };
	VkPipelineDynamicStateCreateInfo ds = { VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO };
	VkGraphicsPipelineCreateInfo pi = { VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO };
	VkPipeline p = 0;

	memset( stages, 0, sizeof( stages ) );
	stages[0].sType = stages[1].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
	stages[0].stage = VK_SHADER_STAGE_VERTEX_BIT;
	stages[0].module = vk.vert;
	stages[0].pName = "main";
	stages[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
	stages[1].module = vk.frag[K_NTEX( key )];
	stages[1].pName = "main";

	vi.vertexBindingDescriptionCount = 1;
	vi.pVertexBindingDescriptions = &binding;
	vi.vertexAttributeDescriptionCount = 4;
	vi.pVertexAttributeDescriptions = attrs;
	ia.topology = K_LINES( key ) ? VK_PRIMITIVE_TOPOLOGY_LINE_LIST : VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
	vp.viewportCount = 1;
	vp.scissorCount = 1;

	rs.polygonMode = VK_POLYGON_MODE_FILL;
	rs.cullMode = K_CULL( key ) == 1 ? VK_CULL_MODE_FRONT_BIT : K_CULL( key ) == 2 ? VK_CULL_MODE_BACK_BIT : VK_CULL_MODE_NONE;
	/* GL's front is counter-clockwise with y up; the vertex shader turns y
	   over for Vulkan, whose rule counts with y down: the same name fits */
	rs.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
	rs.depthBiasEnable = K_OFFSET( key ) ? VK_TRUE : VK_FALSE;
	rs.lineWidth = 1.0f;
	ms.rasterizationSamples = vk.samples;

	dss.depthTestEnable = K_ZTEST( key ) ? VK_TRUE : VK_FALSE;
	dss.depthWriteEnable = K_ZMASK( key ) ? VK_TRUE : VK_FALSE;
	dss.depthCompareOp = compareOps[K_ZFUNC( key )];
	dss.maxDepthBounds = 1.0f;

	memset( &att, 0, sizeof( att ) );
	att.colorWriteMask = K_NOCOLOR( key ) ? 0 : 0xF;
	if ( K_BLEND( key ) ) {
		att.blendEnable = VK_TRUE;
		att.srcColorBlendFactor = att.srcAlphaBlendFactor = blendFactors[K_SRC( key )];
		att.dstColorBlendFactor = att.dstAlphaBlendFactor = blendFactors[K_DST( key )];
		att.colorBlendOp = att.alphaBlendOp = VK_BLEND_OP_ADD;
	}
	cb.attachmentCount = 1;
	cb.pAttachments = &att;
	ds.dynamicStateCount = 3;
	ds.pDynamicStates = dyn;

	pi.stageCount = 2;
	pi.pStages = stages;
	pi.pVertexInputState = &vi;
	pi.pInputAssemblyState = &ia;
	pi.pViewportState = &vp;
	pi.pRasterizationState = &rs;
	pi.pMultisampleState = &ms;
	pi.pDepthStencilState = &dss;
	pi.pColorBlendState = &cb;
	pi.pDynamicState = &ds;
	pi.layout = vk.layout;
	pi.renderPass = vk.renderPass;
	if ( vkCreateGraphicsPipelines( vk.device, VK_NULL_HANDLE, 1, &pi, NULL, &p ) != VK_SUCCESS ) {
		return 0;
	}
	return p;
}

static VkPipeline PipelineFor( unsigned int key )
{
	static int last;
	int i;

	if ( last < vk.numPipelines && vk.pipelines[last].key == key ) {
		return vk.pipelines[last].pipeline;
	}
	for ( i = 0; i < vk.numPipelines; i++ ) {
		if ( vk.pipelines[i].key == key ) {
			last = i;
			return vk.pipelines[i].pipeline;
		}
	}
	if ( vk.numPipelines == MAX_PIPELINES ) {
		return 0;
	}
	vk.pipelines[vk.numPipelines].key = key;
	vk.pipelines[vk.numPipelines].pipeline = MakePipeline( key );
	last = vk.numPipelines++;
	return vk.pipelines[last].pipeline;
}

/* ---- the frame ---------------------------------------------------------- */

static void BeginFrame( void )
{
	VkCommandBufferBeginInfo bi = { VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO };
	VkRenderPassBeginInfo rbi = { VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO };
	VkClearValue clear[3];
	VkDeviceSize zero = 0;
	VkCommandBuffer cmd;

	if ( vk.inFrame || !vk.device ) {
		return;
	}
	cmd = vk.cmd[vk.slot];
	if ( vk.rampDirty && vk.ramp.image ) {
		UploadPixels( &vk.ramp, 0, 0, 0, 256, 1, GL_RGBA, rampData );
		vk.rampDirty = 0;
	}
	/* the frame that last used this slot's buffers */
	vkWaitForFences( vk.device, 1, &vk.fence[vk.slot], VK_TRUE, 30000000000ull );
	vkResetFences( vk.device, 1, &vk.fence[vk.slot] );
	vkAcquireNextImageKHR( vk.device, vk.swapchain, ~0ull, vk.acquired[vk.slot], VK_NULL_HANDLE, &vk.imageIndex );

	bi.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
	vkResetCommandBuffer( cmd, 0 );
	vkBeginCommandBuffer( cmd, &bi );

	memset( clear, 0, sizeof( clear ) );
	clear[0].color.float32[3] = 1.0f;
	clear[1].depthStencil.depth = 1.0f;
	rbi.renderPass = vk.renderPass;
	rbi.framebuffer = vk.sceneFramebuffer;
	rbi.renderArea.extent = vk.extent;
	rbi.clearValueCount = vk.samples != VK_SAMPLE_COUNT_1_BIT ? 3 : 2;
	rbi.pClearValues = clear;
	vkCmdBeginRenderPass( cmd, &rbi, VK_SUBPASS_CONTENTS_INLINE );
	vkCmdBindVertexBuffers( cmd, 0, 1, &vk.vertexBuffer[vk.slot], &zero );
	vkCmdBindIndexBuffer( cmd, vk.indexBuffer[vk.slot], 0, VK_INDEX_TYPE_UINT16 );

	vk.inFrame = 1;
	vk.vertexUsed = vk.indexUsed = 0;
	vk.curPipeline = 0;
	vk.curSet[0] = vk.curSet[1] = 0;
	vk.pushValid = 0;
	vk.dynDirty = 1;
}

void VKGL_SwapBuffers( void )
{
	VkSubmitInfo submit = { VK_STRUCTURE_TYPE_SUBMIT_INFO };
	VkPresentInfoKHR pi = { VK_STRUCTURE_TYPE_PRESENT_INFO_KHR };
	VkPipelineStageFlags stage = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
	VkCommandBuffer cmd;

	if ( !vk.device ) {
		return;
	}
	BeginFrame();
	cmd = vk.cmd[vk.slot];
	vkCmdEndRenderPass( cmd );
	{
		/* the scene onto the screen, through the gamma ramp */
		VkRenderPassBeginInfo rbi = { VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO };
		VkViewport v;
		VkRect2D r;
		VkDescriptorSet sets[2];

		rbi.renderPass = vk.postPass;
		rbi.framebuffer = vk.postFramebuffers[vk.imageIndex];
		rbi.renderArea.extent = vk.extent;
		vkCmdBeginRenderPass( cmd, &rbi, VK_SUBPASS_CONTENTS_INLINE );
		vkCmdBindPipeline( cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, vk.postPipeline );
		memset( &v, 0, sizeof( v ) );
		v.width = (float)vk.extent.width;
		v.height = (float)vk.extent.height;
		v.maxDepth = 1.0f;
		vkCmdSetViewport( cmd, 0, 1, &v );
		memset( &r, 0, sizeof( r ) );
		r.extent = vk.extent;
		vkCmdSetScissor( cmd, 0, 1, &r );
		sets[0] = vk.sceneSet;
		sets[1] = vk.rampSet;
		vkCmdBindDescriptorSets( cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, vk.layout, 0, 2, sets, 0, NULL );
		vkCmdDraw( cmd, 3, 1, 0, 0 );
		vkCmdEndRenderPass( cmd );
	}
	vkEndCommandBuffer( cmd );

	/* the textures this frame draws with */
	FlushUploads();

	submit.waitSemaphoreCount = 1;
	submit.pWaitSemaphores = &vk.acquired[vk.slot];
	submit.pWaitDstStageMask = &stage;
	submit.commandBufferCount = 1;
	submit.pCommandBuffers = &cmd;
	submit.signalSemaphoreCount = 1;
	submit.pSignalSemaphores = &vk.rendered[vk.slot];
	vkQueueSubmit( vk.queue, 1, &submit, vk.fence[vk.slot] );

	pi.waitSemaphoreCount = 1;
	pi.pWaitSemaphores = &vk.rendered[vk.slot];
	pi.swapchainCount = 1;
	pi.pSwapchains = &vk.swapchain;
	pi.pImageIndices = &vk.imageIndex;
	vkQueuePresentKHR( vk.queue, &pi );

	vk.inFrame = 0;
	vk.slot = ( vk.slot + 1 ) % FRAMES;
}

/* Viewport, scissor and depth bias, when they changed. GL counts rows from
   the bottom. */
static void SetDynamic( VkCommandBuffer cmd )
{
	VkViewport v;
	VkRect2D r;
	int x, y, w, h, H = (int)vk.extent.height, W = (int)vk.extent.width;

	v.x = (float)gs.viewport[0];
	v.y = (float)( H - gs.viewport[1] - gs.viewport[3] );
	v.width = (float)gs.viewport[2];
	v.height = (float)gs.viewport[3];
	v.minDepth = gs.depthNear;
	v.maxDepth = gs.depthFar;
	if ( v.width < 1.0f ) v.width = 1.0f;
	if ( v.height < 1.0f ) v.height = 1.0f;
	vkCmdSetViewport( cmd, 0, 1, &v );

	if ( gs.scissorTest ) {
		x = gs.scissor[0];
		y = H - gs.scissor[1] - gs.scissor[3];
		w = gs.scissor[2];
		h = gs.scissor[3];
		if ( x < 0 ) { w += x; x = 0; }
		if ( y < 0 ) { h += y; y = 0; }
		if ( x + w > W ) w = W - x;
		if ( y + h > H ) h = H - y;
		if ( w < 0 ) w = 0;
		if ( h < 0 ) h = 0;
	} else {
		x = y = 0;
		w = W;
		h = H;
	}
	r.offset.x = x;
	r.offset.y = y;
	r.extent.width = (uint32_t)w;
	r.extent.height = (uint32_t)h;
	vkCmdSetScissor( cmd, 0, 1, &r );
	vkCmdSetDepthBias( cmd, gs.offsetUnits, 0.0f, gs.offsetFactor );
	vk.dynDirty = 0;
}

/* Everything a draw needs but its vertexes: the frame, the pipeline for the
   GL state, the textures, the push constants. 0 when it cannot be drawn. */
static int PrepareDraw( int lines )
{
	VkCommandBuffer cmd;
	VkPipeline pipeline;
	vkpush_t push;
	int ntex = gs.texEnabled[0] ? ( gs.texEnabled[1] ? 2 : 1 ) : 0;
	int u;

	BeginFrame();
	if ( !vk.inFrame ) {
		return 0;
	}
	cmd = vk.cmd[vk.slot];

	pipeline = PipelineFor( StateKey( ntex, lines ) );
	if ( !pipeline ) {
		return 0;
	}
	if ( pipeline != vk.curPipeline ) {
		vkCmdBindPipeline( cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline );
		vk.curPipeline = pipeline;
	}
	if ( vk.dynDirty ) {
		SetDynamic( cmd );
	}
	for ( u = 0; u < ntex; u++ ) {
		VkDescriptorSet set = SetFor( gs.bound[u] );

		if ( set != vk.curSet[u] ) {
			vkCmdBindDescriptorSets( cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, vk.layout, u, 1, &set, 0, NULL );
			vk.curSet[u] = set;
		}
	}

	if ( gs.mvpSerial != gs.serial ) {
		MatMul( gs.mvp, gs.stack[1][gs.depth[1]], gs.stack[0][gs.depth[0]] );
		gs.mvpSerial = gs.serial;
	}
	memcpy( push.mvp, gs.mvp, sizeof( push.mvp ) );
	push.env0 = gs.env[0];
	push.env1 = gs.env[1];
	push.afunc = gs.alphaTest ? gs.afunc : 0;
	push.aref = gs.aref;
	push.fogMode = gs.fog ? gs.fogMode : 0;
	push.fogStart = gs.fogStart;
	push.fogEnd = gs.fogEnd > gs.fogStart ? gs.fogEnd : gs.fogStart + 1.0f;
	push.fogDensity = gs.fogDensity;
	memcpy( push.fogColor, gs.fogColor, sizeof( push.fogColor ) );
	if ( !vk.pushValid || memcmp( &push, &vk.curPush, sizeof( push ) ) ) {
		vkpush_t le;

		vkAmigaCopyLE32( &le, &push, sizeof( push ) / 4 );
		vkCmdPushConstants( cmd, vk.layout, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof( le ), &le );
		vk.curPush = push;
		vk.pushValid = 1;
	}
	return 1;
}

static GLubyte ColorByte( float c )
{
	return (GLubyte)( c <= 0.0f ? 0 : c >= 1.0f ? 255 : (int)( c * 255.0f + 0.5f ) );
}

#define SWAP32( v ) __builtin_bswap32( v )

/* count vertexes of the client arrays, from first, into the frame's
   buffer, in the GPU's byte order. */
static void CopyClientVertexes( uint32_t *to, int first, int count )
{
	int vstride = gs.vertex.stride ? gs.vertex.stride : gs.vertex.size * 4;
	const unsigned char *vp = (const unsigned char *)gs.vertex.ptr + first * vstride;
	const unsigned char *cp = NULL, *tp[2] = { NULL, NULL };
	int cstride = 0, tstride[2] = { 0, 0 };
	int three = gs.vertex.size > 2;
	uint32_t color, st[2][2];
	int i, u;

	if ( gs.color.enabled && gs.color.ptr && gs.color.type == GL_UNSIGNED_BYTE ) {
		cstride = gs.color.stride ? gs.color.stride : 4;
		cp = (const unsigned char *)gs.color.ptr + first * cstride;
	}
	{
		union { GLubyte b[4]; uint32_t w; } c;

		c.b[0] = ColorByte( gs.color4[0] );
		c.b[1] = ColorByte( gs.color4[1] );
		c.b[2] = ColorByte( gs.color4[2] );
		c.b[3] = ColorByte( gs.color4[3] );
		color = c.w;
	}
	for ( u = 0; u < 2; u++ ) {
		if ( gs.texEnabled[u] && gs.tc[u].enabled && gs.tc[u].ptr ) {
			tstride[u] = gs.tc[u].stride ? gs.tc[u].stride : gs.tc[u].size * 4;
			tp[u] = (const unsigned char *)gs.tc[u].ptr + first * tstride[u];
		}
		st[u][0] = vkAmigaLEFloat( gs.texcoord[u][0] );
		st[u][1] = vkAmigaLEFloat( gs.texcoord[u][1] );
	}

	for ( i = 0; i < count; i++, to += 8 ) {
		const uint32_t *v = (const uint32_t *)vp;

		to[0] = SWAP32( v[0] );
		to[1] = SWAP32( v[1] );
		to[2] = three ? SWAP32( v[2] ) : 0;
		vp += vstride;
		if ( cp ) {
			to[3] = *(const uint32_t *)cp;      /* four bytes, as they are */
			cp += cstride;
		} else {
			to[3] = color;
		}
		if ( tp[0] ) {
			const uint32_t *t = (const uint32_t *)tp[0];

			to[4] = SWAP32( t[0] );
			to[5] = SWAP32( t[1] );
			tp[0] += tstride[0];
		} else {
			to[4] = st[0][0];
			to[5] = st[0][1];
		}
		if ( tp[1] ) {
			const uint32_t *t = (const uint32_t *)tp[1];

			to[6] = SWAP32( t[0] );
			to[7] = SWAP32( t[1] );
			tp[1] += tstride[1];
		} else {
			to[6] = st[1][0];
			to[7] = st[1][1];
		}
	}
}

/* Room in the frame's buffers for a draw; 0 when they are full. */
static int Reserve( int vertexes, int indexes, uint32_t **vertexTo, unsigned short **indexTo )
{
	if ( vertexes <= 0 || indexes <= 0 || vertexes > 65536
		|| vk.vertexUsed + vertexes > FRAME_VERTEXES || vk.indexUsed + indexes > FRAME_INDEXES ) {
		return 0;
	}
	*vertexTo = (uint32_t *)( vk.vertexData[vk.slot] + vk.vertexUsed * VERTEX_BYTES );
	*indexTo = vk.indexData[vk.slot] + vk.indexUsed;
	return 1;
}

static void Submit( int vertexes, int indexes )
{
	vkCmdDrawIndexed( vk.cmd[vk.slot], indexes, 1, vk.indexUsed, (int32_t)vk.vertexUsed, 0 );
	vk.vertexUsed += vertexes;
	vk.indexUsed += indexes;
	vk.statDraws++;
	vk.statVertexes += vertexes;
}

/* The indexes that draw count vertexes in a GL mode as a triangle or line
   list; *lines says which. Returns their number. */
static int ListIndexes( GLenum mode, int count, unsigned short *out, int max, int *lines )
{
	int i, n = 0;

	*lines = 0;
	switch ( mode ) {
	case GL_TRIANGLES:
		for ( i = 0; i + 2 < count && n + 3 <= max; i += 3 ) {
			out[n++] = i; out[n++] = i + 1; out[n++] = i + 2;
		}
		break;
	case GL_QUADS:
		for ( i = 0; i + 3 < count && n + 6 <= max; i += 4 ) {
			out[n++] = i; out[n++] = i + 1; out[n++] = i + 2;
			out[n++] = i; out[n++] = i + 2; out[n++] = i + 3;
		}
		break;
	case GL_POLYGON:
	case GL_TRIANGLE_FAN:
		for ( i = 1; i + 1 < count && n + 3 <= max; i++ ) {
			out[n++] = 0; out[n++] = i; out[n++] = i + 1;
		}
		break;
	case GL_TRIANGLE_STRIP:
	case GL_QUAD_STRIP:
		for ( i = 0; i + 2 < count && n + 3 <= max; i++ ) {
			if ( i & 1 ) {
				out[n++] = i + 1; out[n++] = i; out[n++] = i + 2;
			} else {
				out[n++] = i; out[n++] = i + 1; out[n++] = i + 2;
			}
		}
		break;
	case GL_LINES:
		*lines = 1;
		for ( i = 0; i + 1 < count && n + 2 <= max; i += 2 ) {
			out[n++] = i; out[n++] = i + 1;
		}
		break;
	case GL_LINE_STRIP:
	case GL_LINE_LOOP:
		*lines = 1;
		for ( i = 0; i + 1 < count && n + 2 <= max; i++ ) {
			out[n++] = i; out[n++] = i + 1;
		}
		if ( mode == GL_LINE_LOOP && count > 2 && n + 2 <= max ) {
			out[n++] = count - 1; out[n++] = 0;
		}
		break;
	default:
		break;      /* points: not drawn */
	}
	return n;
}

void vk_glDrawElements( GLenum mode, GLsizei count, GLenum type, const GLvoid *indices )
{
	uint32_t *vertexTo;
	unsigned short *indexTo;
	int first, vertexes, i;

	if ( mode != GL_TRIANGLES || count <= 0 || !gs.vertex.ptr ) {
		return;
	}
	/* The renderer locks the arrays it draws (glLockArraysEXT): that is the
	   range to copy. Otherwise the indexes tell. */
	if ( gs.lockCount > 0 ) {
		first = gs.lockFirst;
		vertexes = gs.lockCount;
	} else {
		unsigned int lo = ~0u, hi = 0;

		for ( i = 0; i < count; i++ ) {
			unsigned int v = type == GL_UNSIGNED_INT ? ( (const GLuint *)indices )[i]
				: type == GL_UNSIGNED_SHORT ? ( (const GLushort *)indices )[i] : ( (const GLubyte *)indices )[i];

			if ( v < lo ) lo = v;
			if ( v > hi ) hi = v;
		}
		first = (int)lo;
		vertexes = (int)( hi - lo ) + 1;
	}
	if ( !PrepareDraw( 0 ) || !Reserve( vertexes, count, &vertexTo, &indexTo ) ) {
		return;
	}
	CopyClientVertexes( vertexTo, first, vertexes );
	if ( type == GL_UNSIGNED_INT ) {
		const GLuint *from = (const GLuint *)indices;

		for ( i = 0; i < count; i++ ) {
			indexTo[i] = vkAmigaLE16( (uint16_t)( from[i] - first ) );
		}
	} else if ( type == GL_UNSIGNED_SHORT ) {
		const GLushort *from = (const GLushort *)indices;

		for ( i = 0; i < count; i++ ) {
			indexTo[i] = vkAmigaLE16( (uint16_t)( from[i] - first ) );
		}
	} else {
		const GLubyte *from = (const GLubyte *)indices;

		for ( i = 0; i < count; i++ ) {
			indexTo[i] = vkAmigaLE16( (uint16_t)( from[i] - first ) );
		}
	}
	Submit( vertexes, count );
}

void vk_glDrawArrays( GLenum mode, GLint first, GLsizei count )
{
	uint32_t *vertexTo;
	unsigned short *indexTo;
	int lines, n;

	if ( count <= 0 || count > IMM_MAX || !gs.vertex.ptr ) {
		return;
	}
	n = ListIndexes( mode, count, scratchIndexes, IMM_MAX * 3, &lines );
	if ( !n || !PrepareDraw( lines ) || !Reserve( count, n, &vertexTo, &indexTo ) ) {
		return;
	}
	CopyClientVertexes( vertexTo, first, count );
	vkAmigaCopyLE16( indexTo, scratchIndexes, n );
	Submit( count, n );
}

/* ---- glBegin / glEnd ---------------------------------------------------- */

void vk_glBegin( GLenum mode )
{
	immMode = mode;
	immCount = 0;
}

static void ImmVertex( float x, float y, float z )
{
	vkvert_t *v;

	if ( immCount >= IMM_MAX ) {
		return;
	}
	v = &imm[immCount++];
	v->xyz[0] = x;
	v->xyz[1] = y;
	v->xyz[2] = z;
	v->rgba[0] = ColorByte( gs.color4[0] );
	v->rgba[1] = ColorByte( gs.color4[1] );
	v->rgba[2] = ColorByte( gs.color4[2] );
	v->rgba[3] = ColorByte( gs.color4[3] );
	memcpy( v->st, gs.texcoord, sizeof( v->st ) );
}

void vk_glVertex2f( GLfloat x, GLfloat y )              { ImmVertex( x, y, 0.0f ); }
void vk_glVertex3f( GLfloat x, GLfloat y, GLfloat z )   { ImmVertex( x, y, z ); }
void vk_glVertex3fv( const GLfloat *v )                 { ImmVertex( v[0], v[1], v[2] ); }

void vk_glEnd( void )
{
	uint32_t *vertexTo;
	unsigned short *indexTo;
	int lines, n, i;

	n = ListIndexes( immMode, immCount, scratchIndexes, IMM_MAX * 3, &lines );
	if ( n && PrepareDraw( lines ) && Reserve( immCount, n, &vertexTo, &indexTo ) ) {
		const uint32_t *from = (const uint32_t *)imm;

		for ( i = 0; i < immCount; i++, from += 8, vertexTo += 8 ) {
			vertexTo[0] = SWAP32( from[0] );
			vertexTo[1] = SWAP32( from[1] );
			vertexTo[2] = SWAP32( from[2] );
			vertexTo[3] = from[3];
			vertexTo[4] = SWAP32( from[4] );
			vertexTo[5] = SWAP32( from[5] );
			vertexTo[6] = SWAP32( from[6] );
			vertexTo[7] = SWAP32( from[7] );
		}
		vkAmigaCopyLE16( indexTo, scratchIndexes, n );
		Submit( immCount, n );
	}
	immCount = 0;
}

void vk_glArrayElement( GLint i )
{
	const float *p;
	int u;

	if ( !gs.vertex.ptr ) {
		return;
	}
	if ( gs.color.enabled && gs.color.ptr && gs.color.type == GL_UNSIGNED_BYTE ) {
		const GLubyte *c = (const GLubyte *)gs.color.ptr + i * ( gs.color.stride ? gs.color.stride : 4 );

		gs.color4[0] = c[0] / 255.0f;
		gs.color4[1] = c[1] / 255.0f;
		gs.color4[2] = c[2] / 255.0f;
		gs.color4[3] = c[3] / 255.0f;
	}
	for ( u = 0; u < 2; u++ ) {
		if ( gs.tc[u].enabled && gs.tc[u].ptr ) {
			const float *t = (const float *)( (const char *)gs.tc[u].ptr + i * ( gs.tc[u].stride ? gs.tc[u].stride : 8 ) );

			gs.texcoord[u][0] = t[0];
			gs.texcoord[u][1] = t[1];
		}
	}
	p = (const float *)( (const char *)gs.vertex.ptr + i * ( gs.vertex.stride ? gs.vertex.stride : gs.vertex.size * 4 ) );
	ImmVertex( p[0], p[1], gs.vertex.size > 2 ? p[2] : 0.0f );
}

/* ---- current values ----------------------------------------------------- */

void vk_glColor3f( GLfloat r, GLfloat g, GLfloat b )
{
	gs.color4[0] = r; gs.color4[1] = g; gs.color4[2] = b; gs.color4[3] = 1.0f;
}

void vk_glColor3fv( const GLfloat *v )
{
	vk_glColor3f( v[0], v[1], v[2] );
}

void vk_glColor4f( GLfloat r, GLfloat g, GLfloat b, GLfloat a )
{
	gs.color4[0] = r; gs.color4[1] = g; gs.color4[2] = b; gs.color4[3] = a;
}

void vk_glColor4ub( GLubyte r, GLubyte g, GLubyte b, GLubyte a )
{
	vk_glColor4f( r / 255.0f, g / 255.0f, b / 255.0f, a / 255.0f );
}

void vk_glColor4ubv( const GLubyte *v )
{
	vk_glColor4ub( v[0], v[1], v[2], v[3] );
}

void vk_glTexCoord2f( GLfloat s, GLfloat t )
{
	gs.texcoord[0][0] = s;
	gs.texcoord[0][1] = t;
}

void vk_glTexCoord2fv( const GLfloat *v )
{
	vk_glTexCoord2f( v[0], v[1] );
}

void vk_glMultiTexCoord2fARB( GLenum unit, GLfloat s, GLfloat t )
{
	int u = (int)( unit - GL_TEXTURE0_ARB ) & 1;

	gs.texcoord[u][0] = s;
	gs.texcoord[u][1] = t;
}

void vk_glNormal3fv( const GLfloat *v )
{
}

/* ---- client arrays ------------------------------------------------------ */

static vkarray_t *ArrayFor( GLenum array )
{
	switch ( array ) {
	case GL_VERTEX_ARRAY:           return &gs.vertex;
	case GL_COLOR_ARRAY:            return &gs.color;
	case GL_TEXTURE_COORD_ARRAY:    return &gs.tc[gs.clientUnit];
	default:                        return NULL;
	}
}

void vk_glEnableClientState( GLenum array )
{
	vkarray_t *a = ArrayFor( array );

	if ( a ) {
		a->enabled = 1;
	}
}

void vk_glDisableClientState( GLenum array )
{
	vkarray_t *a = ArrayFor( array );

	if ( a ) {
		a->enabled = 0;
	}
}

void vk_glVertexPointer( GLint size, GLenum type, GLsizei stride, const GLvoid *ptr )
{
	gs.vertex.size = size; gs.vertex.type = type; gs.vertex.stride = stride; gs.vertex.ptr = ptr;
}

void vk_glColorPointer( GLint size, GLenum type, GLsizei stride, const GLvoid *ptr )
{
	gs.color.size = size; gs.color.type = type; gs.color.stride = stride; gs.color.ptr = ptr;
}

void vk_glTexCoordPointer( GLint size, GLenum type, GLsizei stride, const GLvoid *ptr )
{
	vkarray_t *a = &gs.tc[gs.clientUnit];

	a->size = size; a->type = type; a->stride = stride; a->ptr = ptr;
}

void vk_glNormalPointer( GLenum type, GLsizei stride, const GLvoid *ptr )
{
}

void vk_glLockArraysEXT( GLint first, GLsizei count )
{
	gs.lockFirst = first;
	gs.lockCount = count;
}

void vk_glUnlockArraysEXT( void )
{
	gs.lockCount = 0;
}

void vk_glClientActiveTextureARB( GLenum unit )
{
	gs.clientUnit = (int)( unit - GL_TEXTURE0_ARB ) & 1;
}

void vk_glActiveTextureARB( GLenum unit )
{
	gs.activeUnit = (int)( unit - GL_TEXTURE0_ARB ) & 1;
}

/* ---- state -------------------------------------------------------------- */

static void SetCap( GLenum cap, int on )
{
	switch ( cap ) {
	case GL_TEXTURE_2D:             gs.texEnabled[gs.activeUnit] = on; break;
	case GL_ALPHA_TEST:             gs.alphaTest = on; break;
	case GL_BLEND:                  gs.blend = on; break;
	case GL_DEPTH_TEST:             gs.depthTest = on; break;
	case GL_CULL_FACE:              gs.cullFace = on; break;
	case GL_POLYGON_OFFSET_FILL:    gs.polyOffset = on; break;
	case GL_SCISSOR_TEST:           gs.scissorTest = on; vk.dynDirty = 1; break;
	case GL_FOG:                    gs.fog = on; break;
	default:                        break;      /* clip planes, stencil, lighting: not here */
	}
}

void vk_glEnable( GLenum cap )      { SetCap( cap, 1 ); }
void vk_glDisable( GLenum cap )     { SetCap( cap, 0 ); }

void vk_glAlphaFunc( GLenum func, GLclampf ref )
{
	gs.afunc = func == GL_GREATER ? 1 : func == GL_LESS ? 2 : func == GL_GEQUAL ? 3 : 0;
	gs.aref = ref;
}

void vk_glTexEnvf( GLenum target, GLenum pname, GLfloat param )
{
	if ( pname == GL_TEXTURE_ENV_MODE ) {
		GLenum mode = (GLenum)param;

		gs.env[gs.activeUnit] = mode == GL_REPLACE ? 1 : mode == GL_DECAL ? 2 : mode == GL_ADD ? 3 : 0;
	}
}

/* GL fog, done in the fragment shaders (RTCW's distance fog). */
void vk_glFogf( GLenum pname, GLfloat param )
{
	switch ( pname ) {
	case GL_FOG_MODE:
		gs.fogMode = (GLenum)param == GL_LINEAR ? 1 : (GLenum)param == GL_EXP2 ? 3 : 2;
		break;
	case GL_FOG_START:      gs.fogStart = param; break;
	case GL_FOG_END:        gs.fogEnd = param; break;
	case GL_FOG_DENSITY:    gs.fogDensity = param; break;
	default:                break;      /* GL_FOG_DISTANCE_MODE_NV: eye distance anyway */
	}
}

void vk_glFogi( GLenum pname, GLint param )
{
	vk_glFogf( pname, (GLfloat)param );
}

void vk_glFogfv( GLenum pname, const GLfloat *params )
{
	if ( pname == GL_FOG_COLOR ) {
		memcpy( gs.fogColor, params, sizeof( gs.fogColor ) );
	} else {
		vk_glFogf( pname, params[0] );
	}
}

void vk_glHint( GLenum target, GLenum mode )    {}

void vk_glBlendFunc( GLenum s, GLenum d )       { gs.blendSrc = s; gs.blendDst = d; }
void vk_glCullFace( GLenum mode )               { gs.cullMode = mode; }
void vk_glDepthFunc( GLenum func )              { gs.depthFunc = func; }
void vk_glDepthMask( GLboolean flag )           { gs.depthMask = flag ? 1 : 0; }
void vk_glClearDepth( GLclampd d )              { gs.clearDepth = (float)d; }

void vk_glColorMask( GLboolean r, GLboolean g, GLboolean b, GLboolean a )
{
	gs.colorMask = ( r || g || b || a ) ? 1 : 0;
}

void vk_glClearColor( GLclampf r, GLclampf g, GLclampf b, GLclampf a )
{
	gs.clearColor[0] = r; gs.clearColor[1] = g; gs.clearColor[2] = b; gs.clearColor[3] = a;
}

void vk_glDepthRange( GLclampd n, GLclampd f )
{
	gs.depthNear = (float)n;
	gs.depthFar = (float)f;
	vk.dynDirty = 1;
}

void vk_glPolygonOffset( GLfloat factor, GLfloat units )
{
	gs.offsetFactor = factor;
	gs.offsetUnits = units;
	vk.dynDirty = 1;
}

void vk_glViewport( GLint x, GLint y, GLsizei w, GLsizei h )
{
	gs.viewport[0] = x; gs.viewport[1] = y; gs.viewport[2] = w; gs.viewport[3] = h;
	vk.dynDirty = 1;
}

void vk_glScissor( GLint x, GLint y, GLsizei w, GLsizei h )
{
	gs.scissor[0] = x; gs.scissor[1] = y; gs.scissor[2] = w; gs.scissor[3] = h;
	vk.dynDirty = 1;
}

/* Inside the frame's render pass: the scissor's rectangle of the colour
   and the depth buffer. */
void vk_glClear( GLbitfield mask )
{
	VkClearAttachment att[2];
	VkClearRect rect;
	int n = 0, x, y, w, h, H = (int)vk.extent.height, W = (int)vk.extent.width;

	BeginFrame();
	if ( !vk.inFrame ) {
		return;
	}
	memset( att, 0, sizeof( att ) );
	if ( mask & GL_COLOR_BUFFER_BIT ) {
		att[n].aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
		memcpy( att[n].clearValue.color.float32, gs.clearColor, sizeof( gs.clearColor ) );
		n++;
	}
	if ( mask & GL_DEPTH_BUFFER_BIT ) {
		att[n].aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT;
		att[n].clearValue.depthStencil.depth = gs.clearDepth;
		n++;
	}
	if ( !n ) {
		return;
	}
	if ( gs.scissorTest ) {
		x = gs.scissor[0];
		y = H - gs.scissor[1] - gs.scissor[3];
		w = gs.scissor[2];
		h = gs.scissor[3];
		if ( x < 0 ) { w += x; x = 0; }
		if ( y < 0 ) { h += y; y = 0; }
		if ( x + w > W ) w = W - x;
		if ( y + h > H ) h = H - y;
		if ( w <= 0 || h <= 0 ) {
			return;
		}
	} else {
		x = y = 0;
		w = W;
		h = H;
	}
	memset( &rect, 0, sizeof( rect ) );
	rect.rect.offset.x = x;
	rect.rect.offset.y = y;
	rect.rect.extent.width = (uint32_t)w;
	rect.rect.extent.height = (uint32_t)h;
	rect.layerCount = 1;
	vkCmdClearAttachments( vk.cmd[vk.slot], n, att, 1, &rect );
}

/* Not in Vulkan, or not needed. */
void vk_glCallList( GLuint list )                           {}
/* Text labels on camera spline points (splines.cpp, editor debugging). */
void vk_glCallLists( GLsizei n, GLenum type, const GLvoid *lists ) {}
void vk_glRasterPos3fv( const GLfloat *v )                  {}
void vk_glClearStencil( GLint s )                           {}
void vk_glClipPlane( GLenum plane, const GLdouble *eq )     {}
void vk_glDrawBuffer( GLenum mode )                         {}
void vk_glFinish( void )                                    {}
void vk_glLineWidth( GLfloat w )                            {}
void vk_glPointSize( GLfloat size )                         {}
void vk_glPolygonMode( GLenum face, GLenum mode )           {}
void vk_glShadeModel( GLenum mode )                         {}
void vk_glStencilFunc( GLenum f, GLint r, GLuint m )        {}
void vk_glStencilMask( GLuint m )                           {}
void vk_glStencilOp( GLenum f, GLenum zf, GLenum zp )       {}
GLenum vk_glGetError( void )                                { return GL_NO_ERROR; }

void vk_glGetDoublev( GLenum pname, GLdouble *params )
{
	params[0] = 0.0;
}

void vk_glGetFloatv( GLenum pname, GLfloat *params )
{
	params[0] = 0.0f;
}

void vk_glGetIntegerv( GLenum pname, GLint *params )
{
	switch ( pname ) {
	case GL_MAX_TEXTURE_UNITS_ARB:  params[0] = 2; break;
	case GL_MAX_TEXTURE_SIZE:       params[0] = 2048; break;
	case GL_CULL_FACE:              params[0] = gs.cullFace; break;
	default:                        params[0] = 0; break;
	}
}

const GLubyte *vk_glGetString( GLenum name )
{
	switch ( name ) {
	case GL_VENDOR:     return (const GLubyte *)"GL 1.x on vulkan.library";
	case GL_RENDERER:   return (const GLubyte *)vk.deviceName;
	case GL_VERSION:    return (const GLubyte *)"1.1 (Vulkan 1.1)";
	case GL_EXTENSIONS: return (const GLubyte *)"GL_ARB_multitexture GL_EXT_compiled_vertex_array GL_EXT_texture_env_add GL_EXT_texture_edge_clamp";
	default:            return (const GLubyte *)"";
	}
}

/* ---- reading back ------------------------------------------------------- */

/* The picture is read from the screen or the window: the frame shown last,
   not the one being drawn (screenshots, the saved game's picture, the
   dissolve). Depth reads say "near", so flares count as hidden. */
void vk_glReadPixels( GLint x, GLint y, GLsizei width, GLsizei height, GLenum format, GLenum type,
	GLvoid *pixels )
{
	struct RastPort *rp;
	unsigned char *tmp, *out = (unsigned char *)pixels;
	int W = (int)vk.extent.width, H = (int)vk.extent.height;
	int sw, sh, ox = 0, oy = 0, bytes = format == GL_RGB ? 3 : 4;
	int row, col;

	if ( format == GL_DEPTH_COMPONENT ) {
		memset( pixels, 0, (size_t)width * height * sizeof( float ) );
		return;
	}
	if ( format != GL_RGB && format != GL_RGBA ) {
		memset( pixels, 0, (size_t)width * height );
		return;
	}
	memset( pixels, 0, (size_t)width * height * bytes );
	if ( !vk.window || !CyberGfxBase || width <= 0 || height <= 0 ) {
		return;
	}
	rp = vk.window->RPort;
	if ( vk.ownWindow ) {
		ox = vk.window->BorderLeft;
		oy = vk.window->BorderTop;
		sw = vk.window->Width - vk.window->BorderLeft - vk.window->BorderRight;
		sh = vk.window->Height - vk.window->BorderTop - vk.window->BorderBottom;
	} else {
		sw = vk.window->Width;
		sh = vk.window->Height;
	}
	tmp = (unsigned char *)malloc( (size_t)sw * 4 );
	if ( !tmp ) {
		return;
	}
	/* a row of the screen at a time; it may be larger than the picture
	   drawn (the server's upscaling) */
	for ( row = 0; row < height; row++ ) {
		int sy = ( H - 1 - ( y + row ) ) * sh / H;

		if ( sy < 0 || sy >= sh ) {
			continue;
		}
		ReadPixelArray( tmp, 0, 0, sw * 4, rp, ox, oy + sy, sw, 1, RECTFMT_RGBA );
		for ( col = 0; col < width; col++ ) {
			int sx = ( x + col ) * sw / W;
			const unsigned char *p;

			if ( sx < 0 || sx >= sw ) {
				continue;
			}
			p = tmp + sx * 4;
			out[0] = p[0];
			out[1] = p[1];
			out[2] = p[2];
			if ( bytes == 4 ) {
				out[3] = 255;
			}
			out += bytes;
		}
		out = (unsigned char *)pixels + (size_t)( row + 1 ) * width * bytes;
	}
	free( tmp );
}

/* ---- the display side, for amiga_vkglimp.cpp ---------------------------- */

/* Before VKGL_CreateContext: 4 for 4x multisampling where the GPU has it. */
void VKGL_SetOptions( int samples )
{
	optSamples = samples;
}

/* The game's gamma ramp (GLimp_SetGamma), used by the last pass. */
void VKGL_SetGamma( const unsigned char *red, const unsigned char *green, const unsigned char *blue )
{
	int i;

	for ( i = 0; i < 256; i++ ) {
		rampData[i * 4 + 0] = red[i];
		rampData[i * 4 + 1] = green[i];
		rampData[i * 4 + 2] = blue[i];
		rampData[i * 4 + 3] = 255;
	}
	rampSet = 1;
	vk.rampDirty = 1;
}

int VKGL_Samples( void )
{
	return vk.samples == VK_SAMPLE_COUNT_4_BIT ? 4 : 1;
}

int VKGL_Open( void )
{
	return VulkanOpen() ? 1 : 0;
}

void VKGL_Close( void )
{
	VulkanClose();
	if ( CyberGfxBase ) {
		CloseLibrary( CyberGfxBase );
		CyberGfxBase = NULL;
	}
}

const char *VKGL_ErrorText( void )
{
	return vk.errorText;
}

const char *VKGL_DeviceName( void )
{
	return vk.deviceName;
}

void VKGL_Stats( unsigned int *draws, unsigned int *vertexes, unsigned int *uploads, unsigned int *pipelines )
{
	*draws = vk.statDraws;
	*vertexes = vk.statVertexes;
	*uploads = vk.statUploads;
	*pipelines = (unsigned int)vk.numPipelines;
	vk.statDraws = vk.statVertexes = vk.statUploads = 0;
}

/* A true colour screen of that size whose pixels are B G R A: vulkan.library
   then renders into it. The modes of one size differ in their pixel format,
   which only the open screen tells: they are tried in turn. Without such a
   mode, the first that opens (the frames are then copied into it). */
static struct Screen *OpenGameScreen( int width, int height )
{
	struct Screen *screen = NULL;
	ULONG id = INVALID_ID, other = INVALID_ID, depth = 32, otherDepth = 32;

	while ( !screen && ( id = NextDisplayInfo( id ) ) != (ULONG)INVALID_ID ) {
		struct DimensionInfo dims;

		if ( !GetDisplayInfoData( NULL, (UBYTE *)&dims, sizeof( dims ), DTAG_DIMS, id ) || dims.MaxDepth < 24
			|| dims.Nominal.MaxX - dims.Nominal.MinX + 1 != width || dims.Nominal.MaxY - dims.Nominal.MinY + 1 != height ) {
			continue;
		}
		/* the depth the mode names: Intuition refuses 32 for a mode that
		   says 24, though its pixels are four bytes */
		depth = dims.MaxDepth;
		screen = OpenScreenTags( NULL, SA_DisplayID, id, SA_Width, width, SA_Height, height, SA_Depth, depth,
			SA_Quiet, TRUE, SA_ShowTitle, FALSE, SA_Type, CUSTOMSCREEN, TAG_DONE );
		if ( screen && GetCyberMapAttr( screen->RastPort.BitMap, CYBRMATTR_PIXFMT ) != PIXFMT_BGRA32 ) {
			if ( other == (ULONG)INVALID_ID ) {
				other = id;
				otherDepth = depth;
			}
			CloseScreen( screen );
			screen = NULL;
		}
	}
	if ( !screen && other != (ULONG)INVALID_ID ) {
		screen = OpenScreenTags( NULL, SA_DisplayID, other, SA_Width, width, SA_Height, height, SA_Depth, otherDepth,
			SA_Quiet, TRUE, SA_ShowTitle, FALSE, SA_Type, CUSTOMSCREEN, TAG_DONE );
	}
	return screen;
}

static int MakeBuffer( VkDeviceSize size, VkBufferUsageFlags usage, VkBuffer *buffer, VkDeviceMemory *memory, void **data )
{
	VkBufferCreateInfo bi = { VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO };
	VkMemoryAllocateInfo ai = { VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO };
	VkMemoryRequirements req;

	bi.size = size;
	bi.usage = usage;
	NEED( vkCreateBuffer( vk.device, &bi, NULL, buffer ) );
	vkGetBufferMemoryRequirements( vk.device, *buffer, &req );
	ai.allocationSize = req.size;
	ai.memoryTypeIndex = MemoryType( req.memoryTypeBits,
		VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT );
	NEED( vkAllocateMemory( vk.device, &ai, NULL, memory ) );
	NEED( vkBindBufferMemory( vk.device, *buffer, *memory, 0 ) );
	NEED( vkMapMemory( vk.device, *memory, 0, VK_WHOLE_SIZE, 0, data ) );
	return 1;
}

static int MakeModule( const uint32_t *code, size_t size, VkShaderModule *module )
{
	VkShaderModuleCreateInfo mi = { VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO };

	mi.codeSize = size;
	mi.pCode = code;
	NEED( vkCreateShaderModule( vk.device, &mi, NULL, module ) );
	return 1;
}

/* The scene pass: into sceneImage, or into msImage resolved to sceneImage,
   with the depth buffer; sceneImage is left for the last pass to read. */
static int MakeRenderPass( void )
{
	int ms = vk.samples != VK_SAMPLE_COUNT_1_BIT;
	VkAttachmentDescription ad[3];
	VkAttachmentReference colorRef = { 0, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL };
	VkAttachmentReference depthRef = { 1, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL };
	VkAttachmentReference resolveRef = { 2, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL };
	VkSubpassDescription sd;
	VkSubpassDependency dep[2];
	VkRenderPassCreateInfo ri = { VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO };

	memset( ad, 0, sizeof( ad ) );
	ad[0].format = vk.colorFormat;
	ad[0].samples = vk.samples;
	ad[0].loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
	ad[0].storeOp = ms ? VK_ATTACHMENT_STORE_OP_DONT_CARE : VK_ATTACHMENT_STORE_OP_STORE;
	ad[0].stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
	ad[0].stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
	ad[0].initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
	ad[0].finalLayout = ms ? VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL : VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
	ad[1].format = vk.depthFormat;
	ad[1].samples = vk.samples;
	ad[1].loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
	ad[1].storeOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
	ad[1].stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
	ad[1].stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
	ad[1].initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
	ad[1].finalLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
	ad[2].format = vk.colorFormat;
	ad[2].samples = VK_SAMPLE_COUNT_1_BIT;
	ad[2].loadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
	ad[2].storeOp = VK_ATTACHMENT_STORE_OP_STORE;
	ad[2].stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
	ad[2].stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
	ad[2].initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
	ad[2].finalLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
	memset( &sd, 0, sizeof( sd ) );
	sd.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
	sd.colorAttachmentCount = 1;
	sd.pColorAttachments = &colorRef;
	sd.pDepthStencilAttachment = &depthRef;
	if ( ms ) {
		sd.pResolveAttachments = &resolveRef;
	}
	memset( dep, 0, sizeof( dep ) );
	/* the last pass of the frame before may still be reading sceneImage */
	dep[0].srcSubpass = VK_SUBPASS_EXTERNAL;
	dep[0].dstSubpass = 0;
	dep[0].srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT | VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT
		| VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
	dep[0].dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT | VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT;
	dep[0].dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
	/* and the last pass reads what this one wrote */
	dep[1].srcSubpass = 0;
	dep[1].dstSubpass = VK_SUBPASS_EXTERNAL;
	dep[1].srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
	dep[1].srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
	dep[1].dstStageMask = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
	dep[1].dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
	ri.attachmentCount = ms ? 3 : 2;
	ri.pAttachments = ad;
	ri.subpassCount = 1;
	ri.pSubpasses = &sd;
	ri.dependencyCount = 2;
	ri.pDependencies = dep;
	NEED( vkCreateRenderPass( vk.device, &ri, NULL, &vk.renderPass ) );

	/* the last pass: the swapchain image, all of it written */
	memset( ad, 0, sizeof( ad ) );
	ad[0].format = vk.colorFormat;
	ad[0].samples = VK_SAMPLE_COUNT_1_BIT;
	ad[0].loadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
	ad[0].storeOp = VK_ATTACHMENT_STORE_OP_STORE;
	ad[0].stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
	ad[0].stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
	ad[0].initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
	ad[0].finalLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
	memset( &sd, 0, sizeof( sd ) );
	sd.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
	sd.colorAttachmentCount = 1;
	sd.pColorAttachments = &colorRef;
	memset( dep, 0, sizeof( dep ) );
	dep[0].srcSubpass = VK_SUBPASS_EXTERNAL;
	dep[0].dstSubpass = 0;
	dep[0].srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
	dep[0].dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
	dep[0].dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
	ri.attachmentCount = 1;
	ri.pAttachments = ad;
	ri.subpassCount = 1;
	ri.pSubpasses = &sd;
	ri.dependencyCount = 1;
	ri.pDependencies = dep;
	NEED( vkCreateRenderPass( vk.device, &ri, NULL, &vk.postPass ) );
	return 1;
}

/* An image the size of the screen to draw into. */
static int MakeTarget( VkFormat format, VkSampleCountFlagBits samples, VkImageUsageFlags usage,
	VkImageAspectFlags aspect, VkImage *image, VkDeviceMemory *memory, VkImageView *view )
{
	VkImageCreateInfo ii = { VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO };
	VkMemoryAllocateInfo ai = { VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO };
	VkImageViewCreateInfo vi = { VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO };
	VkMemoryRequirements req;

	ii.imageType = VK_IMAGE_TYPE_2D;
	ii.format = format;
	ii.extent.width = vk.extent.width;
	ii.extent.height = vk.extent.height;
	ii.extent.depth = 1;
	ii.mipLevels = 1;
	ii.arrayLayers = 1;
	ii.samples = samples;
	ii.tiling = VK_IMAGE_TILING_OPTIMAL;
	ii.usage = usage;
	NEED( vkCreateImage( vk.device, &ii, NULL, image ) );
	memset( &req, 0, sizeof( req ) );
	vkGetImageMemoryRequirements( vk.device, *image, &req );
	if ( !req.size || !req.alignment ) {
		return Fail( "vkGetImageMemoryRequirements", VK_ERROR_UNKNOWN );
	}
	ai.allocationSize = req.size;
	ai.memoryTypeIndex = MemoryType( req.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT );
	NEED( vkAllocateMemory( vk.device, &ai, NULL, memory ) );
	NEED( vkBindImageMemory( vk.device, *image, *memory, 0 ) );
	vi.image = *image;
	vi.viewType = VK_IMAGE_VIEW_TYPE_2D;
	vi.format = format;
	vi.subresourceRange.aspectMask = aspect;
	vi.subresourceRange.levelCount = 1;
	vi.subresourceRange.layerCount = 1;
	NEED( vkCreateImageView( vk.device, &vi, NULL, view ) );
	return 1;
}

/* The last pass: its pipeline, sampler, and the two descriptor sets. */
static int MakePost( void )
{
	VkPipelineShaderStageCreateInfo stages[2];
	VkPipelineVertexInputStateCreateInfo vi = { VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO };
	VkPipelineInputAssemblyStateCreateInfo ia = { VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO };
	VkPipelineViewportStateCreateInfo vp = { VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO };
	VkPipelineRasterizationStateCreateInfo rs = { VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO };
	VkPipelineMultisampleStateCreateInfo ms = { VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO };
	VkPipelineDepthStencilStateCreateInfo dss = { VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO };
	VkPipelineColorBlendAttachmentState att;
	VkPipelineColorBlendStateCreateInfo cb = { VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO };
	VkDynamicState dyn[2] = { VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR };
	VkPipelineDynamicStateCreateInfo ds = { VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO };
	VkGraphicsPipelineCreateInfo pi = { VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO };
	VkSamplerCreateInfo si = { VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO };
	VkDescriptorSetAllocateInfo dai = { VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO };
	VkDescriptorSetLayout layouts[2];
	VkDescriptorSet sets[2];
	VkDescriptorImageInfo info[2];
	VkWriteDescriptorSet w[2];
	int i;

	if ( !MakeModule( vk_post_vert, sizeof( vk_post_vert ), &vk.postVert )
		|| !MakeModule( vk_post_frag, sizeof( vk_post_frag ), &vk.postFrag ) ) {
		return 0;
	}

	/* the ramp: 256 x 1, one entry per channel value; straight until the
	   game sets one */
	if ( !rampSet ) {
		for ( i = 0; i < 256; i++ ) {
			rampData[i * 4 + 0] = rampData[i * 4 + 1] = rampData[i * 4 + 2] = (unsigned char)i;
			rampData[i * 4 + 3] = 255;
		}
		rampSet = 1;
	}
	vk.ramp.minFilter = vk.ramp.magFilter = GL_NEAREST;
	vk.ramp.wrapS = vk.ramp.wrapT = GL_CLAMP;
	if ( !MakeTexture( &vk.ramp, 256, 1 ) ) {
		return 0;
	}
	UploadPixels( &vk.ramp, 0, 0, 0, 256, 1, GL_RGBA, rampData );
	FlushUploads();

	si.magFilter = si.minFilter = VK_FILTER_NEAREST;
	si.mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST;
	si.addressModeU = si.addressModeV = si.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
	si.maxLod = 0.25f;
	si.maxAnisotropy = 1.0f;
	NEED( vkCreateSampler( vk.device, &si, NULL, &vk.postSampler ) );

	layouts[0] = layouts[1] = vk.setLayout;
	dai.descriptorPool = vk.pool;
	dai.descriptorSetCount = 2;
	dai.pSetLayouts = layouts;
	NEED( vkAllocateDescriptorSets( vk.device, &dai, sets ) );
	vk.sceneSet = sets[0];
	vk.rampSet = sets[1];
	memset( w, 0, sizeof( w ) );
	for ( i = 0; i < 2; i++ ) {
		info[i].sampler = vk.postSampler;
		info[i].imageView = i ? vk.ramp.view : vk.sceneView;
		info[i].imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
		w[i].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
		w[i].dstSet = sets[i];
		w[i].descriptorCount = 1;
		w[i].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
		w[i].pImageInfo = &info[i];
	}
	vkUpdateDescriptorSets( vk.device, 2, w, 0, NULL );

	memset( stages, 0, sizeof( stages ) );
	stages[0].sType = stages[1].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
	stages[0].stage = VK_SHADER_STAGE_VERTEX_BIT;
	stages[0].module = vk.postVert;
	stages[0].pName = "main";
	stages[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
	stages[1].module = vk.postFrag;
	stages[1].pName = "main";
	ia.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
	vp.viewportCount = 1;
	vp.scissorCount = 1;
	rs.polygonMode = VK_POLYGON_MODE_FILL;
	rs.cullMode = VK_CULL_MODE_NONE;
	rs.lineWidth = 1.0f;
	ms.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;
	dss.maxDepthBounds = 1.0f;
	memset( &att, 0, sizeof( att ) );
	att.colorWriteMask = 0xF;
	cb.attachmentCount = 1;
	cb.pAttachments = &att;
	ds.dynamicStateCount = 2;
	ds.pDynamicStates = dyn;
	pi.stageCount = 2;
	pi.pStages = stages;
	pi.pVertexInputState = &vi;
	pi.pInputAssemblyState = &ia;
	pi.pViewportState = &vp;
	pi.pRasterizationState = &rs;
	pi.pMultisampleState = &ms;
	pi.pDepthStencilState = &dss;
	pi.pColorBlendState = &cb;
	pi.pDynamicState = &ds;
	pi.layout = vk.layout;
	pi.renderPass = vk.postPass;
	NEED( vkCreateGraphicsPipelines( vk.device, VK_NULL_HANDLE, 1, &pi, NULL, &vk.postPipeline ) );
	return 1;
}


static int MakeDevice( int vsync )
{
	uint32_t n = 1, i;

	{
		const char *exts[2] = { VK_KHR_SURFACE_EXTENSION_NAME, VK_AMIGA_WINDOW_SURFACE_EXTENSION_NAME };
		VkApplicationInfo app = { VK_STRUCTURE_TYPE_APPLICATION_INFO };
		VkInstanceCreateInfo info = { VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO };
		VkAmigaWindowSurfaceCreateInfoAMIGA si = { VK_STRUCTURE_TYPE_AMIGA_WINDOW_SURFACE_CREATE_INFO_AMIGA };
		VkPhysicalDeviceProperties props;

		app.pApplicationName = "jk2sp_vk";
		app.apiVersion = VK_API_VERSION_1_1;
		info.pApplicationInfo = &app;
		info.enabledExtensionCount = 2;
		info.ppEnabledExtensionNames = exts;
		NEED( vkCreateInstance( &info, NULL, &vk.instance ) );
		si.window = vk.window;
		NEED( vkCreateAmigaWindowSurfaceAMIGA( vk.instance, &si, NULL, &vk.surface ) );
		vkEnumeratePhysicalDevices( vk.instance, &n, &vk.gpu );
		if ( !vk.gpu ) {
			return Fail( "vkEnumeratePhysicalDevices", VK_ERROR_INITIALIZATION_FAILED );
		}
		vkGetPhysicalDeviceMemoryProperties( vk.gpu, &vk.memoryProperties );
		vkGetPhysicalDeviceProperties( vk.gpu, &props );
		strncpy( vk.deviceName, props.deviceName, sizeof( vk.deviceName ) - 1 );
		vk.samples = VK_SAMPLE_COUNT_1_BIT;
		if ( optSamples >= 4 && ( props.limits.framebufferColorSampleCounts & VK_SAMPLE_COUNT_4_BIT )
			&& ( props.limits.framebufferDepthSampleCounts & VK_SAMPLE_COUNT_4_BIT ) ) {
			vk.samples = VK_SAMPLE_COUNT_4_BIT;
		}
	}
	{
		float priority = 1.0f;
		const char *ext = VK_KHR_SWAPCHAIN_EXTENSION_NAME;
		VkDeviceQueueCreateInfo qi = { VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO };
		VkDeviceCreateInfo di = { VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO };

		qi.queueFamilyIndex = 0;
		qi.queueCount = 1;
		qi.pQueuePriorities = &priority;
		di.queueCreateInfoCount = 1;
		di.pQueueCreateInfos = &qi;
		di.enabledExtensionCount = 1;
		di.ppEnabledExtensionNames = &ext;
		NEED( vkCreateDevice( vk.gpu, &di, NULL, &vk.device ) );
		vkGetDeviceQueue( vk.device, 0, 0, &vk.queue );
	}
	{
		VkSurfaceCapabilitiesKHR caps;
		VkSwapchainCreateInfoKHR si = { VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR };
		VkPresentModeKHR modes[8];
		uint32_t modeCount = 8;

		NEED( vkGetPhysicalDeviceSurfaceCapabilitiesKHR( vk.gpu, vk.surface, &caps ) );
		vk.extent = caps.currentExtent;
		vk.colorFormat = VK_FORMAT_B8G8R8A8_UNORM;
		si.surface = vk.surface;
		si.minImageCount = 2;
		si.imageFormat = vk.colorFormat;
		si.imageColorSpace = VK_COLOR_SPACE_SRGB_NONLINEAR_KHR;
		si.imageExtent = vk.extent;
		si.imageArrayLayers = 1;
		si.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
		si.preTransform = VK_SURFACE_TRANSFORM_IDENTITY_BIT_KHR;
		si.compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
		si.presentMode = VK_PRESENT_MODE_FIFO_KHR;
		if ( !vsync && vkGetPhysicalDeviceSurfacePresentModesKHR( vk.gpu, vk.surface, &modeCount, modes ) >= 0 ) {
			for ( i = 0; i < modeCount; i++ ) {
				if ( modes[i] == VK_PRESENT_MODE_IMMEDIATE_KHR ) {
					si.presentMode = VK_PRESENT_MODE_IMMEDIATE_KHR;
				}
			}
		}
		si.clipped = VK_TRUE;
		NEED( vkCreateSwapchainKHR( vk.device, &si, NULL, &vk.swapchain ) );
		vkGetSwapchainImagesKHR( vk.device, vk.swapchain, &vk.imageCount, NULL );
		if ( vk.imageCount > MAX_IMAGES ) {
			vk.imageCount = MAX_IMAGES;
		}
		vkGetSwapchainImagesKHR( vk.device, vk.swapchain, &vk.imageCount, vk.images );
	}
	{
		/* the depth buffer: 32-bit float where the GPU has it */
		static const VkFormat candidates[3] = { VK_FORMAT_D32_SFLOAT, VK_FORMAT_X8_D24_UNORM_PACK32, VK_FORMAT_D16_UNORM };
		VkImageCreateInfo ii = { VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO };
		VkMemoryAllocateInfo ai = { VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO };
		VkImageViewCreateInfo vi = { VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO };
		VkMemoryRequirements req;

		vk.depthFormat = VK_FORMAT_D16_UNORM;
		for ( i = 0; i < 3; i++ ) {
			VkFormatProperties fp;

			vkGetPhysicalDeviceFormatProperties( vk.gpu, candidates[i], &fp );
			if ( fp.optimalTilingFeatures & VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT ) {
				vk.depthFormat = candidates[i];
				break;
			}
		}
		ii.imageType = VK_IMAGE_TYPE_2D;
		ii.format = vk.depthFormat;
		ii.extent.width = vk.extent.width;
		ii.extent.height = vk.extent.height;
		ii.extent.depth = 1;
		ii.mipLevels = 1;
		ii.arrayLayers = 1;
		ii.samples = vk.samples;
		ii.tiling = VK_IMAGE_TILING_OPTIMAL;
		ii.usage = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT;
		NEED( vkCreateImage( vk.device, &ii, NULL, &vk.depthImage ) );
		vkGetImageMemoryRequirements( vk.device, vk.depthImage, &req );
		ai.allocationSize = req.size;
		ai.memoryTypeIndex = MemoryType( req.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT );
		NEED( vkAllocateMemory( vk.device, &ai, NULL, &vk.depthMemory ) );
		NEED( vkBindImageMemory( vk.device, vk.depthImage, vk.depthMemory, 0 ) );
		vi.image = vk.depthImage;
		vi.viewType = VK_IMAGE_VIEW_TYPE_2D;
		vi.format = vk.depthFormat;
		vi.subresourceRange.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT;
		vi.subresourceRange.levelCount = 1;
		vi.subresourceRange.layerCount = 1;
		NEED( vkCreateImageView( vk.device, &vi, NULL, &vk.depthView ) );
	}
	if ( !MakeTarget( vk.colorFormat, VK_SAMPLE_COUNT_1_BIT,
			VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT, VK_IMAGE_ASPECT_COLOR_BIT,
			&vk.sceneImage, &vk.sceneMemory, &vk.sceneView ) ) {
		return 0;
	}
	if ( vk.samples != VK_SAMPLE_COUNT_1_BIT && !MakeTarget( vk.colorFormat, vk.samples,
			VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSIENT_ATTACHMENT_BIT, VK_IMAGE_ASPECT_COLOR_BIT,
			&vk.msImage, &vk.msMemory, &vk.msView ) ) {
		return 0;
	}
	if ( !MakeRenderPass() ) {
		return 0;
	}
	{
		VkFramebufferCreateInfo fbi = { VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO };
		VkImageView all[3];

		if ( vk.samples != VK_SAMPLE_COUNT_1_BIT ) {
			all[0] = vk.msView;
			all[1] = vk.depthView;
			all[2] = vk.sceneView;
			fbi.attachmentCount = 3;
		} else {
			all[0] = vk.sceneView;
			all[1] = vk.depthView;
			fbi.attachmentCount = 2;
		}
		fbi.renderPass = vk.renderPass;
		fbi.pAttachments = all;
		fbi.width = vk.extent.width;
		fbi.height = vk.extent.height;
		fbi.layers = 1;
		NEED( vkCreateFramebuffer( vk.device, &fbi, NULL, &vk.sceneFramebuffer ) );
	}
	for ( i = 0; i < vk.imageCount; i++ ) {
		VkImageViewCreateInfo vi = { VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO };
		VkFramebufferCreateInfo fbi = { VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO };
		VkImageView both[2];

		vi.image = vk.images[i];
		vi.viewType = VK_IMAGE_VIEW_TYPE_2D;
		vi.format = vk.colorFormat;
		vi.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
		vi.subresourceRange.levelCount = 1;
		vi.subresourceRange.layerCount = 1;
		NEED( vkCreateImageView( vk.device, &vi, NULL, &vk.views[i] ) );
		both[0] = vk.views[i];
		fbi.renderPass = vk.postPass;
		fbi.attachmentCount = 1;
		fbi.pAttachments = both;
		fbi.width = vk.extent.width;
		fbi.height = vk.extent.height;
		fbi.layers = 1;
		NEED( vkCreateFramebuffer( vk.device, &fbi, NULL, &vk.postFramebuffers[i] ) );
	}
	{
		/* a texture is a descriptor set of one image, bound as set 0 or 1 */
		VkDescriptorSetLayoutBinding b = { 0, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_FRAGMENT_BIT, NULL };
		VkDescriptorSetLayoutCreateInfo li = { VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO };
		VkPipelineLayoutCreateInfo pli = { VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO };
		VkPushConstantRange push = { VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof( vkpush_t ) };
		VkDescriptorPoolSize size = { VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, TEX_SLOTS + 3 };
		VkDescriptorPoolCreateInfo dpi = { VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO };
		VkDescriptorSetLayout both[2];

		li.bindingCount = 1;
		li.pBindings = &b;
		NEED( vkCreateDescriptorSetLayout( vk.device, &li, NULL, &vk.setLayout ) );
		both[0] = both[1] = vk.setLayout;
		pli.setLayoutCount = 2;
		pli.pSetLayouts = both;
		pli.pushConstantRangeCount = 1;
		pli.pPushConstantRanges = &push;
		NEED( vkCreatePipelineLayout( vk.device, &pli, NULL, &vk.layout ) );
		dpi.maxSets = TEX_SLOTS + 3;
		dpi.poolSizeCount = 1;
		dpi.pPoolSizes = &size;
		NEED( vkCreateDescriptorPool( vk.device, &dpi, NULL, &vk.pool ) );
	}
	if ( !MakeModule( vk_gl_vert, sizeof( vk_gl_vert ), &vk.vert )
		|| !MakeModule( vk_gl0_frag, sizeof( vk_gl0_frag ), &vk.frag[0] )
		|| !MakeModule( vk_gl1_frag, sizeof( vk_gl1_frag ), &vk.frag[1] )
		|| !MakeModule( vk_gl2_frag, sizeof( vk_gl2_frag ), &vk.frag[2] ) ) {
		return 0;
	}
	{
		VkCommandPoolCreateInfo pi = { VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO };
		VkCommandBufferAllocateInfo ci = { VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO };
		VkSemaphoreCreateInfo si = { VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO };
		VkFenceCreateInfo fi = { VK_STRUCTURE_TYPE_FENCE_CREATE_INFO };
		VkCommandBuffer all[FRAMES + 1];

		pi.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
		pi.queueFamilyIndex = 0;
		NEED( vkCreateCommandPool( vk.device, &pi, NULL, &vk.commandPool ) );
		ci.commandPool = vk.commandPool;
		ci.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
		ci.commandBufferCount = FRAMES + 1;
		NEED( vkAllocateCommandBuffers( vk.device, &ci, all ) );
		fi.flags = VK_FENCE_CREATE_SIGNALED_BIT;
		for ( i = 0; i < FRAMES; i++ ) {
			void *data;

			vk.cmd[i] = all[i];
			NEED( vkCreateFence( vk.device, &fi, NULL, &vk.fence[i] ) );
			NEED( vkCreateSemaphore( vk.device, &si, NULL, &vk.acquired[i] ) );
			NEED( vkCreateSemaphore( vk.device, &si, NULL, &vk.rendered[i] ) );
			if ( !MakeBuffer( (VkDeviceSize)FRAME_VERTEXES * VERTEX_BYTES, VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
				&vk.vertexBuffer[i], &vk.vertexMemory[i], &data ) ) {
				return 0;
			}
			vk.vertexData[i] = (unsigned char *)data;
			if ( !MakeBuffer( (VkDeviceSize)FRAME_INDEXES * 2, VK_BUFFER_USAGE_INDEX_BUFFER_BIT,
				&vk.indexBuffer[i], &vk.indexMemory[i], &data ) ) {
				return 0;
			}
			vk.indexData[i] = (unsigned short *)data;
		}
		vk.upCmd = all[FRAMES];
		NEED( vkCreateFence( vk.device, &fi, NULL, &vk.upFence ) );
	}
	if ( !MakeStaging( STAGING_BYTES ) ) {
		return 0;
	}
	{
		/* what a unit shows when its texture is not there */
		static const unsigned char white[4] = { 255, 255, 255, 255 };

		vk.white.minFilter = vk.white.magFilter = GL_NEAREST;
		vk.white.wrapS = vk.white.wrapT = GL_REPEAT;
		if ( !MakeTexture( &vk.white, 1, 1 ) ) {
			return 0;
		}
		UploadPixels( &vk.white, 0, 0, 0, 1, 1, GL_RGBA, white );
		FlushUploads();
	}
	if ( !MakePost() ) {
		return 0;
	}
	return 1;
}

int VKGL_CreateContext( int width, int height, int fullscreen, int vsync,
	struct Window **inputWindow, int *drawWidth, int *drawHeight )
{
	int i;

	memset( &vk, 0, sizeof( vk ) );
	memset( &gs, 0, sizeof( gs ) );
	memset( textures, 0, sizeof( textures ) );
	for ( i = 0; i < 3; i++ ) {
		Identity( gs.stack[i][0] );
	}
	gs.serial = 1;
	gs.color4[0] = gs.color4[1] = gs.color4[2] = gs.color4[3] = 1.0f;
	gs.blendSrc = GL_ONE;
	gs.blendDst = GL_ZERO;
	gs.depthFunc = GL_LESS;
	gs.cullMode = GL_BACK;
	gs.depthMask = 1;
	gs.colorMask = 1;
	gs.fogMode = 2;             /* GL's defaults: GL_EXP, density 1, 0..1 */
	gs.fogDensity = 1.0f;
	gs.fogEnd = 1.0f;
	gs.clearDepth = 1.0f;
	gs.depthFar = 1.0f;

	if ( !CyberGfxBase ) {
		CyberGfxBase = OpenLibrary( (CONST_STRPTR)"cybergraphics.library", 41 );
	}
	if ( !CyberGfxBase ) {
		strcpy( vk.errorText, "no cybergraphics.library" );
		return 0;
	}

	if ( fullscreen ) {
		vk.screen = OpenGameScreen( width, height );
		if ( !vk.screen ) {
			sprintf( vk.errorText, "no 32-bit %dx%d screen mode", width, height );
			return 0;
		}
		vk.window = OpenWindowTags( NULL, WA_CustomScreen, (ULONG)vk.screen, WA_Left, 0, WA_Top, 0,
			WA_Width, vk.screen->Width, WA_Height, vk.screen->Height, WA_Borderless, TRUE, WA_Backdrop, TRUE,
			WA_Activate, TRUE, WA_ReportMouse, TRUE, WA_RMBTrap, TRUE, TAG_DONE );
	} else {
		vk.window = OpenWindowTags( NULL, WA_Title, (ULONG)"Jedi Outcast (Vulkan)", WA_InnerWidth, width,
			WA_InnerHeight, height, WA_DragBar, TRUE, WA_DepthGadget, TRUE, WA_Activate, TRUE,
			WA_ReportMouse, TRUE, WA_RMBTrap, TRUE, TAG_DONE );
		vk.ownWindow = 1;
	}
	if ( !vk.window ) {
		sprintf( vk.errorText, "cannot open a %dx%d window", width, height );
		VKGL_DestroyContext();
		return 0;
	}

	if ( !MakeDevice( vsync ) ) {
		char text[sizeof( vk.errorText )];

		strcpy( text, vk.errorText );
		VKGL_DestroyContext();
		strcpy( vk.errorText, text );
		return 0;
	}

	gs.viewport[2] = gs.scissor[2] = (int)vk.extent.width;
	gs.viewport[3] = gs.scissor[3] = (int)vk.extent.height;
	*inputWindow = vk.window;
	*drawWidth = (int)vk.extent.width;
	*drawHeight = (int)vk.extent.height;
	return 1;
}

void VKGL_DestroyContext( void )
{
	int i;

	if ( vk.device ) {
		if ( vk.inFrame ) {
			VKGL_SwapBuffers();
		}
		FlushUploads();
		vkDeviceWaitIdle( vk.device );

		for ( i = 0; i < TEX_SLOTS; i++ ) {
			vktex_t *t = &textures[i];

			if ( t->image ) {
				vkDestroyImageView( vk.device, t->view, NULL );
				vkDestroyImage( vk.device, t->image, NULL );
				vkFreeMemory( vk.device, t->memory, NULL );
			}
		}
		if ( vk.white.image ) {
			vkDestroyImageView( vk.device, vk.white.view, NULL );
			vkDestroyImage( vk.device, vk.white.image, NULL );
			vkFreeMemory( vk.device, vk.white.memory, NULL );
		}
		for ( i = 0; i < vk.numPipelines; i++ ) {
			if ( vk.pipelines[i].pipeline ) {
				vkDestroyPipeline( vk.device, vk.pipelines[i].pipeline, NULL );
			}
		}
		for ( i = 0; i < vk.numSamplers; i++ ) {
			vkDestroySampler( vk.device, vk.samplers[i].sampler, NULL );
		}
		if ( vk.staging ) {
			vkDestroyBuffer( vk.device, vk.staging, NULL );
			vkFreeMemory( vk.device, vk.stagingMemory, NULL );
		}
		for ( i = 0; i < FRAMES; i++ ) {
			if ( vk.vertexBuffer[i] ) {
				vkDestroyBuffer( vk.device, vk.vertexBuffer[i], NULL );
				vkFreeMemory( vk.device, vk.vertexMemory[i], NULL );
			}
			if ( vk.indexBuffer[i] ) {
				vkDestroyBuffer( vk.device, vk.indexBuffer[i], NULL );
				vkFreeMemory( vk.device, vk.indexMemory[i], NULL );
			}
			if ( vk.fence[i] ) vkDestroyFence( vk.device, vk.fence[i], NULL );
			if ( vk.acquired[i] ) vkDestroySemaphore( vk.device, vk.acquired[i], NULL );
			if ( vk.rendered[i] ) vkDestroySemaphore( vk.device, vk.rendered[i], NULL );
		}
		if ( vk.upFence ) vkDestroyFence( vk.device, vk.upFence, NULL );
		if ( vk.commandPool ) vkDestroyCommandPool( vk.device, vk.commandPool, NULL );
		for ( i = 0; i < 3; i++ ) {
			if ( vk.frag[i] ) vkDestroyShaderModule( vk.device, vk.frag[i], NULL );
		}
		if ( vk.vert ) vkDestroyShaderModule( vk.device, vk.vert, NULL );
		if ( vk.pool ) vkDestroyDescriptorPool( vk.device, vk.pool, NULL );
		if ( vk.layout ) vkDestroyPipelineLayout( vk.device, vk.layout, NULL );
		if ( vk.setLayout ) vkDestroyDescriptorSetLayout( vk.device, vk.setLayout, NULL );
		for ( i = 0; i < (int)vk.imageCount; i++ ) {
			if ( vk.postFramebuffers[i] ) vkDestroyFramebuffer( vk.device, vk.postFramebuffers[i], NULL );
			if ( vk.views[i] ) vkDestroyImageView( vk.device, vk.views[i], NULL );
		}
		if ( vk.postPipeline ) vkDestroyPipeline( vk.device, vk.postPipeline, NULL );
		if ( vk.postSampler ) vkDestroySampler( vk.device, vk.postSampler, NULL );
		if ( vk.postVert ) vkDestroyShaderModule( vk.device, vk.postVert, NULL );
		if ( vk.postFrag ) vkDestroyShaderModule( vk.device, vk.postFrag, NULL );
		if ( vk.ramp.image ) {
			vkDestroyImageView( vk.device, vk.ramp.view, NULL );
			vkDestroyImage( vk.device, vk.ramp.image, NULL );
			vkFreeMemory( vk.device, vk.ramp.memory, NULL );
		}
		if ( vk.sceneFramebuffer ) vkDestroyFramebuffer( vk.device, vk.sceneFramebuffer, NULL );
		if ( vk.sceneView ) vkDestroyImageView( vk.device, vk.sceneView, NULL );
		if ( vk.sceneImage ) {
			vkDestroyImage( vk.device, vk.sceneImage, NULL );
			vkFreeMemory( vk.device, vk.sceneMemory, NULL );
		}
		if ( vk.msView ) vkDestroyImageView( vk.device, vk.msView, NULL );
		if ( vk.msImage ) {
			vkDestroyImage( vk.device, vk.msImage, NULL );
			vkFreeMemory( vk.device, vk.msMemory, NULL );
		}
		if ( vk.postPass ) vkDestroyRenderPass( vk.device, vk.postPass, NULL );
		if ( vk.renderPass ) vkDestroyRenderPass( vk.device, vk.renderPass, NULL );
		if ( vk.depthView ) vkDestroyImageView( vk.device, vk.depthView, NULL );
		if ( vk.depthImage ) {
			vkDestroyImage( vk.device, vk.depthImage, NULL );
			vkFreeMemory( vk.device, vk.depthMemory, NULL );
		}
		if ( vk.swapchain ) vkDestroySwapchainKHR( vk.device, vk.swapchain, NULL );
		vkDestroyDevice( vk.device, NULL );
	}
	if ( vk.surface ) vkDestroySurfaceKHR( vk.instance, vk.surface, NULL );
	if ( vk.instance ) vkDestroyInstance( vk.instance, NULL );
	if ( vk.window ) CloseWindow( vk.window );
	if ( vk.screen ) CloseScreen( vk.screen );
	memset( &vk, 0, sizeof( vk ) );
}
