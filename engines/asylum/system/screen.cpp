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

extern int ASYLUM_SCALE_FACTOR;
extern Graphics::Surface *getHDSurface(uint32 resourceId, uint32 frameIndex);

int g_debugDrawRects;

#define TRANSPARENCY_TABLE_SIZE (256 * 256)

Screen::Screen(AsylumEngine *vm) : _vm(vm) ,
	_useColorKey(false), _transTableCount(0), _transTable(nullptr), _transTableBuffer(nullptr) {
	
	// The 1x Logical Buffer (The Game Brain relies on this)
	_backBuffer.create(ASYLUM_SCREEN_WIDTH, ASYLUM_SCREEN_HEIGHT, Graphics::PixelFormat::createFormatCLUT8());

	// The 2x HD Projector Buffer (What the player actually sees!)
	_hdBackBuffer.create(ASYLUM_SCREEN_WIDTH * ASYLUM_SCALE_FACTOR, ASYLUM_SCREEN_HEIGHT * ASYLUM_SCALE_FACTOR, Graphics::PixelFormat::createFormatCLUT8());

	_flag = -1;
	_clipRect = Common::Rect(0, 0, ASYLUM_SCREEN_WIDTH, ASYLUM_SCREEN_HEIGHT);

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

	// 1x Clipping Math (Game Brain)
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

	// --- HD PROJECTOR SWAP ---
	Graphics::Surface origSurf = frame->surface;
	Graphics::Surface *hdSurf = getHDSurface(resource->getResourceId(), frameIndex);
	if (hdSurf) frame->surface = *hdSurf;
	// -------------------------

	if (masked) {
		blitMasked(frame, &src, resourceMask->data + 8, &srcMask, &destMask, (uint16)resourceMask->getData(4), &dest, flags);
	} else {
		blit(frame, &src, &dest, flags);
	}

	// --- RESTORE 1X BRAIN ---
	if (hdSurf) frame->surface = origSurf;
	// ------------------------
}

void Screen::draw(const Graphics::Surface &surface, int x, int y) {
	int S = ASYLUM_SCALE_FACTOR;
	_backBuffer.copyRectToSurface(surface, x, y, Common::Rect(0, 0, surface.w, surface.h));

	// Upscale static UI directly to the HD Buffer
	for (int cy = 0; cy < surface.h; cy++) {
		for (int cx = 0; cx < surface.w; cx++) {
			byte color = *(const byte *)surface.getBasePtr(cx, cy);
			if (color) {
				for (int sy = 0; sy < S; sy++) {
					for (int sx = 0; sx < S; sx++) {
						byte *dst = (byte *)_hdBackBuffer.getBasePtr((x * S) + (cx * S) + sx, (y * S) + (cy * S) + sy);
						*dst = color;
					}
				}
			}
		}
	}
}

void Screen::clear() {
	_backBuffer.fillRect(Common::Rect(0, 0, ASYLUM_SCREEN_WIDTH, ASYLUM_SCREEN_HEIGHT), 0);
	_hdBackBuffer.fillRect(Common::Rect(0, 0, _hdBackBuffer.w, _hdBackBuffer.h), 0);
	copyBackBufferToScreen();
}

void Screen::drawWideScreenBars(int16 barSize) const {
	if (barSize > 0) {
		_vm->_system->fillScreen(Common::Rect(0, 0, _hdBackBuffer.w, barSize * ASYLUM_SCALE_FACTOR), 0);
		_vm->_system->fillScreen(Common::Rect(0, _hdBackBuffer.h - (barSize * ASYLUM_SCALE_FACTOR), _hdBackBuffer.w, _hdBackBuffer.h), 0);
	}
}

void Screen::fillRect(int16 x, int16 y, int16 width, int16 height, uint32 color) {
	_backBuffer.fillRect(Common::Rect(x, y, x + width, y + height), color);
	int S = ASYLUM_SCALE_FACTOR;
	_hdBackBuffer.fillRect(Common::Rect(x * S, y * S, (x + width) * S, (y + height) * S), color);
}

