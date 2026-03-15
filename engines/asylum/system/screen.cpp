/* ScummVM - Graphic Adventure Engine
 * (Copyright headers...)
 */

#include "common/scummsys.h"
#include "graphics/paletteman.h"

#include "asylum/system/screen.h"
#include "asylum/resources/actor.h"
#include "asylum/resources/script.h"
#include "asylum/resources/worldstats.h"
#include "asylum/system/graphics.h"
#include "asylum/views/scene.h"
#include "asylum/asylum.h"
#include "asylum/respack.h"

namespace Asylum {

extern Graphics::Surface *getHDSurface(uint32 resourceId, uint32 frameIndex);

int g_debugDrawRects;

#define TRANSPARENCY_TABLE_SIZE (256 * 256)

Screen::Screen(AsylumEngine *vm) : _vm(vm) ,
	_useColorKey(false), _transTableCount(0), _transTable(nullptr), _transTableBuffer(nullptr) {
	
	// The 1x Logical Buffer (Game Brain & Physics)
	_backBuffer.create(LOGICAL_WIDTH, LOGICAL_HEIGHT, Graphics::PixelFormat::createFormatCLUT8());

	// The HD Projector Buffer (What the Player sees)
	_hdBackBuffer.create(ASYLUM_SCREEN_WIDTH, ASYLUM_SCREEN_HEIGHT, Graphics::PixelFormat::createFormatCLUT8());

	_flag = -1;
	_clipRect = Common::Rect(0, 0, LOGICAL_WIDTH, LOGICAL_HEIGHT);

	memset(&_currentPalette, 0, sizeof(_currentPalette));
	memset(&_mainPalette, 0, sizeof(_mainPalette));
	memset(&_fromPalette, 0, sizeof(_fromPalette));
	memset(&_toPalette,   0, sizeof(_toPalette));

	_isFading = false;
	_fadeStop = false;
	g_debugDrawRects = 0;
}

Screen::~Screen() {
	_backBuffer.free();
	_hdBackBuffer.free();
	clearTransTables();
}

void Screen::draw(ResourceId resourceId) {
	draw(resourceId, 0, Common::Point(0, 0), kDrawFlagNone, kResourceNone, Common::Point(0, 0), false);
}

void Screen::draw(ResourceId resourceId, uint32 frameIndex, const Common::Point &source, DrawFlags flags, bool colorKey) {
	draw(resourceId, frameIndex, source, flags, kResourceNone, Common::Point(0, 0), colorKey);
}

void Screen::draw(ResourceId resourceId, uint32 frameIndex, const int16 (*srcPtr)[2], DrawFlags flags, bool colorKey) {
	draw(resourceId, frameIndex, Common::Point((*srcPtr)[0], (*srcPtr)[1]), flags, kResourceNone, Common::Point(0, 0), colorKey);
}

void Screen::drawTransparent(ResourceId resourceId, uint32 frameIndex, const Common::Point &source, DrawFlags flags, uint32 transTableNum) {
	byte *index = _transTable;
	selectTransTable(transTableNum);
	draw(resourceId, frameIndex, source, (DrawFlags)(flags | 0x90000000));
	_transTable = index;
}

void Screen::draw(ResourceId resourceId, uint32 frameIndex, const Common::Point &source, DrawFlags flags, ResourceId resourceIdDestination, const Common::Point &destination, bool colorKey) {
	GraphicResource *resource = new GraphicResource(_vm, resourceId);
	draw(resource, frameIndex, source, flags, resourceIdDestination, destination, colorKey);
	delete resource;
}

void Screen::draw(GraphicResource *resource, uint32 frameIndex, const Common::Point &source, DrawFlags flags, bool colorKey) {
	draw(resource, frameIndex, source, flags, kResourceNone, Common::Point(0, 0), colorKey);
}

void Screen::drawTransparent(GraphicResource *resource, uint32 frameIndex, const Common::Point &source, DrawFlags flags, uint32 transTableNum) {
	byte *index = _transTable;
	selectTransTable(transTableNum);
	draw(resource, frameIndex, source, (DrawFlags)(flags | 0x90000000));
	_transTable = index;
}

void Screen::draw(GraphicResource *resource, uint32 frameIndex, const Common::Point &source, DrawFlags flags, ResourceId resourceIdDestination, const Common::Point &destination, bool colorKey) {
	GraphicFrame *frame = resource->getFrame(frameIndex);
	ResourceEntry *resourceMask = nullptr;

	Common::Rect src;
	Common::Rect dest;
	Common::Rect srcMask;
	Common::Rect destMask;

	dest.left = source.x + frame->x;
	if (flags & kDrawFlagMirrorLeftRight) {
		if (_flag == -1) {
			if ((resource->getData().flags & 15) >= 2) {
				dest.left = source.x + (int16)resource->getData().maxWidth - ((int16)frame->getWidth() + frame->x);
			}
		} else {
			dest.left += (int16)(2 * (_flag - (frame->getHeight() * 2 - frame->x)));
		}
	}

	dest.top = source.y + frame->y;
	dest.right  = dest.left + (int16)frame->getWidth();
	dest.bottom = dest.top  + (int16)frame->getHeight();

	src.left = 0;
	src.top = 0;
	src.right = frame->getWidth();
	src.bottom = frame->getHeight();

	// 1. Calculate 1x Clipping boundaries
	clip(&src, &dest, flags);

	bool masked = false;
	if (resourceIdDestination) {
		masked = true;
		resourceMask = getResource()->get(resourceIdDestination);
		srcMask = Common::Rect(0, 0, (int16)resourceMask->getData(4), (int16)resourceMask->getData(0));
		destMask = Common::Rect(destination.x, destination.y, destination.x + (int16)resourceMask->getData(4), destination.y + (int16)resourceMask->getData(0));
		clip(&srcMask, &destMask, 0);

		if (!dest.intersects(destMask))
			masked = false;
	}

	if (!src.isValidRect()) return;

	_useColorKey = colorKey;

	// 2. Perform the 1x Draw to the internal Game Brain Buffer
	if (masked) {
		blitMasked(frame, &src, resourceMask->data + 8, &srcMask, &destMask, (uint16)resourceMask->getData(4), &dest, flags);
	} else {
		blit(frame, &src, &dest, flags);
	}

	// 3. Perform the HD Draw to the Projector Buffer!
	int S = ASYLUM_SCALE_FACTOR;
	if (S > 1) {
		Graphics::Surface *hdSurf = getHDSurface(resource->getResourceId(), frameIndex);
		if (hdSurf) {
			// Temporarily swap the engine's backbuffer to the HD one so we can reuse the native drawing code!
			Graphics::Surface tmp = _backBuffer;
			_backBuffer = _hdBackBuffer;
			
			GraphicFrame hdFrame;
			hdFrame.surface = *hdSurf;
			hdFrame.x = frame->x * S;
			hdFrame.y = frame->y * S;

			Common::Rect s_src(src.left * S, src.top * S, src.right * S, src.bottom * S);
			Common::Rect s_dest(dest.left * S, dest.top * S, dest.right * S, dest.bottom * S);

			if (masked) {
				// We handle the HD Mask loop completely manually to guarantee zero memory over-reads.
				// This fixes the horizontal rays!
				byte *frameBuffer = (byte *)hdFrame.surface.getPixels();
				byte *mirroredBuffer = nullptr;
				int hdFrameRight = hdFrame.surface.pitch;
				
				if (flags & kDrawFlagMirrorLeftRight) {
					mirroredBuffer = (byte *)malloc(s_src.right * s_src.bottom);
					blitMirrored(mirroredBuffer, frameBuffer + s_src.right - 1, s_src.bottom, s_src.right, s_src.right + hdFrameRight, 0);
					frameBuffer = mirroredBuffer;
					hdFrameRight = s_src.right;
					s_src.right -= s_src.left;
					s_src.left = 0;
				}

				byte *frameBufferPtr = frameBuffer + (s_src.top * hdFrameRight) + s_src.left;
				byte *maskBufferPtr = resourceMask->data + 8 + (srcMask.top * ((uint16)resourceMask->getData(4) / 8)) + (srcMask.left / 8);

				int skipX = dest.left > destMask.left ? dest.left - destMask.left : 0;
				int skipY = dest.top > destMask.top ? dest.top - destMask.top : 0;
				
				byte nSkippedBits = (srcMask.left + skipX) % 8;
				byte *maskBase = maskBufferPtr + (skipY * ((uint16)resourceMask->getData(4) / 8)) + (skipX / 8);
				byte *hdDest = (byte *)_backBuffer.getPixels() + (s_dest.top * _backBuffer.pitch) + s_dest.left;

				// Iterate over the 1x Draw Bounds
				for (int16 y = 0; y < src.height(); y++) {
					byte *maskRow = maskBase + (y * ((uint16)resourceMask->getData(4) / 8));
					int run = 7 - nSkippedBits;
					uint skip = *maskRow >> nSkippedBits;

					byte *hdSrcRow = frameBufferPtr + (y * S * hdFrameRight);
					byte *hdDstRow = hdDest + (y * S * _backBuffer.pitch);

					for (int16 x = 0; x < src.width(); x++) {
						if (!(skip & 1)) {
							// Map 1x pixel to 2x2 HD block
							for (int sy = 0; sy < S; sy++) {
								for (int sx = 0; sx < S; sx++) {
									byte c = hdSrcRow[(sy * hdFrameRight) + (x * S) + sx];
									if (c) hdDstRow[(sy * _backBuffer.pitch) + (x * S) + sx] = c;
								}
							}
						}
						if (x == src.width() - 1) break;
						run--;
						if (run < 0) { maskRow++; run = 7; skip = *maskRow; }
						else skip >>= 1;
					}
				}
				if (mirroredBuffer) free(mirroredBuffer);
			} else {
				blit(&hdFrame, &s_src, &s_dest, flags);
			}

			// Restore internal state
			_backBuffer = tmp;
		}
	}
}

void Screen::draw(const Graphics::Surface &surface, int x, int y) {
	_backBuffer.copyRectToSurface(surface, x, y, Common::Rect(0, 0, surface.w, surface.h));

	int S = ASYLUM_SCALE_FACTOR;
	if (S > 1) {
		byte *hdPixels = (byte *)_hdBackBuffer.getPixels();
		uint32 hdPitch = _hdBackBuffer.pitch;

		for (int cy = 0; cy < surface.h; cy++) {
			const byte *srcRow = (const byte *)surface.getBasePtr(0, cy);
			byte *dstRow1 = hdPixels + (((y + cy) * S) * hdPitch) + (x * S);
			
			for (int cx = 0; cx < surface.w; cx++) {
				if (srcRow[cx]) {
					for (int sx = 0; sx < S; sx++) dstRow1[(cx * S) + sx] = srcRow[cx];
				}
			}
			for (int sy = 1; sy < S; sy++) {
				memcpy(hdPixels + (((y + cy) * S + sy) * hdPitch) + (x * S), dstRow1, surface.w * S);
			}
		}
	}
}

void Screen::clear() {
	_backBuffer.fillRect(Common::Rect(0, 0, LOGICAL_WIDTH, LOGICAL_HEIGHT), 0);
	_hdBackBuffer.fillRect(Common::Rect(0, 0, ASYLUM_SCREEN_WIDTH, ASYLUM_SCREEN_HEIGHT), 0);
	copyBackBufferToScreen();
}

void Screen::drawWideScreenBars(int16 barSize) const {
	if (barSize > 0) {
		_vm->_system->fillScreen(Common::Rect(0, 0, ASYLUM_SCREEN_WIDTH, barSize * ASYLUM_SCALE_FACTOR), 0);
		_vm->_system->fillScreen(Common::Rect(0, ASYLUM_SCREEN_HEIGHT - (barSize * ASYLUM_SCALE_FACTOR), ASYLUM_SCREEN_WIDTH, ASYLUM_SCREEN_HEIGHT), 0);
	}
}

void Screen::fillRect(int16 x, int16 y, int16 width, int16 height, uint32 color) {
	_backBuffer.fillRect(Common::Rect(x, y, x + width, y + height), color);
	int S = ASYLUM_SCALE_FACTOR;
	_hdBackBuffer.fillRect(Common::Rect(x * S, y * S, (x + width) * S, (y + height) * S), color);
}

void Screen::copyBackBufferToScreen() {
	// No more looping! Just blast the HD buffer straight to the monitor!
	_vm->_system->copyRectToScreen((byte *)_hdBackBuffer.getPixels(), _hdBackBuffer.w, 0, 0, _hdBackBuffer.w, _hdBackBuffer.h);
}

void Screen::clip(Common::Rect *source, Common::Rect *destination, int32 flags) const {
	int16 diffLeft = 0 - destination->left;
	if (diffLeft > 0) {
		destination->left = 0;
		if (flags & 2) source->right -= diffLeft;
		else source->left  += diffLeft;
	}

	int16 diffRight = destination->right - LOGICAL_WIDTH;
	if (diffRight > 0) {
		destination->right -= diffRight;
		if (flags & 2) source->left  += diffRight;
		else source->right -= diffRight;
	}

	int16 diffTop = 0 - destination->top;
	if (diffTop > 0) {
		destination->top = 0;
		source->top += diffTop;
	}

	int16 diffBottom = destination->bottom - LOGICAL_HEIGHT;
	if (diffBottom > 0) {
		source->bottom -= diffBottom;
		destination->bottom -= diffBottom;
	}
}

//////////////////////////////////////////////////////////////////////////
// Graphic Data (1x Blitters)
//////////////////////////////////////////////////////////////////////////
void Screen::blit(GraphicFrame *frame, Common::Rect *source, Common::Rect *destination, int32 flags) {
	if (!_transTable)
			error("[Screen::blit] Transparency table buffer not initialized");

	if ((uint32)flags & 0x80000000) {
		int32 flagSet = flags & 0x7FFFFFFF;
		bool hasTransTableIndex = false;

		if (flags & 0x10000000) {
			flagSet = flags & 0x6FFFFFFF;
			hasTransTableIndex = (_transTable ? true : false);
		}

		bool isMirrored = (flagSet == kDrawFlagMirrorLeftRight);

		if (hasTransTableIndex) {
			if (isMirrored) {
				blitTranstableMirrored((byte *)_backBuffer.getPixels()    + destination->top * _backBuffer.pitch    + destination->left,
				                       (byte *)frame->surface.getPixels() + source->top      * frame->surface.pitch + source->right - 1,
				                       destination->height(),
				                       destination->width(),
				                       (uint16)destination->width() + frame->surface.pitch,
				                       _backBuffer.pitch - (uint16)destination->width());
			} else {
				blitTranstable((byte *)_backBuffer.getPixels()    + destination->top * _backBuffer.pitch    + destination->left,
				               (byte *)frame->surface.getPixels() + source->top      * frame->surface.pitch + source->left,
				               destination->height(),
				               destination->width(),
				               frame->surface.pitch - (uint16)destination->width(),
				               _backBuffer.pitch    - (uint16)destination->width());
			}
		} else if (flagSet) {
			if (isMirrored) {
				if (_useColorKey) {
					blitMirroredColorKey((byte *)_backBuffer.getPixels()    + destination->top * _backBuffer.pitch    + destination->left,
					                     (byte *)frame->surface.getPixels() + source->top      * frame->surface.pitch + source->right,
					                     destination->height(),
					                     destination->width(),
					                     frame->surface.pitch + (uint16)destination->width(),
					                     _backBuffer.pitch    - (uint16)destination->width());
				} else {
					blitMirrored((byte *)_backBuffer.getPixels()    + destination->top * _backBuffer.pitch    + destination->left,
					             (byte *)frame->surface.getPixels() + source->top      * frame->surface.pitch + source->right,
					             destination->height(),
					             destination->width(),
					             frame->surface.pitch + (uint16)destination->width(),
					             _backBuffer.pitch    - (uint16)destination->width());
				}
			}
		} else {
			if (_useColorKey) {
				blitRawColorKey((byte *)_backBuffer.getPixels()    + destination->top * _backBuffer.pitch    + destination->left,
				                (byte *)frame->surface.getPixels() + source->top      * frame->surface.pitch + source->left,
				                destination->height(),
				                destination->width(),
				                frame->surface.pitch - (uint16)destination->width(),
				                _backBuffer.pitch    - (uint16)destination->width());
			} else {
				blitRaw((byte *)_backBuffer.getPixels()    + destination->top * _backBuffer.pitch    + destination->left,
				        (byte *)frame->surface.getPixels() + source->top      * frame->surface.pitch + source->left,
				        destination->height(),
				        destination->width(),
				        frame->surface.pitch - (uint16)destination->width(),
				        _backBuffer.pitch    - (uint16)destination->width());
			}
		}
	} else if (flags) {
		blt(destination, frame, source, flags);
	} else {
		bltFast(destination->left, destination->top, frame, source);
	}

	if (g_debugDrawRects)
		_backBuffer.frameRect(*destination, 0x220);
}

void Screen::blitTranstable(byte *dstBuffer, byte *srcBuffer, int16 height, int16 width, uint16 srcPitch, uint16 dstPitch) const {
	while (height--) {
		for (int16 i = width; i; --i) {
			if (*srcBuffer) *dstBuffer = _transTable[(*srcBuffer << 8) + *dstBuffer];
			dstBuffer++; srcBuffer++;
		}
		dstBuffer += dstPitch; srcBuffer += srcPitch;
	}
}

void Screen::blitTranstableMirrored(byte *dstBuffer, byte *srcBuffer, int16 height, int16 width, uint16 srcPitch, uint16 dstPitch) const {
	while (height--) {
		for (int16 i = width; i; --i) {
			if (*srcBuffer) *dstBuffer = _transTable[(*srcBuffer << 8) + *dstBuffer];
			dstBuffer++; srcBuffer--;
		}
		dstBuffer += dstPitch; srcBuffer += srcPitch;
	}
}

void Screen::blitCrossfade(byte *dstBuffer, byte *srcBuffer, byte *objectBuffer, int16 height, int16 width, uint16 srcPitch, uint16 dstPitch, uint16 objectPitch) const {
	while (height--) {
		for (int16 i = width; i; --i) {
			if (*srcBuffer) *dstBuffer = _transTable[(*srcBuffer << 8) + *objectBuffer];
			dstBuffer++; srcBuffer++; objectBuffer++;
		}
		dstBuffer += dstPitch; srcBuffer += srcPitch; objectBuffer += objectPitch;
	}
}

void Screen::blitMirrored(byte *dstBuffer, byte *srcBuffer, int16 height, int16 width, uint16 srcPitch, uint16 dstPitch) const {
	while (height--) {
		for (int16 i = width; i; --i) {
			*dstBuffer = *srcBuffer; dstBuffer++; srcBuffer--;
		}
		dstBuffer += dstPitch; srcBuffer += srcPitch;
	}
}

void Screen::blitMirroredColorKey(byte *dstBuffer, byte *srcBuffer, int16 height, int16 width, uint16 srcPitch, uint16 dstPitch) const {
	while (height--) {
		for (int16 i = width; i; --i) {
			if (*srcBuffer != 0) *dstBuffer = *srcBuffer;
			dstBuffer++; srcBuffer--;
		}
		dstBuffer += dstPitch; srcBuffer += srcPitch;
	}
}

void Screen::blitRaw(byte *dstBuffer, byte *srcBuffer, int16 height, int16 width, uint16 srcPitch, uint16 dstPitch) const {
	while (height--) {
		memcpy(dstBuffer, srcBuffer, (uint16)width);
		dstBuffer += dstPitch; srcBuffer += srcPitch;
	}
}

void Screen::blitRawColorKey(byte *dstBuffer, byte *srcBuffer, int16 height, int16 width, uint16 srcPitch, uint16 dstPitch) const {
	while (height--) {
		for (int16 i = width; i; --i) {
			if (*srcBuffer != 0) *dstBuffer = *srcBuffer;
			dstBuffer++; srcBuffer++;
		}
		dstBuffer += dstPitch; srcBuffer += srcPitch;
	}
}

void Screen::blitMasked(GraphicFrame *frame, Common::Rect *source, byte *maskData, Common::Rect *sourceMask, Common::Rect *destMask, uint16 maskWidth, Common::Rect *destination, int32 flags) {
	byte *frameBuffer = (byte *)frame->surface.getPixels();
	byte *mirroredBuffer = nullptr;
	int16 frameRight = frame->surface.pitch;
	uint16 maskHeight = (uint16)sourceMask->height(); 
	byte nSkippedBits = ABS(sourceMask->left) % 8;

	if (flags & kDrawFlagMirrorLeftRight) {
		mirroredBuffer = (byte *)malloc((size_t)(source->right * source->bottom));
		if (!mirroredBuffer) error("[Screen::blitMasked] Cannot allocate buffer for mirrored surface");
		blitMirrored(mirroredBuffer, frameBuffer + source->right - 1, source->bottom, source->right, (uint16)(source->right + frame->surface.pitch), 0);
		frameBuffer = mirroredBuffer;
		frameRight = source->right;
		source->right -= source->left;
		source->left = 0;
	}

	byte *frameBufferPtr = frameBuffer + source->top * frameRight + source->left;
	byte *maskBufferPtr  = maskData    + sourceMask->top * (maskWidth / 8) + sourceMask->left / 8;

	if ((destMask->left    + sourceMask->width())  < destination->left
	 || (destination->left + source->width())      < destMask->left
	 || (destMask->top     + sourceMask->height()) < destination->top
	 || (destination->top  + source->height())     < destMask->top) {
		blitRawColorKey((byte *)_backBuffer.getPixels() + destination->top * _backBuffer.pitch + destination->left,
		                frameBufferPtr, source->height(), source->width(),
		                (uint16)(frameRight - source->width()), (uint16)(_backBuffer.pitch - source->width()));
		free(mirroredBuffer);
		if (g_debugDrawRects) _backBuffer.frameRect(*destMask, 0x220);
		return;
	}

	if (destination->left > destMask->left) {
		nSkippedBits += ABS(destination->left - destMask->left) % 8;
		maskBufferPtr += (destination->left - destMask->left) / 8 + nSkippedBits / 8;
		nSkippedBits %= 8;
		sourceMask->setWidth(sourceMask->width() + destMask->left - destination->left);
		destMask->left = destination->left;
	}

	if (destination->top > destMask->top) {
		maskBufferPtr += (destination->top - destMask->top) * maskWidth / 8;
		sourceMask->setHeight(sourceMask->height() + destMask->top - destination->top);
		destMask->top = destination->top;
	}

	if (destination->left < destMask->left) {
		blitRawColorKey((byte *)_backBuffer.getPixels() + destination->top * _backBuffer.pitch + destination->left,
		                frameBufferPtr, source->height(), destMask->left - destination->left,
		                (uint16)(frameRight + destination->left - destMask->left),
		                (uint16)(_backBuffer.pitch + destination->left - destMask->left));
		if (g_debugDrawRects) _backBuffer.frameRect(Common::Rect(destination->left, destination->top, destMask->left, destination->top + source->height()), 0x10);
		frameBufferPtr += destMask->left - destination->left;
		source->setWidth(source->width() + destination->left - destMask->left);
		destination->left = destMask->left;
	}

	if ((source->width() + destination->left) > (destMask->left + sourceMask->width())) {
		blitRawColorKey((byte *)_backBuffer.getPixels() + destination->top * _backBuffer.pitch + destMask->left + sourceMask->width(),
		                frameBufferPtr + destMask->left + sourceMask->width() - destination->left,
		                source->height(), source->width() + destination->left - (destMask->left + sourceMask->width()),
		                (uint16)(frameRight + destMask->left + sourceMask->width() - (destination->left + source->width())),
		                (uint16)(_backBuffer.pitch + destMask->left + sourceMask->width() - (destination->left + source->width())));
		if (g_debugDrawRects) _backBuffer.frameRect(Common::Rect(destMask->left, destination->top, destMask->left + source->width(), destination->top + source->height()), 0x36);
		source->setWidth(destMask->left + sourceMask->width() - destination->left);
	}

	if (destination->top < destMask->top) {
		blitRawColorKey((byte *)_backBuffer.getPixels() + destination->top * _backBuffer.pitch + destination->left,
		                frameBufferPtr, destMask->top - destination->top, source->width(),
		                (uint16)(frameRight - source->width()), (uint16)(_backBuffer.pitch - source->width()));
		if (g_debugDrawRects) _backBuffer.frameRect(Common::Rect(destination->left, destination->top, destination->left + source->width(), destMask->top), 0x23);
		frameBufferPtr += (destMask->top - destination->top) * frameRight;
		source->setHeight(source->height() + destination->top - destMask->top);
		destination->top = destMask->top;
	}

	if ((source->height() + destination->top) > (destMask->top + sourceMask->height())) {
		blitRawColorKey((byte *)_backBuffer.getPixels() + (destMask->top + sourceMask->height()) * _backBuffer.pitch + destination->left,
		                frameBufferPtr + (destMask->top + sourceMask->height() - destination->top) * frameRight,
		                destination->top + source->height() - (sourceMask->height() + destMask->top), source->width(),
		                (uint16)(frameRight - source->width()), (uint16)(_backBuffer.pitch - source->width()));
		source->setHeight(destMask->top + sourceMask->height() - destination->top);
	}

	bltMasked(frameBufferPtr, maskBufferPtr, source->height(), source->width(),
	          (uint16)(frameRight - source->width()), (uint16)(maskWidth - (nSkippedBits + source->width())) / 8,
	          nSkippedBits, (byte *)_backBuffer.getPixels() + _backBuffer.pitch * destination->top + destination->left,
	          (uint16)(_backBuffer.pitch - source->width()));

	if (g_debugDrawRects) {
		_backBuffer.frameRect(*destination, 0x128);
		drawZoomedMask(maskData, maskHeight / 8, maskWidth / 8, maskWidth);
	}
	free(mirroredBuffer);
}

void Screen::drawZoomedMask(byte *mask, uint16 height, uint16 width, uint16 maskPitch) {
	uint16 zoom = 7;
	byte *dstBuffer = (byte *)_backBuffer.getPixels();
	uint16 dstPitch = (uint16)(_backBuffer.pitch - (width * zoom));
	uint16 srcPitch = maskPitch;
	byte *srcBuffer = mask;

	height *= zoom;
	while (height--) {
		for (int16 i = 0; i < width; i++) {
			for (int j = 0; j < zoom; j++) {
				*dstBuffer = *srcBuffer;
				dstBuffer++;
			}
			srcBuffer++;
		}
		dstBuffer += dstPitch;
		srcBuffer += (height % zoom) ? -width : srcPitch;
	}
}

void Screen::bltMasked(byte *srcBuffer, byte *maskBuffer, int16 height, int16 width, uint16 srcPitch, uint16 maskPitch, byte nSkippedBits, byte *dstBuffer, uint16 dstPitch) const {
	if (nSkippedBits > 7) error("[Screen::bltMasked] Invalid number of skipped bits (was: %d, max: 7)", nSkippedBits);

	while (height--) {
		int run = 7 - nSkippedBits;
		uint skip = *maskBuffer >> nSkippedBits;

		for (int16 i = 0; i < width; i++) {
			if (*srcBuffer && !(skip & 1)) *dstBuffer = *srcBuffer;
			dstBuffer++; srcBuffer++;

			if (i == width - 1) break;

			run--;
			if (run < 0) {
				++maskBuffer;
				run  = 7;
				skip = *maskBuffer;
			} else {
				skip >>= 1;
			}
		}
		dstBuffer  += dstPitch;
		srcBuffer  += srcPitch;
		maskBuffer += maskPitch + 1;
	}
}

void Screen::blt(Common::Rect *dest, GraphicFrame *frame, Common::Rect *source, int32 flags) {
	if (_useColorKey) {
		copyToBackBufferWithTransparency((byte *)frame->surface.getBasePtr(source->left, source->top), frame->surface.pitch,
		                                 dest->left, dest->top, (uint16)source->width(), (uint16)source->height(), (bool)(flags & kDrawFlagMirrorLeftRight));
	} else {
		copyToBackBuffer((byte *)frame->surface.getBasePtr(source->left, source->top), frame->surface.pitch,
		                 dest->left, dest->top, (uint16)source->width(), (uint16)source->height(), (bool)(flags & kDrawFlagMirrorLeftRight));
	}
}

void Screen::bltFast(int16 dX, int16 dY, GraphicFrame *frame, Common::Rect *source) {
	if (!frame->surface.getPixels() || source->width() == 0 || source->height() == 0) return;
	if (_useColorKey) _backBuffer.copyRectToSurfaceWithKey(frame->surface, dX, dY, *source, 0x00);
	else _backBuffer.copyRectToSurface(frame->surface, dX, dY, *source);
}

void Screen::copyToBackBuffer(const byte *buffer, int32 pitch, int16 x, int16 y, uint16 width, uint16 height, bool mirrored) {
	if (!buffer || width == 0 || height == 0) return;
	
	if (!mirrored) {
		_backBuffer.copyRectToSurface(buffer, pitch, x, y, width, height);

		int S = ASYLUM_SCALE_FACTOR;
		if (S > 1) {
			byte *hdPixels = (byte *)_hdBackBuffer.getPixels();
			uint32 hdPitch = _hdBackBuffer.pitch;

			for (int cy = 0; cy < height; cy++) {
				const byte *srcRow = buffer + (cy * pitch);
				byte *dstRow1 = hdPixels + (((y + cy) * S) * hdPitch) + (x * S);
				
				// Scale X incredibly fast
				for (int cx = 0; cx < width; cx++) {
					byte c = srcRow[cx];
					for (int sx = 0; sx < S; sx++) dstRow1[(cx * S) + sx] = c;
				}
				// Clone Y rows using high-speed memcpy!
				for (int sy = 1; sy < S; sy++) {
					memcpy(hdPixels + (((y + cy) * S + sy) * hdPitch) + (x * S), dstRow1, width * S);
				}
			}
		}
	} else {
		error("[Screen::copyToBackBuffer] Mirrored drawing not implemented (no color key)");
	}
}

void Screen::copyToBackBufferWithTransparency(byte *buffer, int32 pitch, int16 x, int16 y, uint16 width, uint16 height, bool mirrored) {
	byte *dest = (byte *)_backBuffer.getPixels();
	int32 left = (x < 0) ? -x : 0;
	int32 top = (y < 0) ? -y : 0;
	int32 right = (x + width > LOGICAL_WIDTH) ? LOGICAL_WIDTH - abs(x) : width;
	int32 bottom = (y + height > LOGICAL_HEIGHT) ? LOGICAL_HEIGHT - abs(y) : height;

	for (int32 curY = top; curY < bottom; curY++) {
		for (int32 curX = left; curX < right; curX++) {
			uint32 offset = (uint32)((mirrored ? right - (curX + 1) : curX) + curY * pitch);
			if (buffer[offset] != 0) dest[x + curX + (y + curY) * LOGICAL_WIDTH] = buffer[offset];
		}
	}

	int S = ASYLUM_SCALE_FACTOR;
	if (S > 1) {
		byte *hdDest = (byte *)_hdBackBuffer.getPixels();
		for (int32 curY = top; curY < bottom; curY++) {
			for (int32 curX = left; curX < right; curX++) {
				uint32 offset = (uint32)((mirrored ? right - (curX + 1) : curX) + curY * pitch);
				if (buffer[offset] != 0) {
					byte color = buffer[offset];
					for (int sy = 0; sy < S; sy++) {
						for (int sx = 0; sx < S; sx++) {
							hdDest[((x + curX) * S + sx) + ((y + curY) * S + sy) * ASYLUM_SCREEN_WIDTH] = color;
						}
					}
				}
			}
		}
	}
}

//////////////////////////////////////////////////////////////////////////
// Palette
//////////////////////////////////////////////////////////////////////////
byte *Screen::getPaletteData(ResourceId id) {
	ResourceEntry *resource = getResource()->get(id);
	byte flag = *(resource->data + 5);
	if (!(flag & 32))
		error("[Screen::getPaletteData] Invalid palette resource id %d (0x%X) with flag %d", id, id, flag);
	return (resource->data + resource->getData(12));
}

void Screen::loadGrayPalette() {
	ResourceId paletteId = getWorld()->actions[getScene()->getActor()->getActionIndex3()]->paletteResourceId;
	if (!paletteId)
		paletteId = getWorld()->currentPaletteId;

	byte *paletteData = getPaletteData(paletteId);
	paletteData += 4;

	for (uint32 j = 3; j < ARRAYSIZE(_currentPalette) - 3; j += 3) {
		uint32 gray = 4 * (paletteData[j] + paletteData[j + 1] + paletteData[j + 2]) / 3;
		_currentPalette[j] = _currentPalette[j + 1] = _currentPalette[j + 2] = (byte)gray;
	}
}

void Screen::setPalette(ResourceId id) {
	byte *data = getPaletteData(id);
	setupPalette(data + 4, data[2], READ_LE_UINT16(data));
}

void Screen::setMainPalette(const byte *data) {
	memcpy(&_mainPalette, data, sizeof(_mainPalette));
}

void Screen::setupPalette(byte *buffer, int start, int count) {
	if (start < 0 || start > 256)
		error("[Screen::setupPalette] Invalid start parameter (was: %d, valid: [0 ; 255])", start);

	if ((count + start) > 256)
		error("[Screen::setupPalette] Parameters go past the palette buffer (start: %d, count: %d with sum > 256)", start, count);

	if (count > 0) {
		byte *palette = (byte *)_mainPalette;
		palette += start;

		for (int32 i = 0; i < count; i++) {
			palette[0] = (byte)(buffer[0] * 4);
			palette[1] = (byte)(buffer[1] * 4);
			palette[2] = (byte)(buffer[2] * 4);
			buffer  += 3;
			palette += 3;
		}
	}
	_vm->_system->getPaletteManager()->setPalette(_mainPalette, 0, 256);
}

void Screen::updatePalette() {
	debugC(kDebugLevelScene, "[Screen::updatePalette] Not implemented!");
}

void Screen::updatePalette(int32 param) {
	if (param >= 21) {
		for (uint32 j = 3; j < ARRAYSIZE(_mainPalette) - 3; j += 3) {
			_mainPalette[j]     = _currentPalette[j];
			_mainPalette[j + 1] = _currentPalette[j + 1];
			_mainPalette[j + 2] = _currentPalette[j + 2];
		}
		setupPalette(nullptr, 0, 0);
		paletteFade(0, 25, 10);
	} else {
		ResourceId paletteId = getWorld()->actions[getScene()->getActor()->getActionIndex3()]->paletteResourceId;
		if (!paletteId)
			paletteId = getWorld()->currentPaletteId;

		byte *paletteData = getPaletteData(paletteId);
		paletteData += 4;

		float fParam = param / 20.0;
		for (uint32 j = 3; j < ARRAYSIZE(_mainPalette) - 3; j += 3) {
			_mainPalette[j]     = (byte)((1.0 - fParam) * 4 * paletteData[j]     + fParam * _currentPalette[j]);
			_mainPalette[j + 1] = (byte)((1.0 - fParam) * 4 * paletteData[j + 1] + fParam * _currentPalette[j + 1]);
			_mainPalette[j + 2] = (byte)((1.0 - fParam) * 4 * paletteData[j + 2] + fParam * _currentPalette[j + 2]);
		}
		setupPalette(nullptr, 0, 0);
	}
}

void Screen::queuePaletteFade(ResourceId resourceId, int32 ticksWait, int32 delta) {
	if (_isFading && !_fadeQueue.empty() && _fadeQueue.front().resourceId == resourceId)
		return;
	if (ticksWait < 0 || delta <= 0)
		return;

	FadeParameters fadeParams = {resourceId, ticksWait, delta, _vm->getTick(), 1};
	_fadeQueue.push(fadeParams);
}

void Screen::stopPaletteFade(char red, char green, char blue) {
	for (uint i = 3; i < ARRAYSIZE(_mainPalette) - 3; i += 3) {
		_mainPalette[i]     = (byte)red;
		_mainPalette[i + 1] = (byte)green;
		_mainPalette[i + 2] = (byte)blue;
	}
	stopQueuedPaletteFade();
	setupPalette(nullptr, 0, 0);
}

void Screen::stopPaletteFadeAndSet(ResourceId id, int32 ticksWait, int32 delta) {
	stopQueuedPaletteFade();
	initQueuedPaletteFade(id, delta);
	for (int i = 1; i < delta + 1; i++) {
		runQueuedPaletteFade(id, delta, i);
		g_system->delayMillis((uint32)ticksWait);
		g_system->updateScreen();
	}
}

void Screen::paletteFade(uint32 start, int32 ticksWait, int32 delta) {
	if (start > 255 || ticksWait < 0 || delta <= 0)
		return;

	byte palette[PALETTE_SIZE];
	memcpy(&palette,  &_mainPalette, sizeof(palette));

	int32 colorDelta = delta + 1;
	byte red   = palette[3 * start];
	byte green = palette[3 * start + 1];
	byte blue  = palette[3 * start + 2];

	for (int32 i = 1; i < colorDelta; i++) {
		for (uint32 j = 3; j < ARRAYSIZE(_mainPalette) - 3; j += 3) {
			_mainPalette[j]     = (byte)(palette[j]     + i * (red   - palette[j])     / colorDelta);
			_mainPalette[j + 1] = (byte)(palette[j + 1] + i * (green - palette[j + 1]) / colorDelta);
			_mainPalette[j + 2] = (byte)(palette[j + 2] + i * (blue  - palette[j + 2]) / colorDelta);
		}
		setupPalette(nullptr, 0, 0);
		g_system->delayMillis((uint32)ticksWait);
		Common::Event ev;
		do {
		} while (_vm->getEventManager()->pollEvent(ev));
		g_system->updateScreen();
	}
}

void Screen::processPaletteFadeQueue() {
	if (_fadeQueue.empty())
		return;

	FadeParameters *current = &_fadeQueue.front();
	if (_vm->getTick() > current->nextTick) {
		if (current->step > current->delta) {
			_isFading = false;
			(void)_fadeQueue.pop();
			if (_fadeQueue.empty()) {
				stopQueuedPaletteFade();
				return;
			}
			current = &_fadeQueue.front();
			initQueuedPaletteFade(current->resourceId, current->delta);
		} else {
			if (current->step == 1)
				initQueuedPaletteFade(current->resourceId, current->delta);
			current->nextTick += current->ticksWait;
		}
		runQueuedPaletteFade(current->resourceId, current->delta, current->step++);
	}
}

void Screen::initQueuedPaletteFade(ResourceId id, int32 delta) {
	_fadeStop = false;
	_isFading = true;
	byte *data = getPaletteData(id);

	memcpy(_fromPalette, _mainPalette, sizeof(_fromPalette));
	memcpy(_toPalette,   _mainPalette, sizeof(_toPalette));

	int16 count = READ_LE_UINT16(data);
	byte start = data[2];
	if (count > 0) {
		byte *pData = data + 4;
		for (int16 i = 0; i < count; i++) {
			_toPalette[i + start]     = (byte)(4 * pData[0]);
			_toPalette[i + start + 1] = (byte)(4 * pData[1]);
			_toPalette[i + start + 2] = (byte)(4 * pData[2]);
			pData += 3;
		}
	}
	setPaletteGamma(data, _toPalette);
}

void Screen::runQueuedPaletteFade(ResourceId id, int32 delta, int i) {
	if (_fadeStop) return;
	int32 colorDelta = delta + 1;
	for (uint32 j = 3; j < ARRAYSIZE(_mainPalette) - 3; j += 3) {
		_mainPalette[j]     = (byte)(_fromPalette[j]     + i * (_toPalette[j]     - _fromPalette[j])     / colorDelta);
		_mainPalette[j + 1] = (byte)(_fromPalette[j + 1] + i * (_toPalette[j + 1] - _fromPalette[j + 1]) / colorDelta);
		_mainPalette[j + 2] = (byte)(_fromPalette[j + 2] + i * (_toPalette[j + 2] - _fromPalette[j + 2]) / colorDelta);
	}
	setupPalette(nullptr, 0, 0);
}

void Screen::stopQueuedPaletteFade() {
	if (!_isFading) return;
	_fadeStop = true;
}

void Screen::setPaletteGamma(ResourceId id) {
	setPaletteGamma(getPaletteData(id));
}

void Screen::setPaletteGamma(byte *data, byte *target) {
	if (target == nullptr)
		target = (byte *)&_mainPalette;

	data += 4;
	for (int32 i = 1; i < 256; i++) {
		byte color = 0;
		if (data[0] > 0) color = data[0];
		if (data[1] > color) color = data[1];
		if (data[2] > color) color = data[2];

		int gamma = color + (Config.gammaLevel * (63 - color) + 31) / 63;

		if (gamma && color != 0) {
			if (data[0]) target[0] = (byte)(4 * ((color >> 1) + data[0] * gamma) / color);
			if (data[1]) target[1] = (byte)(4 * ((color >> 1) + data[1] * gamma) / color);
			if (data[2]) target[2] = (byte)(4 * ((color >> 1) + data[2] * gamma) / color);
		}
		target += 3;
		data   += 3;
	}
}

void Screen::setGammaLevel(ResourceId id) {
	if (!Config.gammaLevel) return;
	if (!id) error("[Screen::setGammaLevel] Resource Id is invalid");

	setPaletteGamma(getPaletteData(id));
	setupPalette(nullptr, 0, 0);
}

void Screen::setupTransTable(ResourceId resourceId) {
	if (resourceId) setupTransTables(1, resourceId);
	else setupTransTables(0);
}

void Screen::setupTransTables(uint32 count, ...) {
	if (!count) {
		clearTransTables();
		return;
	}

	va_list va;
	va_start(va, count);

	if (_transTableCount != count) clearTransTables();
	_transTableCount = count;

	if (!_transTableBuffer) {
		_transTableBuffer = (byte *)malloc(count * TRANSPARENCY_TABLE_SIZE);
		if (!_transTableBuffer) error("[Screen::setupTransTables] Cannot allocate memory for transparency table buffer");
		_transTable = _transTableBuffer;
	}

	uint32 index = 0;
	for (uint32 i = 0; i < _transTableCount; i++) {
		ResourceId id = va_arg(va, ResourceId);
		memcpy(&_transTableBuffer[index], getResource()->get(id)->data, TRANSPARENCY_TABLE_SIZE);
		index += TRANSPARENCY_TABLE_SIZE;
	}
	va_end(va);
}

void Screen::clearTransTables() {
	free(_transTableBuffer);
	_transTableBuffer = nullptr;
	_transTable = nullptr;
	_transTableCount = 0;
}

void Screen::selectTransTable(uint32 index) {
	if (!_transTableBuffer) error("[Screen::selectTransTable] Transparency table buffer not initialized");
	if (index >= _transTableCount) return;
	_transTable = &_transTableBuffer[TRANSPARENCY_TABLE_SIZE * index];
}

void Screen::addGraphicToQueue(ResourceId resourceId, uint32 frameIndex, const Common::Point &point, DrawFlags flags, int32 transTableNum, int32 priority) {
	GraphicQueueItem item;
	item.priority = priority;
	item.type = kGraphicItemNormal;
	item.source = point; 
	item.resourceId = resourceId;
	item.frameIndex = frameIndex;
	item.flags = flags;
	item.transTableNum = transTableNum;
	_queueItems.push_back(item);
}

void Screen::addGraphicToQueue(ResourceId resourceId, uint32 frameIndex, const int16 (*pointPtr)[2], DrawFlags flags, int32 transTableNum, int32 priority) {
	addGraphicToQueue(resourceId, frameIndex, Common::Point((*pointPtr)[0], (*pointPtr)[1]), flags, transTableNum, priority);
}

void Screen::addGraphicToQueueMasked(ResourceId resourceId, uint32 frameIndex, const Common::Point &source, int32 resourceIdDestination, const Common::Point &destination, DrawFlags flags, int32 priority) {
	GraphicQueueItem item;
	item.priority = priority;
	item.type = kGraphicItemMasked;
	item.source = source;
	item.resourceId = resourceId;
	item.frameIndex = frameIndex;
	item.flags = flags;
	item.resourceIdDestination = resourceIdDestination;
	item.destination = destination;
	_queueItems.push_back(item);
}

void Screen::addGraphicToQueueCrossfade(ResourceId resourceId, uint32 frameIndex, const Common::Point &point, int32 objectResourceId, const Common::Point &destination, uint32 transTableNum) {
	byte *transparencyIndex = _transTable;
	selectTransTable(transTableNum);

	GraphicResource *resource = new GraphicResource(_vm, resourceId);
	GraphicFrame *frame = resource->getFrame(frameIndex);

	GraphicResource *resourceObject = new GraphicResource(_vm, objectResourceId);
	GraphicFrame *frameObject = resourceObject->getFrame(0);

	Common::Rect src(0, 0, frame->getWidth(), frame->getHeight());
	Common::Rect dst = src;
	
	dst.translate(point.x + frame->x, point.y + frame->y);

	clip(&src, &dst, 0);
	if (src.isValidRect()) {
		_useColorKey = true;

		// 1x Crossfade Draw
		blitCrossfade((byte *)_backBuffer.getPixels()          + dst.top                   * _backBuffer.pitch          + dst.left,
		              (byte *)frame->surface.getPixels()       + src.top                   * frame->surface.pitch       + src.left,
		              (byte *)frameObject->surface.getPixels() + (destination.y + dst.top) * frameObject->surface.pitch + (dst.left + destination.x),
		              dst.height(),
		              dst.width(),
		              (uint16)(frame->surface.pitch       - dst.width()),
		              (uint16)(_backBuffer.pitch          - dst.width()),
		              (uint16)(frameObject->surface.pitch - dst.width()));

		// HD Crossfade Draw
		int S = ASYLUM_SCALE_FACTOR;
		if (S > 1) {
			Graphics::Surface tmp = _backBuffer;
			_backBuffer = _hdBackBuffer;
			
			Graphics::Surface origF = frame->surface;
			Graphics::Surface origO = frameObject->surface;

			Graphics::Surface *hdF = getHDSurface(resourceId, frameIndex);
			Graphics::Surface *hdO = getHDSurface(objectResourceId, 0);

			if (hdF) frame->surface = *hdF;
			if (hdO) frameObject->surface = *hdO;

			Common::Rect s_src(src.left * S, src.top * S, src.right * S, src.bottom * S);
			Common::Rect s_dst(dst.left * S, dst.top * S, dst.right * S, dst.bottom * S);

			blitCrossfade((byte *)_backBuffer.getPixels()          + s_dst.top                   * _backBuffer.pitch          + s_dst.left,
			              (byte *)frame->surface.getPixels()       + s_src.top                   * frame->surface.pitch       + s_src.left,
			              (byte *)frameObject->surface.getPixels() + (destination.y * S + s_dst.top) * frameObject->surface.pitch + (s_dst.left + destination.x * S),
			              s_dst.height(),
			              s_dst.width(),
			              (uint16)(frame->surface.pitch       - s_dst.width()),
			              (uint16)(_backBuffer.pitch          - s_dst.width()),
			              (uint16)(frameObject->surface.pitch - s_dst.width()));

			if (hdF) frame->surface = origF;
			if (hdO) frameObject->surface = origO;

			_backBuffer = tmp;
		}
	}

	_transTable = transparencyIndex;
	delete resource;
	delete resourceObject;
}

void Screen::addGraphicToQueue(GraphicQueueItem const &item) {
	_queueItems.push_back(item);
}

bool Screen::graphicQueueItemComparator(const GraphicQueueItem &item1, const GraphicQueueItem &item2) {
	return item1.priority > item2.priority;
}

void Screen::drawGraphicsInQueue() {
	Common::sort(_queueItems.begin(), _queueItems.end(), &Screen::graphicQueueItemComparator);

	for (const auto &item : _queueItems) {
		if (item.type == kGraphicItemNormal) {
			if (item.transTableNum <= 0 || Config.performance <= 1)
				draw(item.resourceId, item.frameIndex, item.source, item.flags);
			else
				drawTransparent(item.resourceId, item.frameIndex, item.source, item.flags, (uint32)(item.transTableNum - 1));
		} else if (item.type == kGraphicItemMasked) {
			draw(item.resourceId, item.frameIndex, item.source, item.flags, item.resourceIdDestination, item.destination);
		}
	}
}

void Screen::clearGraphicsInQueue() {
	_queueItems.clear();
}

void Screen::deleteGraphicFromQueue(ResourceId resourceId) {
	for (uint32 i = 0; i < _queueItems.size(); i++) {
		if (_queueItems[i].resourceId == resourceId) {
			_queueItems.remove_at(i);
			break;
		}
	}
}

void Screen::drawLine(const Common::Point &source, const Common::Point &destination, uint32 color) {
	int scale = ASYLUM_SCALE_FACTOR;
	_backBuffer.drawLine(source.x, source.y, destination.x, destination.y, color);
	_hdBackBuffer.drawLine(source.x * scale, source.y * scale, destination.x * scale, destination.y * scale, color);
}

void Screen::drawLine(const int16 (*srcPtr)[2], const int16 (*dstPtr)[2], uint32 color) {
	int scale = ASYLUM_SCALE_FACTOR;
	_backBuffer.drawLine((*srcPtr)[0], (*srcPtr)[1], (*dstPtr)[0], (*dstPtr)[1], color);
	_hdBackBuffer.drawLine((*srcPtr)[0] * scale, (*srcPtr)[1] * scale, (*dstPtr)[0] * scale, (*dstPtr)[1] * scale, color);
}

void Screen::drawRect(const Common::Rect &rect, uint32 color) {
	int scale = ASYLUM_SCALE_FACTOR;
	_backBuffer.frameRect(rect, color);
	Common::Rect s_rect(rect.left * scale, rect.top * scale, rect.right * scale, rect.bottom * scale);
	_hdBackBuffer.frameRect(s_rect, color);
}

void Screen::copyToBackBufferClipped(Graphics::Surface *surface, int16 x, int16 y) {
	Common::Rect screenRect(getWorld()->xLeft, getWorld()->yTop, getWorld()->xLeft + LOGICAL_WIDTH, getWorld()->yTop + LOGICAL_HEIGHT);
	Common::Rect animRect(x, y, x + (int16)surface->w, y + (int16)surface->h);
	animRect.clip(screenRect);

	if (!animRect.isEmpty()) {
		animRect.translate(-(int16)getWorld()->xLeft, -(int16)getWorld()->yTop);

		int startX = animRect.right  == LOGICAL_WIDTH ? 0 : surface->w - animRect.width();
		int startY = animRect.bottom == LOGICAL_HEIGHT ? 0 : surface->h - animRect.height();

		if (surface->w > LOGICAL_WIDTH)
			startX = getWorld()->xLeft;
		if (surface->h > LOGICAL_HEIGHT)
			startY = getWorld()->yTop;

		_vm->screen()->copyToBackBufferWithTransparency(
			((byte *)surface->getPixels()) +
			startY * surface->pitch +
			startX * surface->format.bytesPerPixel,
			surface->pitch,
			animRect.left,
			animRect.top,
			(uint16)animRect.width(),
			(uint16)animRect.height());
	}
}

} // end of namespace Asylum
