/* ScummVM - Graphic Adventure Engine
 * (Copyright headers...)
 */

#include "asylum/resources/polygons.h"

// --- HD REMASTER INCLUDES ---
#include "common/config-manager.h"
// ----------------------------

namespace Asylum {

//////////////////////////////////////////////////////////////////////////
// Contains
//////////////////////////////////////////////////////////////////////////
bool Polygon::contains(const Common::Point &point) {
	bool  yflag0;
	bool  yflag1;
	bool inside_flag = false;

	if (points.size() == 0)
		return false;

	Common::Point *vtx0 = &points[count() - 1];
	Common::Point *vtx1 = &points[0];

	yflag0 = (vtx0->y > point.y);
	for (uint32 pt = 0; pt < count(); pt++, vtx1++) {
		if (point == *vtx1)
			return true;

		yflag1 = (vtx1->y > point.y);
		if (yflag0 != yflag1) {
			if (((vtx1->y - point.y) * (vtx0->x - vtx1->x) > (vtx1->x - point.x) * (vtx0->y - vtx1->y)) == yflag1) {
				inside_flag = !inside_flag;
			}
		}
		yflag0 = yflag1;
		vtx0   = vtx1;
	}

	return inside_flag;
}

//////////////////////////////////////////////////////////////////////////
// Polygons
//////////////////////////////////////////////////////////////////////////
Polygons::Polygons(Common::SeekableReadStream *stream) : _size(0), _numEntries(0) {
	load(stream);
}

Polygons::~Polygons() {
	_entries.clear();
}

Polygon Polygons::get(uint32 index) {
	if (index >= _entries.size())
		error("[Polygons::getEntry] Invalid polygon index (was: %d, max: %d)", index, _entries.size() - 1);

	return _entries[index];
}

void Polygons::load(Common::SeekableReadStream *stream) {
	_size       = stream->readSint32LE();
	_numEntries = stream->readSint32LE();

	for (int32 g = 0; g < _numEntries; g++) {
		Polygon poly;

		uint32 numPoints = stream->readUint32LE();

		for (uint32 i = 0; i < numPoints; i++) {
			Common::Point point;
			point.x = (int16)(stream->readSint32LE() & 0xFFFF);
			point.y = (int16)(stream->readSint32LE() & 0xFFFF);

			poly.points.push_back(point);
		}

		stream->skip((MAX_POLYGONS - numPoints) * 8);

		poly.boundingRect.left   = (int16)(stream->readSint32LE() & 0xFFFF);
		poly.boundingRect.top    = (int16)(stream->readSint32LE() & 0xFFFF);
		poly.boundingRect.right  = (int16)(stream->readSint32LE() & 0xFFFF);
		poly.boundingRect.bottom = (int16)(stream->readSint32LE() & 0xFFFF);

		_entries.push_back(poly);
	}

	// --- HD REMASTER: SCALE ALL POLYGONS ---
	int scaleFactor = 1;
	if (ConfMan.hasKey("InternalUpscalingFactor")) {
		scaleFactor = ConfMan.getInt("InternalUpscalingFactor");
		if (scaleFactor < 1) scaleFactor = 1;
	}

	if (scaleFactor > 1) {
		for (uint32 i = 0; i < _entries.size(); i++) {
			// Scale individual collision points
			for (uint32 pt = 0; pt < _entries[i].points.size(); pt++) {
				_entries[i].points[pt].x *= scaleFactor;
				_entries[i].points[pt].y *= scaleFactor;
			}
			// Scale the bounding box of the polygon
			_entries[i].boundingRect.left *= scaleFactor;
			_entries[i].boundingRect.top *= scaleFactor;
			_entries[i].boundingRect.right *= scaleFactor;
			_entries[i].boundingRect.bottom *= scaleFactor;
		}
	}
	// ---------------------------------------
}

} // end of namespace Asylum