void Screen::copyBackBufferToScreen() {
	// Send the HD Projector Buffer to the monitor!
	_vm->_system->copyRectToScreen((byte *)_hdBackBuffer.getPixels(), _hdBackBuffer.w, 0, 0, _hdBackBuffer.w, _hdBackBuffer.h);
}

void Screen::clip(Common::Rect *source, Common::Rect *destination, int32 flags) const {
	int16 diffLeft = _clipRect.left - destination->left;
	if (diffLeft > 0) {
		destination->left = _clipRect.left;
		if (flags & 2) source->right -= diffLeft;
		else source->left  += diffLeft;
	}

	int16 diffRight = destination->right - _clipRect.right;
	if (diffRight > 0) {
		destination->right -= diffRight;
		if (flags & 2) source->left  += diffRight;
		else source->right -= diffRight;
	}

	int16 diffTop = _clipRect.top - destination->top;
	if (diffTop > 0) {
		destination->top = _clipRect.top;
		source->top += diffTop;
	}

	int16 diffBottom = destination->bottom - _clipRect.bottom;
	if (diffBottom > 0) {
		source->bottom -= diffBottom;
		destination->bottom -= diffBottom;
	}
}

// --------------------------------------------------------------------------------------
// HD BLITTERS: The Game Brain gives us 1x coordinates, we output scaled HD pixels!
// --------------------------------------------------------------------------------------
void Screen::blit(GraphicFrame *frame, Common::Rect *source, Common::Rect *destination, int32 flags) {
	if (!_transTable) error("[Screen::blit] Transparency table buffer not initialized");

	int S = ASYLUM_SCALE_FACTOR;
	uint16 fPitch = frame->surface.pitch;
	uint16 bPitch = _hdBackBuffer.pitch;

	byte *dstBase = (byte *)_hdBackBuffer.getPixels() + (destination->top * S) * bPitch + (destination->left * S);
	byte *srcBase = (byte *)frame->surface.getPixels() + (source->top * S) * fPitch + (source->left * S);
	byte *srcMirror = (byte *)frame->surface.getPixels() + (source->top * S) * fPitch + (source->right * S) - 1;

	if ((uint32)flags & 0x80000000) {
		int32 flagSet = flags & 0x7FFFFFFF;
		bool hasTransTableIndex = false;
		if (flags & 0x10000000) { flagSet = flags & 0x6FFFFFFF; hasTransTableIndex = (_transTable ? true : false); }
		bool isMirrored = (flagSet == kDrawFlagMirrorLeftRight);

		if (hasTransTableIndex) {
			if (isMirrored) blitTranstableMirrored(dstBase, srcMirror, destination->height(), destination->width(), fPitch + (destination->width() * S), bPitch - (destination->width() * S));
			else blitTranstable(dstBase, srcBase, destination->height(), destination->width(), fPitch - (destination->width() * S), bPitch - (destination->width() * S));
		} else if (flagSet) {
			if (isMirrored) {
				if (_useColorKey) blitMirroredColorKey(dstBase, srcMirror, destination->height(), destination->width(), fPitch + (destination->width() * S), bPitch - (destination->width() * S));
				else blitMirrored(dstBase, srcMirror, destination->height(), destination->width(), fPitch + (destination->width() * S), bPitch - (destination->width() * S));
			}
		} else {
			if (_useColorKey) blitRawColorKey(dstBase, srcBase, destination->height(), destination->width(), fPitch - (destination->width() * S), bPitch - (destination->width() * S));
			else blitRaw(dstBase, srcBase, destination->height(), destination->width(), fPitch - (destination->width() * S), bPitch - (destination->width() * S));
		}
	} else if (flags) {
		blt(destination, frame, source, flags);
	} else {
		bltFast(destination->left, destination->top, frame, source);
	}
}

