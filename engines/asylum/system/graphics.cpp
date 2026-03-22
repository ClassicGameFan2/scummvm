/* ScummVM - Graphic Adventure Engine
 *
 * ScummVM is the legal property of its developers, whose names
 * are too numerous to list here. Please refer to the COPYRIGHT
 * file distributed with this source distribution.
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.

 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.

 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 *
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
#include "common/fs.h"
#include "common/config-manager.h"
#include "common/hashmap.h"
// ----------------------------

namespace Asylum {

// --- GLOBAL HD DICTIONARY (THE VAULT) ---
// This vault is completely hidden from the 1x Game Brain!
static Common::HashMap<uint32, Graphics::Surface *> g_hdSurfaces;

Graphics::Surface *getHDSurface(uint32 resourceId, uint32 frameIndex) {
	uint32 key = (resourceId << 8) | (frameIndex & 0xFF);
	if (g_hdSurfaces.contains(key)) return g_hdSurfaces[key];
	return nullptr;
}
// ----------------------------------------

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
	// This only frees the 1x image from the engine's active memory. 
	// The HD image remains safe in the Vault!
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

	// --- HD CONFIGURATION ---
	int scaleFactor = 1;
	if (ConfMan.hasKey("InternalUpscalingFactor")) {
		scaleFactor = ConfMan.getInt("InternalUpscalingFactor");
		if (scaleFactor < 1) scaleFactor = 1; 
	}
	bool doReplace = ConfMan.hasKey("Asset_HD_Replace") ? ConfMan.getInt("Asset_HD_Replace") != 0 : false;
	uint32 packId = (_resourceId >> 16) & 0xFF;

	// SMART I/O OPTIMIZER: Only check the hard drive if we know this object has modded frames!
	// This prevents the engine from scanning the HDD 2000+ times when a menu opens, killing the lag completely.
	bool isModdedObject = false;
	if (doReplace) {
		Common::String checkName = Common::String::format("SanitariumHDPack/RES%03d/obj_%u_f0.png", packId, _resourceId);
		Common::Path checkPath(checkName);
		if (Common::FSNode(checkPath).exists()) {
			isModdedObject = true;
		}
	}

	dataPtr = data;

	for (uint32 i = 0; i < frameCount; i++) {
		dataPtr = data + _frames[i].offset;
		dataPtr += 8; 

		_frames[i].x  = (int16)READ_LE_UINT16(dataPtr); dataPtr += 2;
		_frames[i].y  = (int16)READ_LE_UINT16(dataPtr); dataPtr += 2;
		uint16 height = READ_LE_UINT16(dataPtr); dataPtr += 2;
		uint16 width  = READ_LE_UINT16(dataPtr); dataPtr += 2;

		if (width > 0 && height > 0) {
			
			// 1. ALWAYS GIVE THE 1X GAME BRAIN THE ORIGINAL VANILLA DATA
			// This guarantees perfect pixel collision for Max, even if you are playing at 4x with Waifu PNGs!
			_frames[i].surface.create(width, height, Graphics::PixelFormat::createFormatCLUT8());
			_frames[i].surface.copyRectToSurface(dataPtr, width, 0, 0, width, height);

			// 2. HANDLE HD REPLACEMENT & VAULT BUILDING
			if (scaleFactor > 1 || doReplace) {
				uint32 key = (_resourceId << 8) | (i & 0xFF);
				
				if (!g_hdSurfaces.contains(key)) {
					bool replaced = false;
					int expectedW = width * scaleFactor;
					int expectedH = height * scaleFactor;

					// Check for Waifu2x HD Replacement
					if (isModdedObject) {
						Common::String hdName = Common::String::format("SanitariumHDPack/RES%03d/obj_%u_f%d.png", packId, _resourceId, i);
						Common::Path hdPath(hdName);
						Common::FSNode hdNode(hdPath);
						
						if (hdNode.exists()) {
							Common::File f;
							if (f.open(hdNode)) {
								Image::PNGDecoder decoder;
								if (decoder.loadStream(f)) {
									const Graphics::Surface *decSurf = decoder.getSurface();
									if (decSurf->format.bytesPerPixel == 1) { 
										
										// If playing at 1x, replace the Game Brain's vanilla surface with the 1x Mod
										if (scaleFactor == 1) {
											if (decSurf->w == width && decSurf->h == height) {
												_frames[i].surface.copyFrom(*decSurf);
											}
											replaced = true;
										} 
										// If playing at 2x+, put the PNG strictly in the HD Vault! (Game Brain stays vanilla)
										else {
											Graphics::Surface *hdSurf = new Graphics::Surface();
											hdSurf->create(expectedW, expectedH, Graphics::PixelFormat::createFormatCLUT8());
											
											// Safely scale loaded PNG to expected bounds to prevent crashes
											const byte *srcPx = (const byte *)decSurf->getPixels();
											byte *dstPx = (byte *)hdSurf->getPixels();
											float sX = (float)decSurf->w / expectedW;
											float sY = (float)decSurf->h / expectedH;
											
											for (int sy = 0; sy < expectedH; sy++) {
												int srcY = (int)(sy * sY);
												if (srcY >= decSurf->h) srcY = decSurf->h - 1;
												const byte *srcRow = srcPx + (srcY * decSurf->pitch);
												byte *dstRow = dstPx + (sy * hdSurf->pitch);
												for (int sx = 0; sx < expectedW; sx++) {
													int srcX = (int)(sx * sX);
													if (srcX >= decSurf->w) srcX = decSurf->w - 1;
													dstRow[sx] = srcRow[srcX];
												}
											}
											g_hdSurfaces[key] = hdSurf;
											replaced = true;
										}
									}
								}
							}
						}
					}

					// Nearest-Neighbor Fallback
					if (!replaced && scaleFactor > 1) {
						Graphics::Surface *hdSurf = new Graphics::Surface();
						hdSurf->create(expectedW, expectedH, Graphics::PixelFormat::createFormatCLUT8());
						const byte *srcPx = (const byte *)_frames[i].surface.getPixels();
						byte *dstPx = (byte *)hdSurf->getPixels();
						for (int sy = 0; sy < expectedH; sy++) {
							const byte *srcRow = srcPx + ((sy / scaleFactor) * _frames[i].surface.pitch);
							byte *dstRow = dstPx + (sy * hdSurf->pitch);
							for (int sx = 0; sx < expectedW; sx++) {
								dstRow[sx] = srcRow[sx / scaleFactor];
							}
						}
						g_hdSurfaces[key] = hdSurf;
					}
				}
			}
		}
	}
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
