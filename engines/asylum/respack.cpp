/* ScummVM - Graphic Adventure Engine
 * (Copyright headers...)
 */

#include "asylum/respack.h"

// --- HD REMASTER INCLUDES ---
#undef PALETTE_SIZE
#include "image/png.h"
#include "common/file.h"
#include "common/path.h"
#include "common/fs.h"
#include "common/config-manager.h"
#include "asylum/system/graphics.h"
#include "asylum/system/screen.h"
// ----------------------------

namespace Asylum {

const struct {
	int  cdNumber;
	uint resourceId;
	uint size;
} patchedSizes[] = {
	{1, 0x800402B0, 40146626}, {3, 0x800403EB, 18177962}, {2, 0x8004071D, 40501676},
	{1, 0x8004072D,  7349518}, {2, 0x80040733, 40367314}, {1, 0x8004073B,  5534658},
	{2, 0x8004073C, 40347616}, {1, 0x80040745,  4333670}, {2, 0x80040746, 40214368},
	{3, 0x8004074A, 17247084}, {2, 0x8004074C, 40072902}, {3, 0x80040756, 15741212},
	{2, 0x8004075E, 39099030}, {1, 0x8004076E,  1122128}, {2, 0x80040781, 36131104},
	{3, 0x80040782, 15468752}, {2, 0x80040783, 36119940}, {1, 0x80040786,   755152},
	{2, 0x800408B9, 18430980}, {3, 0x8004093A,  6679208}, {1, 0x8004093D,   383318},
	{3, 0x80040942,  4502532}, {2, 0x80040968,  3920338}, {3, 0x80040970,   654212},
	{2, 0x8004097D,   524576}, {1, 0x8004097F,    52574}, {2, 0x80040983,   289832},
};

//////////////////////////////////////////////////////////////////////////
// ResourceManager
//////////////////////////////////////////////////////////////////////////

ResourceManager::ResourceManager(AsylumEngine *vm) : _cdNumber(-1), _musicPackId(kResourcePackInvalid), _vm(vm) {
}

ResourceManager::~ResourceManager() {
	for (const auto &resource : _resources)
		delete resource._value;
	for (const auto &music : _music)
		delete music._value;
}

ResourceEntry *ResourceManager::get(ResourceId id) {
	ResourcePackId packId = RESOURCE_PACK(id);
	uint16 index = RESOURCE_INDEX(id);

	bool isMusicPack = (packId == kResourcePackMusic);

	if (isMusicPack && _musicPackId == kResourcePackInvalid)
		error("[ResourceManager::get] Current music pack Id has not been set!");

	ResourceCache *cache = isMusicPack ? &_music : &_resources;

	if (!cache->contains(packId)) {
		ResourcePack *pack;

		if (isMusicPack) {
			if (_vm->checkGameVersion("Demo"))
				pack = new ResourcePack("res.002");
			else
				pack = new ResourcePack(Common::Path(Common::String::format("mus.%03d", _musicPackId)));
		} else {
			if (packId == kResourcePackSharedSound) {
				if (_vm->checkGameVersion("Demo")) {
					pack = new ResourcePack("res.004");
					cache->setVal(packId, pack);
					return cache->getVal(packId)->get(index);
				}

				if (_cdNumber == -1)
					error("[ResourceManager::get] Cd number has not been set!");

				pack = new ResourcePack(Common::Path(Common::String::format("res.%01d%02d", _cdNumber, packId)));

				if (pack->_packFile.size() == 299872422)
					for (int i = 0; i < ARRAYSIZE(patchedSizes); i++)
						if (_cdNumber == patchedSizes[i].cdNumber)
							pack->_resources[RESOURCE_INDEX(patchedSizes[i].resourceId)].size = patchedSizes[i].size;
			} else {
				pack = new ResourcePack(Common::Path(Common::String::format("res.%03d", packId)));
			}
		}

		cache->setVal(packId, pack);

		// --- HD AUTO-DUMPER HOOK ---
		if (!isMusicPack && ConfMan.hasKey("Asset_Dump") && ConfMan.getInt("Asset_Dump") != 0) {
			pack->dumpToPNG(packId, _vm);
		}
		// ---------------------------
	}

	return cache->getVal(packId)->get(index);
}

void ResourceManager::unload(ResourcePackId id) {
	if (_resources.contains(id)) {
		delete _resources.getVal(id);
		_resources.erase(id);
	}

	if (_music.contains(id)) {
		delete _music.getVal(id);
		_music.erase(id);
	}
}

//////////////////////////////////////////////////////////////////////////
// ResourcePack
//////////////////////////////////////////////////////////////////////////
ResourcePack::ResourcePack(const Common::Path &filename) {
	init(filename);
}

ResourcePack::~ResourcePack() {
	for (uint32 i = 0; i < _resources.size(); i++)
		delete [] _resources[i].data;

	_resources.clear();
	_packFile.close();
}

void ResourcePack::init(const Common::Path &filename) {
	if (!_packFile.open(filename))
		error("[ResourcePack::init] Could not open resource file: %s", filename.toString(Common::Path::kNativeSeparator).c_str());

	uint32 entryCount = _packFile.readUint32LE();
	_resources.resize(entryCount);

	uint32 prevOffset = _packFile.readUint32LE();
	uint32 nextOffset = 0;

	for (uint32 i = 0; i < entryCount; i++) {
		ResourceEntry entry;
		entry.offset = prevOffset;

		nextOffset = (i < entryCount - 1) ? _packFile.readUint32LE() : (uint32)_packFile.size();
		entry.size = (nextOffset > 0) ? nextOffset - prevOffset : (uint32)_packFile.size() - prevOffset;
		entry.data = nullptr;

		_resources[i] = entry;

		prevOffset = nextOffset;
	}
}

ResourceEntry *ResourcePack::get(uint16 index) {
	if (index > _resources.size() - 1)
		return nullptr;

	if (!_resources[index].data) {
		_packFile.seek(_resources[index].offset, SEEK_SET);
		_resources[index].data = new byte[_resources[index].size];
		_packFile.read(_resources[index].data, _resources[index].size);
	}

	return &_resources[index];
}


// =====================================================================
// AUTO-MASS-DUMPER
// =====================================================================
void ResourcePack::dumpToPNG(ResourcePackId packId, AsylumEngine *vm) {
	Common::String dirName = Common::String::format("SanitariumDump/RES%03d", (int)packId);
	Common::String markerName = dirName + "/dump_complete.txt";

	// 1. HARD DRIVE SHIELD
	Common::Path markerPath(markerName);
	Common::FSNode markerNode(markerPath);
	if (markerNode.exists()) {
		return; 
	}

	warning("--- AUTO-MASS-DUMP INITIATED FOR PACK %d ---", packId);

	// 2. PALETTE SCANNER & TRUE VGA BASELINE
	byte localPalette[768];
	bool hasLocalPalette = false;

	for (uint32 i = 0; i < _resources.size(); i++) {
		if (_resources[i].size == 800) {
			byte *tempData = new byte[800];
			_packFile.seek(_resources[i].offset, SEEK_SET);
			_packFile.read(tempData, 800);
			
			if (!strncmp((char *)tempData, "D3GR", 4)) {
				byte *rawData = tempData + 32;
				byte *target = localPalette;
				
				// EXACT 1998 VGA BASELINE CONVERSION
				// The raw palette is 6-bit (0-63). To convert to standard 8-bit (0-255), 
				// the engine simply multiplies each value by 4. 
				// This perfectly matches the scene.cpp baseline without artificial blowing out!
				for (int p = 0; p < 256; p++) {
					target[0] = (byte)(rawData[0] * 4);
					target[1] = (byte)(rawData[1] * 4);
					target[2] = (byte)(rawData[2] * 4);
					target += 3;
					rawData += 3;
				}
				
				hasLocalPalette = true;
				delete[] tempData;
				warning("-> Found and applied True VGA Baseline palette for pack %d!", packId);
				break;
			}
			delete[] tempData;
		}
	}

	// 3. MASS EXTRACTION
	for (uint32 i = 0; i < _resources.size(); i++) {
		if (_resources[i].size > 32 && _resources[i].size != 800) {
			
			_packFile.seek(_resources[i].offset, SEEK_SET);
			char magic[5] = {0};
			_packFile.read(magic, 4);

			if (!strncmp(magic, "D3GR", 4)) {
				ResourceId resId = (ResourceId)((((packId) << 16) + 0x80000000) + i);
				
				GraphicResource *res = new GraphicResource(vm, resId);
				if (res && res->count() > 0 && res->count() < 2000) {
					for (uint32 f = 0; f < res->count(); f++) {
						GraphicFrame *frame = res->getFrame(f);
						
						if (frame && frame->surface.getPixels() && frame->surface.format.bytesPerPixel == 1) {
							Common::String fileName = Common::String::format("%s/obj_%u_f%d.png", dirName.c_str(), resId, f);
							Common::Path filePath(fileName);

							if (!Common::File::exists(filePath)) {
								Common::DumpFile out;
								if (out.open(filePath, true)) {
									const byte *pal = hasLocalPalette ? localPalette : vm->screen()->getPalette();
									Image::writePNG(out, frame->surface, pal);
									out.close();
								}
							}
						}
					}
				}
				delete res;

				if (_resources[i].data) {
					delete[] _resources[i].data;
					_resources[i].data = nullptr;
				}
			}
		}

		Common::Event ev;
		while (vm->getEventManager()->pollEvent(ev)) {}
	}

	// 4. WRITE THE MARKER FILE
	Common::DumpFile marker;
	if (marker.open(Common::Path(markerName), true)) {
		marker.write("DUMP COMPLETE", 13);
		marker.close();
	}
	warning("--- MASS DUMP COMPLETE FOR PACK %d ---", packId);
}
// =====================================================================

} // end of namespace Asylum