void Screen::blitTranstable(byte *dstBuffer, byte *srcBuffer, int16 height, int16 width, uint16 srcPitch, uint16 dstPitch) const {
	int S = ASYLUM_SCALE_FACTOR; height *= S; width *= S;
	while (height--) {
		for (int16 i = width; i; --i) {
			if (*srcBuffer) *dstBuffer = _transTable[(*srcBuffer << 8) + *dstBuffer];
			dstBuffer++; srcBuffer++;
		}
		dstBuffer += dstPitch; srcBuffer += srcPitch;
	}
}

void Screen::blitTranstableMirrored(byte *dstBuffer, byte *srcBuffer, int16 height, int16 width, uint16 srcPitch, uint16 dstPitch) const {
	int S = ASYLUM_SCALE_FACTOR; height *= S; width *= S;
	while (height--) {
		for (int16 i = width; i; --i) {
			if (*srcBuffer) *dstBuffer = _transTable[(*srcBuffer << 8) + *dstBuffer];
			dstBuffer++; srcBuffer--;
		}
		dstBuffer += dstPitch; srcBuffer += srcPitch;
	}
}

void Screen::blitCrossfade(byte *dstBuffer, byte *srcBuffer, byte *objectBuffer, int16 height, int16 width, uint16 srcPitch, uint16 dstPitch, uint16 objectPitch) const {
	int S = ASYLUM_SCALE_FACTOR; height *= S; width *= S;
	while (height--) {
		for (int16 i = width; i; --i) {
			if (*srcBuffer) *dstBuffer = _transTable[(*srcBuffer << 8) + *objectBuffer];
			dstBuffer++; srcBuffer++; objectBuffer++;
		}
		dstBuffer += dstPitch; srcBuffer += srcPitch; objectBuffer += objectPitch;
	}
}

void Screen::blitMirrored(byte *dstBuffer, byte *srcBuffer, int16 height, int16 width, uint16 srcPitch, uint16 dstPitch) const {
	int S = ASYLUM_SCALE_FACTOR; height *= S; width *= S;
	while (height--) {
		for (int16 i = width; i; --i) {
			*dstBuffer = *srcBuffer; dstBuffer++; srcBuffer--;
		}
		dstBuffer += dstPitch; srcBuffer += srcPitch;
	}
}

void Screen::blitMirroredColorKey(byte *dstBuffer, byte *srcBuffer, int16 height, int16 width, uint16 srcPitch, uint16 dstPitch) const {
	int S = ASYLUM_SCALE_FACTOR; height *= S; width *= S;
	while (height--) {
		for (int16 i = width; i; --i) {
			if (*srcBuffer != 0) *dstBuffer = *srcBuffer;
			dstBuffer++; srcBuffer--;
		}
		dstBuffer += dstPitch; srcBuffer += srcPitch;
	}
}

void Screen::blitRaw(byte *dstBuffer, byte *srcBuffer, int16 height, int16 width, uint16 srcPitch, uint16 dstPitch) const {
	int S = ASYLUM_SCALE_FACTOR; height *= S; width *= S;
	while (height--) {
		memcpy(dstBuffer, srcBuffer, (uint16)width);
		dstBuffer += dstPitch; srcBuffer += srcPitch;
	}
}

void Screen::blitRawColorKey(byte *dstBuffer, byte *srcBuffer, int16 height, int16 width, uint16 srcPitch, uint16 dstPitch) const {
	int S = ASYLUM_SCALE_FACTOR; height *= S; width *= S;
	while (height--) {
		for (int16 i = width; i; --i) {
			if (*srcBuffer != 0) *dstBuffer = *srcBuffer;
			dstBuffer++; srcBuffer++;
		}
		dstBuffer += dstPitch; srcBuffer += srcPitch;
	}
}

