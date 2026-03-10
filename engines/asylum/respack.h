/* ScummVM - Graphic Adventure Engine
 * (Copyright headers...)
 */

#ifndef ASYLUM_RESPACK_H
#define ASYLUM_RESPACK_H

#include "common/array.h"
#include "common/file.h"
#include "common/hashmap.h"

#include "asylum/asylum.h"
#include "asylum/shared.h"

namespace Asylum {

class ResourceManager;

struct ResourceEntry {
	byte   *data;
	uint32  size;
	uint32  offset;

	ResourceEntry() {
		data = NULL;
		size = 0;
		offset = 0;
	}

	uint32 getData(uint32 off) {
		if (data == NULL)
			error("[ResourceEntry::getData] Invalid data");

		return READ_LE_UINT32(data + off);
	}
};

class ResourcePack {
public:
	ResourceEntry *get(uint16 index);
	
	// --- HD REMASTER HOOK ---
	void dumpToPNG(ResourcePackId packId, AsylumEngine *vm);
	// ------------------------

protected:
	ResourcePack(const Common::Path &filename);
	~ResourcePack();

private:
	Common::Array<ResourceEntry> _resources;
	Common::File _packFile;

	void init(const Common::Path &filename);

	friend class ResourceManager;
};

class ResourceManager {
public:
	ResourceManager(AsylumEngine *vm);
	~ResourceManager();

	/**
	 * Get a resource entry
	 *
	 * @param id The ResourceId to get.
	 *
	 * @return the resource entry
	 */
	ResourceEntry *get(ResourceId id);

	/**
	 * Unloads the resources associated with the id
	 *
	 * @param id The identifier.
	 */
	void unload(ResourcePackId id);

	int getCdNumber() { return _cdNumber; }
	void setCdNumber(int cdNumber) { _cdNumber = cdNumber; }
	void setMusicPackId(ResourcePackId id) { _musicPackId = id; }
	void clearSharedSoundCache() { _resources.erase(kResourcePackSharedSound); }
	void clearMusicCache() { _music.erase(kResourcePackMusic); }

private:
	struct ResourcePackId_EqualTo {
		bool operator()(const ResourcePackId &x, const ResourcePackId &y) const { return x == y; }
	};

	struct ResourcePackId_Hash {
		uint operator()(const ResourcePackId &x) const { return x; }
	};

	typedef Common::HashMap<ResourcePackId, ResourcePack *, ResourcePackId_Hash, ResourcePackId_EqualTo> ResourceCache;

	ResourceCache _resources;
	ResourceCache _music;

	int            _cdNumber;
	ResourcePackId _musicPackId;
	AsylumEngine  *_vm;
};

} // end of namespace Asylum

#endif // ASYLUM_RESPACK_H
