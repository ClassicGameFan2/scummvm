/* ScummVM - Graphic Adventure Engine
 * (Copyright headers...)
 */

#include "asylum/system/graphics.h"
#include "asylum/asylum.h"
#include "asylum/respack.h"
#include "asylum/system/screen.h"

// --- HD REMASTER INCLUDES ---
#undef PALETTE_SIZE
#include "image/png.h"
#include "common/file.h"
#include "common/path.h"
#include "common/config-manager.h"
#include "common/hashmap.h"
#include "common/array.h"
// ----------------------------

namespace Asylum {

// --- GLOBAL HD MEMORY CACHE ---
struct CachedHDFrame {
	Graphics::Surface *surf;
	int16 x;
	int16 y;
};
struct CachedHDResource {
	uint16 maxWidth;
	Common::Array<CachedHDFrame> frames;
};

static Common::HashMap<uint32, CachedHDResource> *g_hdCache = nullptr;
// ------------------------------

GraphicResource::GraphicResource(AsylumEngine *engine) : _vm(engine), _resourceId(kResourceNone) {
}

GraphicResource::GraphicResource(AsylumEngine *engine, ResourceId id) : _vm(engine), _resourceId(kResourceNone) {
	if (!load(id))
		error("[GraphicResource::GraphicResource] Error loading resource (0x%X)", id);
}

GraphicResource::~GraphicResource() {
	clear();
}

bool GraphicResource::load(ResourceId id) {
	if (id == kResourceNone)
		error("[GraphicResource::load] Trying to load an invalid resource!");

	clear();

	ResourceEntry *resEntry = getResource()->get(id);
	if (!resEntry)
		return false;

	_resourceId = id;
	init(resEntry->data, resEntry->size);

	return true;
}

void GraphicResource::clear() {
	for (uint32 i = 0; i < _frames.size(); i++) {
		_frames[i].surface.free();
	}
	_frames.clear();
}

GraphicFrame *GraphicResource::getFrame(uint32 frame) {
	if (frame >= _frames.size())
		error("[GraphicResource::getFrame] Invalid frame index (was: %d, max:%d)", frame, _frames.size() - 1);
	return &_frames[frame];
}

void GraphicResource::init(byte *data, int32 size) {
	byte *dataPtr = data;

	for (uint i = 0; i < sizeof(_data.tag); i++) {
		_data.tag[i] = *dataPtr;
		++dataPtr;
	}

	_data.flags  = READ_LE_UINT32(dataPtr); dataPtr += 4;
	int32 contentOffset = (int32)READ_LE_UINT32(dataPtr); dataPtr += 4;
	_data.field_C  = READ_LE_UINT32(dataPtr); dataPtr += 4;
	_data.field_10 = READ_LE_UINT32(dataPtr); dataPtr += 4;
	_data.field_14 = READ_LE_UINT32(dataPtr); dataPtr += 4;
	uint16 frameCount = READ_LE_UINT16(dataPtr); dataPtr += 2;
	_data.maxWidth = READ_LE_UINT16(dataPtr); dataPtr += 2;

	_frames.resize(frameCount);

	int32 prevOffset = (int32)READ_LE_UINT32(dataPtr) + contentOffset; dataPtr += 4;
	int32 nextOffset = 0;

	for (int32 i = 0; i < frameCount; i++) {
		GraphicFrame frame;
		frame.offset = prevOffset;
		nextOffset = (i < frameCount - 1) ? (int32)READ_LE_UINT32(dataPtr) + contentOffset : size;
		dataPtr += 4; 
		frame.size = (nextOffset > 0) ? nextOffset - prevOffset : size - prevOffset;
		_frames[i] = frame;
		prevOffset = nextOffset;
	}

	if (!g_hdCache) g_hdCache = new Common::HashMap<uint32, CachedHDResource>();

	// =====================================================================
	// CACHE LOOKUP: Deep Copy from the Vault
	// =====================================================================
	if (g_hdCache->contains(_resourceId)) {
		CachedHDResource &hd = (*g_hdCache)[_resourceId];
		_data.maxWidth = hd.maxWidth;
		
		for (uint32 i = 0; i < frameCount; i++) {
			_frames[i].x = hd.frames[i].x;
			_frames[i].y = hd.frames[i].y;
			
			if (hd.frames[i].surf->getPixels()) {
				_frames[i].surface.create(hd.frames[i].surf->w, hd.frames[i].surf->h, hd.frames[i].surf->format);
				_frames[i].surface.copyFrom(*hd.frames[i].surf);
			}
		}
		return; 
	}
	// =====================================================================

	// --- HD CONFIGURATION ---
	int scaleFactor = 1;
	if (ConfMan.hasKey("InternalUpscalingFactor")) {
		scaleFactor = ConfMan.getInt("InternalUpscalingFactor");
		if (scaleFactor < 1) scaleFactor = 1; 
	}
	bool doReplace = ConfMan.hasKey("Asset_HD_Replace") ? ConfMan.getInt("Asset_HD_Replace") != 0 : false;
	uint32 packId = (_resourceId >> 16) & 0xFF;

	CachedHDResource newCacheEntry;
	dataPtr = data;

	for (uint32 i = 0; i < frameCount; i++) {
		dataPtr = data + _frames[i].offset;
		dataPtr += 8; 

		_frames[i].x  = (int16)READ_LE_UINT16(dataPtr); dataPtr += 2;
		_frames[i].y  = (int16)READ_LE_UINT16(dataPtr); dataPtr += 2;
		uint16 height = READ_LE_UINT16(dataPtr); dataPtr += 2;
		uint16 width  = READ_LE_UINT16(dataPtr); dataPtr += 2;

		CachedHDFrame cacheFrame;
		cacheFrame.surf = new Graphics::Surface();
		cacheFrame.x = _frames[i].x;
		cacheFrame.y = _frames[i].y;

		if (width > 0 && height > 0) {
			Graphics::Surface origSurf;
			origSurf.create(width, height, Graphics::PixelFormat::createFormatCLUT8());
			origSurf.copyRectToSurface(dataPtr, width, 0, 0, width, height);

			bool replaced = false;
			int appliedScale = 1;

			// CHECK FOR HD REPLACEMENT
			if (doReplace) {
				Common::String hdName = Common::String::format("SanitariumHDPack/RES%03d/obj_%u_f%d.png", packId, _resourceId, i);
				Common::Path hdPath(hdName);
				
				if (Common::File::exists(hdPath)) {
					Common::File f;
					if (f.open(hdPath)) {
						Image::PNGDecoder decoder;
						if (decoder.loadStream(f)) {
							const Graphics::Surface *decSurf = decoder.getSurface();
							if (decSurf->format.bytesPerPixel == 1) {
								cacheFrame.surf->create(decSurf->w, decSurf->h, Graphics::PixelFormat::createFormatCLUT8());
								cacheFrame.surf->copyFrom(*decSurf);
								replaced = true;
								appliedScale = decSurf->w / width;
								if (appliedScale < 1) appliedScale = 1;
							} else {
								warning("[HD INJECTOR] Failed: %s is not 8-bit Indexed!", hdName.c_str());
							}
						}
					}
				}
			}

			// INTERNAL NEAREST-NEIGHBOR FALLBACK
			if (!replaced && scaleFactor > 1) {
				int scaledW = width * scaleFactor;
				int scaledH = height * scaleFactor;
				cacheFrame.surf->create(scaledW, scaledH, Graphics::PixelFormat::createFormatCLUT8());

				const byte *srcPx = (const byte *)origSurf.getPixels();
				byte *dstPx = (byte *)cacheFrame.surf->getPixels();

				for (int sy = 0; sy < scaledH; sy++) {
					const byte *srcRow = srcPx + ((sy / scaleFactor) * origSurf.pitch);
					byte *dstRow = dstPx + (sy * cacheFrame.surf->pitch);
					for (int sx = 0; sx < scaledW; sx++) {
						dstRow[sx] = srcRow[sx / scaleFactor];
					}
				}
				replaced = true;
				appliedScale = scaleFactor;
			}

			// FINALIZE
			if (!replaced) {
				cacheFrame.surf->create(width, height, origSurf.format);
				cacheFrame.surf->copyFrom(origSurf);
			} else {
				cacheFrame.x *= appliedScale;
				cacheFrame.y *= appliedScale;
			}

			_frames[i].x = cacheFrame.x;
			_frames[i].y = cacheFrame.y;
			_frames[i].surface.create(cacheFrame.surf->w, cacheFrame.surf->h, cacheFrame.surf->format);
			_frames[i].surface.copyFrom(*cacheFrame.surf);

			origSurf.free();
		}
		newCacheEntry.frames.push_back(cacheFrame);
	}

	newCacheEntry.maxWidth = _data.maxWidth * scaleFactor;
	_data.maxWidth = newCacheEntry.maxWidth;

	(*g_hdCache)[_resourceId] = newCacheEntry;
}

uint32 GraphicResource::getFrameCount(AsylumEngine *engine, ResourceId id) {
	GraphicResource *resource = new GraphicResource(engine, id);
	uint32 frameCount = resource->count();
	delete resource;
	return frameCount;
}

Common::Rect GraphicResource::getFrameRect(AsylumEngine *engine, ResourceId id, uint32 index) {
	GraphicResource *resource = new GraphicResource(engine, id);
	GraphicFrame *frame = resource->getFrame(index);
	Common::Rect rect = frame->getRect();
	delete resource;
	return rect;
}

} // end of namespace Asylum