// THE MASK SCALER (Fixes the horizontal rays completely!)
void Screen::blitMasked(GraphicFrame *frame, Common::Rect *source, byte *maskData, Common::Rect *sourceMask, Common::Rect *destMask, uint16 maskWidth, Common::Rect *destination, int32 flags) {
	int S = ASYLUM_SCALE_FACTOR;
	byte *frameBuffer = (byte *)frame->surface.getPixels();
	byte *mirroredBuffer = nullptr;
	int16 frameRight = frame->surface.pitch;

	// Use the 1x destMask boundaries to skip pixels, but write output to HD BackBuffer
	byte nSkippedBits = ABS(sourceMask->left) % 8;

	if (flags & kDrawFlagMirrorLeftRight) {
		mirroredBuffer = (byte *)malloc((size_t)((source->right * S) * (source->bottom * S)));
		blitMirrored(mirroredBuffer, frameBuffer + (source->right * S) - 1, source->bottom, source->right, (uint16)((source->right * S) + frameRight), 0);
		frameBuffer = mirroredBuffer;
		frameRight = source->right * S;
		source->right -= source->left;
		source->left = 0;
	}

	byte *frameBufferPtr = frameBuffer + (source->top * S) * frameRight + (source->left * S);
	byte *maskBufferPtr  = maskData    + sourceMask->top * (maskWidth / 8) + sourceMask->left / 8;

	if ((destMask->left + sourceMask->width()) < destination->left || (destination->left + source->width()) < destMask->left ||
	    (destMask->top + sourceMask->height()) < destination->top || (destination->top + source->height()) < destMask->top) {
		blitRawColorKey((byte *)_hdBackBuffer.getPixels() + (destination->top * S) * _hdBackBuffer.pitch + (destination->left * S), frameBufferPtr, source->height(), source->width(), frameRight - (source->width() * S), _hdBackBuffer.pitch - (source->width() * S));
		free(mirroredBuffer);
		return;
	}

	if (destination->left > destMask->left) {
		nSkippedBits += ABS(destination->left - destMask->left) % 8;
		maskBufferPtr += (destination->left - destMask->left) / 8 + nSkippedBits / 8;
		nSkippedBits %= 8;
		sourceMask->setWidth(sourceMask->width() + destMask->left - destination->left);
		frameBufferPtr += (destMask->left - destination->left) * S;
		destMask->left = destination->left;
	}

	if (destination->top > destMask->top) {
		maskBufferPtr += (destination->top - destMask->top) * maskWidth / 8;
		sourceMask->setHeight(sourceMask->height() + destMask->top - destination->top);
		frameBufferPtr += (destination->top - destMask->top) * S * frameRight;
		destMask->top = destination->top;
	}

	if (destination->left < destMask->left) {
		int pWidth = destMask->left - destination->left;
		blitRawColorKey((byte *)_hdBackBuffer.getPixels() + (destination->top * S) * _hdBackBuffer.pitch + (destination->left * S), frameBufferPtr, source->height(), pWidth, frameRight - (pWidth * S), _hdBackBuffer.pitch - (pWidth * S));
		frameBufferPtr += pWidth * S;
		source->setWidth(source->width() - pWidth);
		destination->left = destMask->left;
	}

	if ((source->width() + destination->left) > (destMask->left + sourceMask->width())) {
		int offsetW = destMask->left + sourceMask->width() - destination->left;
		int pWidth = source->width() - offsetW;
		blitRawColorKey((byte *)_hdBackBuffer.getPixels() + (destination->top * S) * _hdBackBuffer.pitch + (destMask->left + sourceMask->width()) * S, frameBufferPtr + (offsetW * S), source->height(), pWidth, frameRight - (pWidth * S), _hdBackBuffer.pitch - (pWidth * S));
		source->setWidth(offsetW);
	}

	if (destination->top < destMask->top) {
		int pHeight = destMask->top - destination->top;
		blitRawColorKey((byte *)_hdBackBuffer.getPixels() + (destination->top * S) * _hdBackBuffer.pitch + (destination->left * S), frameBufferPtr, pHeight, source->width(), frameRight - (source->width() * S), _hdBackBuffer.pitch - (source->width() * S));
		frameBufferPtr += pHeight * S * frameRight;
		source->setHeight(source->height() - pHeight);
		destination->top = destMask->top;
	}

	if ((source->height() + destination->top) > (destMask->top + sourceMask->height())) {
		int offsetH = sourceMask->height() + destMask->top - destination->top;
		int pHeight = source->height() - offsetH;
		blitRawColorKey((byte *)_hdBackBuffer.getPixels() + ((destMask->top + sourceMask->height()) * S) * _hdBackBuffer.pitch + (destination->left * S), frameBufferPtr + (offsetH * S) * frameRight, pHeight, source->width(), frameRight - (source->width() * S), _hdBackBuffer.pitch - (source->width() * S));
		source->setHeight(offsetH);
	}

	// 1x Loop boundaries, 2x writes!
	for (int16 y = 0; y < source->height(); y++) {
		byte *dstRow = (byte *)_hdBackBuffer.getPixels() + ((destination->top + y) * S * _hdBackBuffer.pitch) + (destination->left * S);
		byte *srcRow = frameBufferPtr + (y * S * frameRight);
		byte *maskPtr = maskBufferPtr + (y * (maskWidth / 8));
		
		int run = 7 - nSkippedBits;
		uint skip = *maskPtr >> nSkippedBits;

		for (int16 x = 0; x < source->width(); x++) {
			if (!(skip & 1)) {
				// Perfect mapping: Copy SxS block!
				for (int sy = 0; sy < S; sy++) {
					for (int sx = 0; sx < S; sx++) {
						byte color = srcRow[(sy * frameRight) + (x * S) + sx];
						if (color) dstRow[(sy * _hdBackBuffer.pitch) + (x * S) + sx] = color;
					}
				}
			}
			if (x == source->width() - 1) break;
			run--;
			if (run < 0) { maskPtr++; run = 7; skip = *maskPtr; }
			else skip >>= 1;
		}
	}
	free(mirroredBuffer);
}

void Screen::blt(Common::Rect *dest, GraphicFrame *frame, Common::Rect *source, int32 flags) {
	int S = ASYLUM_SCALE_FACTOR;
	if (_useColorKey) copyToBackBufferWithTransparency((byte *)frame->surface.getBasePtr(source->left * S, source->top * S), frame->surface.pitch, dest->left * S, dest->top * S, (uint16)(source->width() * S), (uint16)(source->height() * S), (bool)(flags & kDrawFlagMirrorLeftRight));
	else copyToBackBuffer((byte *)frame->surface.getBasePtr(source->left * S, source->top * S), frame->surface.pitch, dest->left * S, dest->top * S, (uint16)(source->width() * S), (uint16)(source->height() * S), (bool)(flags & kDrawFlagMirrorLeftRight));
}

void Screen::bltFast(int16 dX, int16 dY, GraphicFrame *frame, Common::Rect *source) {
	if (!frame->surface.getPixels() || source->width() == 0 || source->height() == 0) return;
	int S = ASYLUM_SCALE_FACTOR;
	Common::Rect s_src(source->left * S, source->top * S, source->right * S, source->bottom * S);
	if (_useColorKey) _hdBackBuffer.copyRectToSurfaceWithKey(frame->surface, dX * S, dY * S, s_src, 0x00);
	else _hdBackBuffer.copyRectToSurface(frame->surface, dX * S, dY * S, s_src);
}

void Screen::copyToBackBuffer(const byte *buffer, int32 pitch, int16 x, int16 y, uint16 width, uint16 height, bool mirrored) {
	if (!buffer || width == 0 || height == 0) return;
	if (!mirrored) _hdBackBuffer.copyRectToSurface(buffer, pitch, x, y, width, height);
}

void Screen::copyToBackBufferWithTransparency(byte *buffer, int32 pitch, int16 x, int16 y, uint16 width, uint16 height, bool mirrored) {
	byte *dest = (byte *)_hdBackBuffer.getPixels();
	int32 left = (x < 0) ? -x : 0;
	int32 top = (y < 0) ? -y : 0;
	int32 right = (x + width > _hdBackBuffer.w) ? _hdBackBuffer.w - abs(x) : width;
	int32 bottom = (y + height > _hdBackBuffer.h) ? _hdBackBuffer.h - abs(y) : height;

	for (int32 curY = top; curY < bottom; curY++) {
		for (int32 curX = left; curX < right; curX++) {
			uint32 offset = (uint32)((mirrored ? right - (curX + 1) : curX) + curY * pitch);
			if (buffer[offset] != 0) dest[x + curX + (y + curY) * _hdBackBuffer.pitch] = buffer[offset];
		}
	}
}

// ... PASTE REMAINDER OF SCREEN.CPP HERE ...
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

	// Fetch HD crossfade buffers!
	Graphics::Surface origFrame = frame->surface;
	Graphics::Surface origObj = frameObject->surface;
	Graphics::Surface *hdFrame = getHDSurface(resourceId, frameIndex);
	Graphics::Surface *hdObj = getHDSurface(objectResourceId, 0);

	if (hdFrame) frame->surface = *hdFrame;
	if (hdObj) frameObject->surface = *hdObj;

	int scale = ASYLUM_SCALE_FACTOR;
	Common::Point s_point(point.x * scale, point.y * scale);
	Common::Point s_dest(destination.x * scale, destination.y * scale);

	Common::Rect src(0, 0, frame->getWidth(), frame->getHeight());
	Common::Rect dst = src;
	
	dst.translate(s_point.x + (frame->x * scale), s_point.y + (frame->y * scale));

	clip(&src, &dst, 0);
	if (src.isValidRect()) {
		_useColorKey = true;
		blitCrossfade((byte *)_hdBackBuffer.getPixels()          + dst.top                   * _hdBackBuffer.pitch          + dst.left,
		              (byte *)frame->surface.getPixels()       + src.top                   * frame->surface.pitch       + src.left,
		              (byte *)frameObject->surface.getPixels() + (s_dest.y + dst.top) * frameObject->surface.pitch + (dst.left + s_dest.x),
		              dst.height(),
		              dst.width(),
		              (uint16)(frame->surface.pitch       - dst.width()),
		              (uint16)(_hdBackBuffer.pitch          - dst.width()),
		              (uint16)(frameObject->surface.pitch - dst.width()));
	}

	if (hdFrame) frame->surface = origFrame;
	if (hdObj) frameObject->surface = origObj;

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
	int scale = ASYLUM_SCALE_FACTOR;
	int sx = x * scale;
	int sy = y * scale;

	Common::Rect screenRect(getWorld()->xLeft * scale, getWorld()->yTop * scale, (getWorld()->xLeft * scale) + ASYLUM_SCREEN_WIDTH, (getWorld()->yTop * scale) + ASYLUM_SCREEN_HEIGHT);
	Common::Rect animRect(sx, sy, sx + (int16)surface->w, sy + (int16)surface->h);
	animRect.clip(screenRect);

	if (!animRect.isEmpty()) {
		animRect.translate(-(int16)(getWorld()->xLeft * scale), -(int16)(getWorld()->yTop * scale));

		int startX = animRect.right  == ASYLUM_SCREEN_WIDTH ? 0 : surface->w - animRect.width();
		int startY = animRect.bottom == ASYLUM_SCREEN_HEIGHT ? 0 : surface->h - animRect.height();

		if (surface->w > ASYLUM_SCREEN_WIDTH)
			startX = getWorld()->xLeft * scale;
		if (surface->h > ASYLUM_SCREEN_HEIGHT)
			startY = getWorld()->yTop * scale;

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
