/* ScummVM - Graphic Adventure Engine
 * (Copyright headers...)
 */

#include "backends/keymapper/action.h"
#include "backends/keymapper/hardware-input.h"
#include "backends/keymapper/keymap.h"
#include "backends/keymapper/keymapper.h"
#include "common/keyboard.h"
#include "graphics/palette.h"

#include "asylum/views/menu.h"
#include "asylum/resources/actor.h"
#include "asylum/resources/worldstats.h"
#include "asylum/system/cursor.h"
#include "asylum/system/graphics.h"
#include "asylum/system/savegame.h"
#include "asylum/system/screen.h"
#include "asylum/system/text.h"
#include "asylum/views/scene.h"
#include "asylum/views/video.h"
#include "asylum/respack.h"
#include "asylum/staticres.h"
#include "asylum/asylum.h"

// --- HD REMASTER SCALING ---
#include "common/config-manager.h"
namespace Asylum {
static int getScaleFactor() {
	if (ConfMan.hasKey("InternalUpscalingFactor")) {
		int sf = ConfMan.getInt("InternalUpscalingFactor");
		return sf > 1 ? sf : 1;
	}
	return 1;
}
#define LOGICAL_MOUSE(pt) Common::Point((pt).x / getScaleFactor(), (pt).y / getScaleFactor())
// ---------------------------

Menu::Menu(AsylumEngine *vm): _vm(vm) {
	_initGame = false;
	_activeScreen   = kMenuNone;
	_soundResourceId = kResourceNone;
	_musicResourceId = kResourceNone;
	_gameStarted = false;
	_currentIcon = kMenuNone;
	_selectedShortcutIndex = -1;
	_dword_455C74 = 0;
	_dword_455C78 = false;
	_dword_455C80 = false;
	_dword_455D4C = false;
	_dword_455D5C = false;
	_isEditingSavegameName = false;
	_testSoundsPlaying = false;
	_dword_456288 = 0;
	_caretBlink = 0;
	_startIndex = 0;
	_showMovie = false;
	memset(&_iconFrames, 0, sizeof(_iconFrames));

	_movieCount = 0;
	_movieIndex = 0;
	memset(&_movieList, 0 , sizeof(_movieList));

	_prefixWidth = 0;
	_loadingDuringStartup = false;
	_thumbnailIndex = -1;
	_creditsFrameIndex = 0;
	switch (_vm->getLanguage()) {
	default:
	case Common::EN_ANY: _creditsNumSteps = 8688; break;
	case Common::DE_DEU: _creditsNumSteps = 6840; break;
	case Common::FR_FRA: _creditsNumSteps = 6384; break;
	}
}

void Menu::show() {
	getSharedData()->setFlag(kFlagSkipDrawScene, true);
	getScreen()->clear();
	_vm->switchEventHandler(this);
	getSound()->stopAll();
	_activeScreen = kMenuShowCredits;
	_soundResourceId = kResourceNone;
	_musicResourceId = kResourceNone;
	_gameStarted = false;
	_startIndex = 480;
	_creditsFrameIndex = 0;
	setup();
}

void Menu::setup() {
	getScreen()->clear();
	getCursor()->hide();
	getSharedData()->setFlag(kFlag1, true);

	if (_vm->isGameFlagSet(kGameFlagFinishGame)) {
		getText()->loadFont(MAKE_RESOURCE(kResourcePackShared, 32));
		getScreen()->setPalette(MAKE_RESOURCE(kResourcePackShared, 31));
		getScreen()->setGammaLevel(MAKE_RESOURCE(kResourcePackShared, 31));
		getScreen()->setupTransTables(4, MAKE_RESOURCE(kResourcePackShared, 34),
		                                 MAKE_RESOURCE(kResourcePackShared, 35),
		                                 MAKE_RESOURCE(kResourcePackShared, 36),
		                                 MAKE_RESOURCE(kResourcePackShared, 37));
		getScreen()->selectTransTable(1);
		_dword_455D4C = false;
		_dword_455D5C = false;
		getSound()->playSound(MAKE_RESOURCE(kResourcePackShared, 56), false, Config.voiceVolume);
	} else {
		getText()->loadFont(MAKE_RESOURCE(kResourcePackShared, 25));
		getScreen()->setPalette(MAKE_RESOURCE(kResourcePackShared, 26));
		getScreen()->setGammaLevel(MAKE_RESOURCE(kResourcePackShared, 26));
		getScreen()->setupTransTables(4, MAKE_RESOURCE(kResourcePackShared, 27),
		                                 MAKE_RESOURCE(kResourcePackShared, 28),
		                                 MAKE_RESOURCE(kResourcePackShared, 29),
		                                 MAKE_RESOURCE(kResourcePackShared, 30));
		getScreen()->selectTransTable(1);
		getSound()->playMusic(kResourceNone, 0);
		getSound()->playMusic(MAKE_RESOURCE(kResourcePackShared, 38));
	}
}

void Menu::leave() {
	_activeScreen = kMenuNone;
	getCursor()->set(MAKE_RESOURCE(kResourcePackShared, 2));
	getText()->loadFont(kFontYellow);
}

void Menu::switchFont(bool condition) {
	getText()->loadFont((condition) ? kFontYellow : kFontBlue);
}

void Menu::closeCredits() {
	getScreen()->clear();
	getCursor()->show();
	getSharedData()->setFlag(kFlag1, false);

	getText()->loadFont(kFontYellow);
	getScreen()->setPalette(MAKE_RESOURCE(kResourcePackShared, 17));
	getScreen()->setGammaLevel(MAKE_RESOURCE(kResourcePackShared, 17));
	getScreen()->setupTransTables(4, MAKE_RESOURCE(kResourcePackShared, 18),
	                                 MAKE_RESOURCE(kResourcePackShared, 19),
	                                 MAKE_RESOURCE(kResourcePackShared, 20),
	                                 MAKE_RESOURCE(kResourcePackShared, 21));
	getScreen()->selectTransTable(1);
	getSound()->playMusic(kResourceNone, 0);
	getSound()->playMusic(_musicResourceId);

	if (_vm->isGameFlagSet(kGameFlagFinishGame)) {
		if (!_dword_455D4C) {
			_dword_455D4C = true;
			getSound()->stop(MAKE_RESOURCE(kResourcePackShared, 56));
		}
	}
	leave();
}

Menu::MenuScreen Menu::findMousePosition() {
	for (uint i = 0; i < ARRAYSIZE(menuRects); i++)
		if (_vm->rectContains(&menuRects[i], LOGICAL_MOUSE(getCursor()->position())))
			return (MenuScreen)i;
	return kMenuNone;
}

void Menu::playTestSounds() {
	_testSoundsPlaying = true;
	getSound()->playSound(kAmbientSound, true, Config.ambientVolume);
	getSound()->playSound(kSfxSound, true, Config.sfxVolume);
	getSound()->playSound(kVoiceSound, true, Config.voiceVolume);
}

void Menu::stopTestSounds() {
	_testSoundsPlaying = false;
	getSound()->stop(kAmbientSound);
	getSound()->stop(kSfxSound);
	getSound()->stop(kVoiceSound);
}

void Menu::adjustMasterVolume(int32 delta) const {
	int32 *volume = nullptr;
	int32 volumeIndex = 1;
	do {
		switch (volumeIndex) {
		case 1: volume = &Config.musicVolume; break;
		case 2: volume = &Config.ambientVolume; break;
		case 3: volume = &Config.sfxVolume; break;
		case 4: volume = &Config.voiceVolume; break;
		case 5: volume = &Config.movieVolume; break;
		default: error("[Menu::adjustMasterVolume] Invalid volume index (%d)", volumeIndex);
		}
		if (delta >= 0) {
			if (*volume < 0) {
				if (*volume == -9999) *volume = -5000;
				*volume += 250;
				if (*volume > 0) *volume = 0;
			}
		} else {
			if (*volume > -5000) {
				*volume -= 250;
				if (*volume <= -5000) *volume = -9999;
			}
		}
		++volumeIndex;
	} while (volumeIndex < 6);
}

void Menu::adjustTestVolume() {
	getSound()->setMusicVolume(Config.musicVolume);
	if ((Config.movieVolume / 250 + 20) <= 0)
		getSound()->playMusic(_musicResourceId);
	if (getSound()->isPlaying(kAmbientSound)) getSound()->setVolume(kAmbientSound, Config.ambientVolume);
	else if (_testSoundsPlaying) getSound()->playSound(kAmbientSound, true, Config.ambientVolume);

	if (getSound()->isPlaying(kSfxSound)) getSound()->setVolume(kSfxSound, Config.sfxVolume);
	else if (_testSoundsPlaying) getSound()->playSound(kSfxSound, true, Config.sfxVolume);

	if (getSound()->isPlaying(kVoiceSound)) getSound()->setVolume(kVoiceSound, Config.voiceVolume);
	else if (_testSoundsPlaying) getSound()->playSound(kVoiceSound, true, Config.voiceVolume);
}

void Menu::setupMusic() {
	getSound()->stopAll();
	int32 index = getScene() ? getWorld()->musicCurrentResourceIndex : kMusicStopped;
	if (index == kMusicStopped) {
		_soundResourceId = kResourceNone;
		_musicResourceId = MAKE_RESOURCE(kResourcePackShared, 39);
		getSound()->playMusic(_musicResourceId);
	} else {
		_soundResourceId = kResourceNone;
		_musicResourceId = MAKE_RESOURCE(kResourcePackMusic, index);
	}
}

void Menu::adjustPerformance() {
	getSound()->stopAll();
	getSound()->playMusic(kResourceNone, 0);
	setupMusic();
	int32 index = getScene() ? getWorld()->musicCurrentResourceIndex : kMusicStopped;
	if (index != kMusicStopped) getSound()->playMusic(MAKE_RESOURCE(kResourcePackMusic, index));
}

Common::String Menu::getChapterName() {
	return Common::String::format("%s%2d", getText()->get(MAKE_RESOURCE(kResourcePackText, 1334)), chapterIndexes[getWorld()->chapter]);
}

bool Menu::handleEvent(const AsylumEvent &evt) {
	// --- HD REMASTER MOUSE SCALER ---
	AsylumEvent logicalEvt = evt;
	if (logicalEvt.type == Common::EVENT_LBUTTONDOWN || logicalEvt.type == Common::EVENT_RBUTTONDOWN || 
	    logicalEvt.type == Common::EVENT_MOUSEMOVE || logicalEvt.type == Common::EVENT_LBUTTONUP || 
	    logicalEvt.type == Common::EVENT_RBUTTONUP) {
		logicalEvt.mouse.x /= getScaleFactor();
		logicalEvt.mouse.y /= getScaleFactor();
	}
	// --------------------------------

	switch ((int32)logicalEvt.type) {
	case EVENT_ASYLUM_INIT: return init();
	case EVENT_ASYLUM_UPDATE: return update();
	case EVENT_ASYLUM_MUSIC: return music();
	case Common::EVENT_KEYDOWN: return key(logicalEvt);
	case Common::EVENT_LBUTTONDOWN:
	case Common::EVENT_RBUTTONDOWN: return click(logicalEvt);
	default: break;
	}
	return false;
}

bool Menu::init() {
	if (_showMovie) {
		_showMovie = false;
		getCursor()->set(MAKE_RESOURCE(kResourcePackShared, 3));
	} else {
		if (!_initGame) {
			_initGame = true;
			getSaveLoad()->loadMoviesViewed();
			_showMovie = !_loadingDuringStartup;
			if (!_loadingDuringStartup) getVideo()->play(0, this);
			if (!getSaveLoad()->hasSavegames()) {
				_vm->restart();
				return true;
			}
			getCursor()->show();
		}
		_caretBlink = 0;
		_activeScreen = kMenuNone;
		_currentIcon = kMenuNone;
		_dword_455C74 = 0;
		setupMusic();
		getCursor()->hide();
		getCursor()->set(MAKE_RESOURCE(kResourcePackShared, 2));
	}

	if (_gameStarted) getScene()->getActor()->stopWalking();

	getScreen()->clear();
	getText()->loadFont(kFontYellow);
	getScreen()->setPalette(MAKE_RESOURCE(kResourcePackShared, 17));
	getScreen()->setGammaLevel(MAKE_RESOURCE(kResourcePackShared, 17));
	getScreen()->setupTransTables(4, MAKE_RESOURCE(kResourcePackShared, 18),
	                                 MAKE_RESOURCE(kResourcePackShared, 19),
	                                 MAKE_RESOURCE(kResourcePackShared, 20),
	                                 MAKE_RESOURCE(kResourcePackShared, 21));
	getScreen()->selectTransTable(1);
	g_system->updateScreen();
	getCursor()->show();
	return true;
}

bool Menu::update() {
	uint32 ticks = _vm->getTick();

	if (!getSharedData()->getFlag(kFlagRedraw)) {
		getScreen()->draw(kBackground, (_activeScreen == kMenuNone) ? 1 : 0, Common::Point(0, 0));
		uint32 frameIndex = 0;

		if (!getCursor()->isHidden()) {
			Common::Point cursor = LOGICAL_MOUSE(getCursor()->position());
			if (cursor.x < 230 || cursor.x > 399 || cursor.y < 199 || cursor.y > 259)
				frameIndex = eyeFrameIndex[Actor::getAngle(Common::Point(320, 240), cursor)];
			else if (cursor.x >= 743 && cursor.x <= 743 && cursor.y >= 587 && cursor.y <= 602)
				frameIndex = 9;
		}

		if (_activeScreen == kMenuNone) {
			getScreen()->draw(kEye, frameIndex, Common::Point(0, 0));
			MenuScreen icon = findMousePosition();
			if (icon != kMenuNone) {
				getScreen()->draw(MAKE_RESOURCE(kResourcePackShared, icon + 4), _iconFrames[icon], Common::Point(0, 0));
				_iconFrames[icon] = (_iconFrames[icon] + 1) % GraphicResource::getFrameCount(_vm, MAKE_RESOURCE(kResourcePackShared, icon + 4));

				getText()->drawCentered(Common::Point(menuRects[icon][0] - 5, menuRects[icon][3] + 5),
										menuRects[icon][2] - menuRects[icon][0],
										MAKE_RESOURCE(kResourcePackText, 1309 + icon));

				if (!_dword_455C74 || _currentIcon != icon) {
					_dword_455C74 = true;
					_currentIcon = icon;
					if (_soundResourceId && getSound()->isPlaying(_soundResourceId) && _soundResourceId != MAKE_RESOURCE(kResourcePackShared, icon + 44))
						getSound()->stopAll(_soundResourceId);
					if (_soundResourceId != MAKE_RESOURCE(kResourcePackShared, icon + 44) || !getSound()->isPlaying(_soundResourceId)) {
						_soundResourceId = MAKE_RESOURCE(kResourcePackShared, icon + 44);
						getSound()->playSound(_soundResourceId, false, Config.voiceVolume);
					}
				}
			} else {
				_dword_455C74 = 0;
			}
		} else {
			getScreen()->drawTransparent(kEye, frameIndex, Common::Point(0, 0), kDrawFlagNone, 3);
			getScreen()->draw(MAKE_RESOURCE(kResourcePackShared, _activeScreen + 4), _iconFrames[_activeScreen], Common::Point(0, 0));
			_iconFrames[_activeScreen] = (_iconFrames[_activeScreen] + 1) % GraphicResource::getFrameCount(_vm, MAKE_RESOURCE(kResourcePackShared, _activeScreen + 4));
		}

		switch (_activeScreen) {
		case kMenuNewGame: updateNewGame(); break;
		case kMenuLoadGame: updateLoadGame(); break;
		case kMenuSaveGame: updateSaveGame(); break;
		case kMenuDeleteGame: updateDeleteGame(); break;
		case kMenuViewMovies: updateViewMovies(); break;
		case kMenuQuitGame: updateQuitGame(); break;
		case kMenuTextOptions: updateTextOptions(); break;
		case kMenuAudioOptions: updateAudioOptions(); break;
		case kMenuSettings: updateSettings(); break;
		case kMenuKeyboardConfig: updateKeyboardConfig(); break;
		case kMenuShowCredits: updateShowCredits(); break;
		case kMenuReturnToGame: updateReturnToGame(); break;
		default: break;
		}

		getSharedData()->setFlag(kFlagRedraw, true);
	}

	if (ticks > getSharedData()->getNextScreenUpdate()) {
		if (getSharedData()->getFlag(kFlagRedraw)) {
			getScreen()->copyBackBufferToScreen();
			getSharedData()->setFlag(kFlagRedraw, false);
			getSharedData()->setNextScreenUpdate(ticks + 55);
		}
	}
	return true;
}

bool Menu::music() {
	if (_activeScreen == kMenuShowCredits && _vm->isGameFlagSet(kGameFlagFinishGame) && !_dword_455D5C && !_dword_455D4C) {
		_dword_455D5C = true;
		getSound()->playMusic(kResourceNone, 0);
		getSound()->playMusic(MAKE_RESOURCE(kResourcePackShared, 38));
		return true;
	}
	return false;
}

bool Menu::key(const AsylumEvent &evt) {
	switch (_activeScreen) {
	case kMenuSaveGame: keySaveGame(evt); break;
	case kMenuKeyboardConfig: keyKeyboardConfig(evt); break;
	case kMenuShowCredits: keyShowCredits(); break;
	default: break;
	}
	return true;
}

bool Menu::click(const AsylumEvent &evt) {
	if (evt.type == Common::EVENT_RBUTTONDOWN && _activeScreen == kMenuShowCredits) {
		 clickShowCredits();
		 return true;
	}

	if (_activeScreen != kMenuNone) {
		switch (_activeScreen) {
		case kMenuNewGame: clickNewGame(); break;
		case kMenuLoadGame: clickLoadGame(); break;
		case kMenuSaveGame: clickSaveGame(); break;
		case kMenuDeleteGame: clickDeleteGame(); break;
		case kMenuViewMovies: clickViewMovies(); break;
		case kMenuQuitGame: clickQuitGame(); break;
		case kMenuTextOptions: clickTextOptions(); break;
		case kMenuAudioOptions: clickAudioOptions(); break;
		case kMenuSettings: clickSettings(); break;
		case kMenuKeyboardConfig: clickKeyboardConfig(); break;
		case kMenuShowCredits: clickShowCredits(); break;
		case kMenuReturnToGame: clickReturnToGame(); break;
		default: break;
		}
		return true;
	}

	_activeScreen = findMousePosition();
	if (_activeScreen == kMenuNone) return true;

	getCursor()->set(MAKE_RESOURCE(kResourcePackShared, 3));
	getText()->loadFont(kFontYellow);

	switch (_activeScreen) {
	case kMenuSaveGame: _isEditingSavegameName = false; // fallthrough
	case kMenuLoadGame:
		_dword_455C80 = false; _dword_455C78 = false; _dword_456288 = 0; _startIndex = 0; getSaveLoad()->loadList(); break;
	case kMenuDeleteGame:
		_dword_455C80 = false; _startIndex = 0; getSaveLoad()->loadList(); break;
	case kMenuViewMovies:
		_showMovie = false; _dword_455C78 = false; _dword_456288 = 0; _startIndex = 0; _movieCount = getSaveLoad()->getMoviesViewed((int32 *)&_movieList); break;
	case kMenuKeyboardConfig: _selectedShortcutIndex = -1; break;
	case kMenuReturnToGame: if (_gameStarted) clickReturnToGame(); break;
	case kMenuShowCredits: _startIndex = 480; _creditsFrameIndex = 0; setup(); break;
	default: break;
	}
	return true;
}

void Menu::updateNewGame() {
	Common::Point cursor = LOGICAL_MOUSE(getCursor()->position());
	getText()->loadFont(kFontYellow);
	getText()->drawCentered(Common::Point(10, 100), 620, MAKE_RESOURCE(kResourcePackText, 1321));
	switchFont(cursor.x < 247 || cursor.x > (247 + getText()->getWidth(MAKE_RESOURCE(kResourcePackText, 1322))) || cursor.y < 273 || cursor.y > (273 + 24));
	getText()->setPosition(Common::Point(247, 273));
	getText()->draw(MAKE_RESOURCE(kResourcePackText, 1322));
	switchFont(cursor.x < 369 || cursor.x > (369 + getText()->getWidth(MAKE_RESOURCE(kResourcePackText, 1323))) || cursor.y < 273 || cursor.y > (273 + 24));
	getText()->setPosition(Common::Point(369, 273));
	getText()->draw(MAKE_RESOURCE(kResourcePackText, 1323));
}

bool Menu::hasThumbnail(int index) {
	if (getSaveLoad()->hasSavegame(index + _startIndex))
		return _vm->getMetaEngine()->querySaveMetaInfos(_vm->getTargetName().c_str(), index + _startIndex).getThumbnail();
	return false;
}

void Menu::readThumbnail() {
	if (_thumbnailSurface.getPixels()) _thumbnailSurface.free();
	Graphics::PaletteLookup paletteLookup(getScreen()->getPalette(), 256);
	SaveStateDescriptor desc = _vm->getMetaEngine()->querySaveMetaInfos(_vm->getTargetName().c_str(), _thumbnailIndex + _startIndex);
	const Graphics::Surface *thumbnail = desc.getThumbnail();
	int w = thumbnail->w, h = thumbnail->h;
	_thumbnailSurface.create(w, h, Graphics::PixelFormat::createFormatCLUT8());
	for (int i = 0; i < w; i++)
		for (int j = 0; j < h; j++) {
			byte r, g, b;
			thumbnail->format.colorToRGB(thumbnail->getPixel(i, j), r, g, b);
			_thumbnailSurface.setPixel(i, j, paletteLookup.findBestColor(r, g, b));
		}
}

void Menu::showThumbnail() {
	int x = _thumbnailIndex < 6 ? 150 : 470;
	int y = 179 + (_thumbnailIndex % 6) * 29;
	getScreen()->draw(_thumbnailSurface, x, y);
}

void Menu::updateLoadGame() {
	Common::Point cursor = LOGICAL_MOUSE(getCursor()->position());
	char text[100];
	if (_dword_455C80) {
		getText()->loadFont(kFontYellow);
		getText()->drawCentered(Common::Point(10, 100), 620, MAKE_RESOURCE(kResourcePackText, 1329));
		snprintf(text, sizeof(text), "%s ?", getSaveLoad()->getName()->c_str());
		getText()->drawCentered(Common::Point(10, 134), 620, text);

		if (cursor.x < 247 || cursor.x > (247 + getText()->getWidth(MAKE_RESOURCE(kResourcePackText, 1330))) || cursor.y < 273 || cursor.y > (273 + 24)) getText()->loadFont(kFontYellow);
		else getText()->loadFont(kFontBlue);
		getText()->setPosition(Common::Point(247, 273));
		getText()->draw(MAKE_RESOURCE(kResourcePackText, 1330));

		if (cursor.x < 369 || cursor.x > (369 + getText()->getWidth(MAKE_RESOURCE(kResourcePackText, 1331))) || cursor.y < 273 || cursor.y > (273 + 24)) getText()->loadFont(kFontYellow);
		else getText()->loadFont(kFontBlue);
		getText()->setPosition(Common::Point(369, 273));
		getText()->draw(MAKE_RESOURCE(kResourcePackText, 1331));
		return;
	}

	getText()->loadFont(kFontYellow);
	getText()->drawCentered(Common::Point(10, 100), 620, MAKE_RESOURCE(kResourcePackText, 1325));
	int current = -1;

	if (_dword_455C78) {
		getText()->drawCentered(Common::Point(10, 190), 620, MAKE_RESOURCE(kResourcePackText, 1332));
		getText()->drawCentered(Common::Point(10, 190 + 29), 620, MAKE_RESOURCE(kResourcePackText, 1333));
		getText()->drawCentered(Common::Point(10, 190 + 53), 620, getSaveLoad()->getName()->c_str());
		++_dword_456288;
		if (_dword_456288 == 60) {
			_dword_456288 = 0; _dword_455C78 = false; getCursor()->show();
		} else if (_dword_456288 == 1) {
			getCursor()->hide();
		}
	} else {
		int32 index = 0;
		for (int16 y = 150; y < 324; y += 29) {
			if (index + _startIndex >= 25) break;
			snprintf(text, sizeof(text), "%d. %s", index + _startIndex + 1, getSaveLoad()->getName((uint32)(index + _startIndex)).c_str());
			if (cursor.x < 30 || cursor.x > (30 + getText()->getWidth(text)) || cursor.y < y  || cursor.y > (y + 24)) {
				getText()->loadFont(kFontYellow);
			} else {
				getText()->loadFont(kFontBlue);
				if (hasThumbnail(index)) current = index;
			}
			getText()->setPosition(Common::Point(30, y));
			getText()->draw(text);
			++index;
		}

		for (int16 y = 150; y < 324; y += 29) {
			if (index + _startIndex >= 25) break;
			snprintf(text, sizeof(text), "%d. %s", index + _startIndex + 1, getSaveLoad()->getName((uint32)(index + _startIndex)).c_str());
			if (cursor.x < 350 || cursor.x > (350 + getText()->getWidth(text)) || cursor.y < y   || cursor.y > (y + 24)) {
				getText()->loadFont(kFontYellow);
			} else {
				getText()->loadFont(kFontBlue);
				if (hasThumbnail(index)) current = index;
			}
			getText()->setPosition(Common::Point(350, y));
			getText()->draw(text);
			++index;
		}
	}

	if (cursor.x < 30  || cursor.x > (30 + getText()->getWidth(MAKE_RESOURCE(kResourcePackText, 1326))) || cursor.y < 340 || cursor.y > (340 + 24)) getText()->loadFont(kFontYellow);
	else getText()->loadFont(kFontBlue);
	getText()->setPosition(Common::Point(30, 340));
	getText()->draw(MAKE_RESOURCE(kResourcePackText, 1326));

	if (cursor.x < 300 || cursor.x > (300 + getText()->getWidth(MAKE_RESOURCE(kResourcePackText, 1328))) || cursor.y < 340 || cursor.y > (340 + 24)) getText()->loadFont(kFontYellow);
	else getText()->loadFont(kFontBlue);
	getText()->setPosition(Common::Point(300, 340));
	getText()->draw(MAKE_RESOURCE(kResourcePackText, 1328));

	if (cursor.x < 550 || cursor.x > (550 + getText()->getWidth(MAKE_RESOURCE(kResourcePackText, 1327))) || cursor.y < 340 || cursor.y > (340 + 24)) getText()->loadFont(kFontYellow);
	else getText()->loadFont(kFontBlue);
	getText()->setPosition(Common::Point(550, 340));
	getText()->draw(MAKE_RESOURCE(kResourcePackText, 1327));

	if (current == -1) {
		_thumbnailIndex = -1;
		return;
	}
	if (current != _thumbnailIndex) {
		_thumbnailIndex = current;
		readThumbnail();
	}
	showThumbnail();
}

void Menu::updateSaveGame() {
	Common::Point cursor = LOGICAL_MOUSE(getCursor()->position());
	char text[100];
	if (_dword_455C80) {
		getText()->loadFont(kFontYellow);
		getText()->drawCentered(Common::Point(10, 100), 620, MAKE_RESOURCE(kResourcePackText, 1339));
		snprintf(text, sizeof(text), "%s ?", getSaveLoad()->getName()->c_str());
		getText()->drawCentered(Common::Point(10, 134), 620, text);

		if (cursor.x < 247 || cursor.x > (247 + getText()->getWidth(MAKE_RESOURCE(kResourcePackText, 1340))) || cursor.y < 273 || cursor.y > (273 + 24)) getText()->loadFont(kFontYellow);
		else getText()->loadFont(kFontBlue);
		getText()->setPosition(Common::Point(247, 273));
		getText()->draw(MAKE_RESOURCE(kResourcePackText, 1340));

		if (cursor.x < 369 || cursor.x > (369 + getText()->getWidth(MAKE_RESOURCE(kResourcePackText, 1341))) || cursor.y < 273 || cursor.y > (273 + 24)) getText()->loadFont(kFontYellow);
		else getText()->loadFont(kFontBlue);
		getText()->setPosition(Common::Point(369, 273));
		getText()->draw(MAKE_RESOURCE(kResourcePackText, 1341));
		return;
	}

	getText()->loadFont(kFontYellow);
	getText()->drawCentered(Common::Point(10, 100), 620, MAKE_RESOURCE(kResourcePackText, 1335));

	if (_dword_455C78) {
		getText()->drawCentered(Common::Point(10, 220), 620, MAKE_RESOURCE(kResourcePackText, 1343));
		getText()->drawCentered(Common::Point(10, 220 + 29), 620, getSaveLoad()->getName()->c_str());
		++_dword_456288;
		if (_dword_456288 == 30) {
			_dword_456288 = 0; _dword_455C78 = false; getCursor()->show();
		}
	} else {
		int32 index = 0;
		for (int16 y = 150; y < 324; y += 29) {
			if (index + _startIndex >= 25) break;
			snprintf(text, sizeof(text), "%d. %s", index + _startIndex + 1, getSaveLoad()->getName((uint32)(index + _startIndex)).c_str());

			if (!_isEditingSavegameName) {
				if (cursor.x < 30 || cursor.x > (30 + getText()->getWidth(text)) || cursor.y < y  || cursor.y > (y + 24)) getText()->loadFont(kFontYellow);
				else getText()->loadFont(kFontBlue);
			} else {
				if (getSaveLoad()->getIndex() != (uint32)(index + _startIndex)) getText()->loadFont(kFontYellow);
				else getText()->loadFont(kFontBlue);
			}

			getText()->setPosition(Common::Point(30, y));
			getText()->draw(text);
			if (_isEditingSavegameName && getSaveLoad()->getIndex() == (uint32)(index + _startIndex)) {
				if (_caretBlink < 6) getText()->drawASCII('_');
				_caretBlink = (_caretBlink + 1) % 12;
			}
			++index;
		}

		for (int16 y = 150; y < 324; y += 29) {
			if (index + _startIndex >= 25) break;
			snprintf(text, sizeof(text), "%d. %s", index + _startIndex + 1, getSaveLoad()->getName((uint32)(index + _startIndex)).c_str());

			if (!_isEditingSavegameName) {
				if (cursor.x < 350 || cursor.x > (350 + getText()->getWidth(text)) || cursor.y < y   || cursor.y > (y + 24)) getText()->loadFont(kFontYellow);
				else getText()->loadFont(kFontBlue);
			} else {
				if (getSaveLoad()->getIndex() != (uint32)(index + _startIndex)) getText()->loadFont(kFontYellow);
				else getText()->loadFont(kFontBlue);
			}

			getText()->setPosition(Common::Point(350, y));
			getText()->draw(text);
			if (_isEditingSavegameName && getSaveLoad()->getIndex() == (uint32)(index + _startIndex)) {
				if (_caretBlink < 6) getText()->drawASCII('_');
				_caretBlink = (_caretBlink + 1) % 12;
			}
			++index;
		}
	}

	if (getCursor()->isHidden() || cursor.x < 30  || cursor.x > (30 + getText()->getWidth(MAKE_RESOURCE(kResourcePackText, 1336))) || cursor.y < 340 || cursor.y > (340 + 24)) getText()->loadFont(kFontYellow);
	else getText()->loadFont(kFontBlue);
	getText()->setPosition(Common::Point(30, 340));
	getText()->draw(MAKE_RESOURCE(kResourcePackText, 1336));

	if (getCursor()->isHidden() || cursor.x < 300 || cursor.x > (300 + getText()->getWidth(MAKE_RESOURCE(kResourcePackText, 1338))) || cursor.y < 340 || cursor.y > (340 + 24)) getText()->loadFont(kFontYellow);
	else getText()->loadFont(kFontBlue);
	getText()->setPosition(Common::Point(300, 340));
	getText()->draw(MAKE_RESOURCE(kResourcePackText, 1338));

	if (getCursor()->isHidden() || cursor.x < 550 || cursor.x > (550 + getText()->getWidth(MAKE_RESOURCE(kResourcePackText, 1337))) || cursor.y < 340 || cursor.y > (340 + 24)) getText()->loadFont(kFontYellow);
	else getText()->loadFont(kFontBlue);
	getText()->setPosition(Common::Point(550, 340));
	getText()->draw(MAKE_RESOURCE(kResourcePackText, 1337));
}

void Menu::updateDeleteGame() {
	Common::Point cursor = LOGICAL_MOUSE(getCursor()->position());
	char text[100];
	if (_dword_455C80) {
		getText()->loadFont(kFontYellow);
		getText()->drawCentered(Common::Point(10, 100), 620, MAKE_RESOURCE(kResourcePackText, 1349));
		snprintf(text, sizeof(text), "%s ?", getSaveLoad()->getName()->c_str());
		getText()->drawCentered(Common::Point(10, 134), 620, text);

		if (cursor.x < 247 || cursor.x > (247 + getText()->getWidth(MAKE_RESOURCE(kResourcePackText, 1350))) || cursor.y < 273 || cursor.y > (273 + 24)) getText()->loadFont(kFontYellow);
		else getText()->loadFont(kFontBlue);
		getText()->setPosition(Common::Point(247, 273));
		getText()->draw(MAKE_RESOURCE(kResourcePackText, 1350));

		if (cursor.x < 369 || cursor.x > (369 + getText()->getWidth(MAKE_RESOURCE(kResourcePackText, 1351))) || cursor.y < 273 || cursor.y > (273 + 24)) getText()->loadFont(kFontYellow);
		else getText()->loadFont(kFontBlue);
		getText()->setPosition(Common::Point(369, 273));
		getText()->draw(MAKE_RESOURCE(kResourcePackText, 1351));
		return;
	}

	getText()->loadFont(kFontYellow);
	getText()->drawCentered(Common::Point(10, 100), 620, MAKE_RESOURCE(kResourcePackText, 1345));

	int32 index = 0;
	for (int16 y = 150; y < 324; y += 29) {
		if (index + _startIndex >= 25) break;
		snprintf(text, sizeof(text), "%d. %s", index + _startIndex + 1, getSaveLoad()->getName((uint32)(index + _startIndex)).c_str());
		if (cursor.x < 30 || cursor.x > (30 + getText()->getWidth(text)) || cursor.y < y  || cursor.y > (y + 24)) getText()->loadFont(kFontYellow);
		else getText()->loadFont(kFontBlue);
		getText()->setPosition(Common::Point(30, y));
		getText()->draw(text);
		++index;
	}

	for (int16 y = 150; y < 324; y += 29) {
		if (index + _startIndex >= 25) break;
		snprintf(text, sizeof(text), "%d. %s", index + _startIndex + 1, getSaveLoad()->getName((uint32)(index + _startIndex)).c_str());
		if (cursor.x < 350 || cursor.x > (350 + getText()->getWidth(text)) || cursor.y < y   || cursor.y > (y + 24)) getText()->loadFont(kFontYellow);
		else getText()->loadFont(kFontBlue);
		getText()->setPosition(Common::Point(350, y));
		getText()->draw(text);
		++index;
	}

	if (cursor.x < 30  || cursor.x > (30 + getText()->getWidth(MAKE_RESOURCE(kResourcePackText, 1346))) || cursor.y < 340 || cursor.y > (340 + 24)) getText()->loadFont(kFontYellow);
	else getText()->loadFont(kFontBlue);
	getText()->setPosition(Common::Point(30, 340));
	getText()->draw(MAKE_RESOURCE(kResourcePackText, 1346));

	if (cursor.x < 300 || cursor.x > (300 + getText()->getWidth(MAKE_RESOURCE(kResourcePackText, 1348))) || cursor.y < 340 || cursor.y > (340 + 24)) getText()->loadFont(kFontYellow);
	else getText()->loadFont(kFontBlue);
	getText()->setPosition(Common::Point(300, 340));
	getText()->draw(MAKE_RESOURCE(kResourcePackText, 1348));

	if (cursor.x < 550 || cursor.x > (550 + getText()->getWidth(MAKE_RESOURCE(kResourcePackText, 1347))) || cursor.y < 340 || cursor.y > (340 + 24)) getText()->loadFont(kFontYellow);
	else getText()->loadFont(kFontBlue);
	getText()->setPosition(Common::Point(550, 340));
	getText()->draw(MAKE_RESOURCE(kResourcePackText, 1347));
}

void Menu::updateViewMovies() {
	Common::Point cursor = LOGICAL_MOUSE(getCursor()->position());
	char text[100];
	char text2[100];

	if (!_dword_455C78) {
		getText()->loadFont(kFontYellow);
		snprintf(text2, sizeof(text2), getText()->get(MAKE_RESOURCE(kResourcePackText, 1352)), getSharedData()->cdNumber);
		getText()->drawCentered(Common::Point(10, 100), 620, text2);

		int32 index = _startIndex;
		for (int16 y = 150; y < 324; y += 29) {
			if (index >= ARRAYSIZE(_movieList)) break;
			if (_movieList[index] != -1) {
				snprintf(text, sizeof(text), "%d. %s", index + 1, getText()->get(MAKE_RESOURCE(kResourcePackText, 1359 + _movieList[index])));
				snprintf(text2, sizeof(text2), getText()->get(MAKE_RESOURCE(kResourcePackText, 1356)), moviesCd[_movieList[index]]);
				Common::strcat_s(text, text2);
				if (getCursor()->isHidden() || cursor.x < 30 || cursor.x > (30 + getText()->getWidth(text)) || cursor.y < y || cursor.y > (y + 24)) getText()->loadFont(kFontYellow);
				else getText()->loadFont(kFontBlue);
				getText()->setPosition(Common::Point(30, y));
				getText()->draw(text);
			}
			++index;
		}

		for (int16 y = 150; y < 324; y += 29) {
			if (index >= ARRAYSIZE(_movieList)) break;
			if (_movieList[index] != -1) {
				snprintf(text, sizeof(text), "%d. %s", index + 1, getText()->get(MAKE_RESOURCE(kResourcePackText, 1359 + _movieList[index])));
				snprintf(text2, sizeof(text2), getText()->get(MAKE_RESOURCE(kResourcePackText, 1356)), moviesCd[_movieList[index]]);
				Common::strcat_s(text, text2);
				if (getCursor()->isHidden() || cursor.x < 350 || cursor.x > (350 + getText()->getWidth(text)) || cursor.y < y || cursor.y > (y + 24)) getText()->loadFont(kFontYellow);
				else getText()->loadFont(kFontBlue);
				getText()->setPosition(Common::Point(350, y));
				getText()->draw(text);
			}
			index++;
		}

		if (getCursor()->isHidden() || cursor.x < 30 || cursor.x > (30 + getText()->getWidth(MAKE_RESOURCE(kResourcePackText, 1353))) || cursor.y < 340 || cursor.y > (340 + 24)) getText()->loadFont(kFontYellow);
		else getText()->loadFont(kFontBlue);
		getText()->setPosition(Common::Point(30, 340));
		getText()->draw(MAKE_RESOURCE(kResourcePackText, 1353));

		if (getCursor()->isHidden() || cursor.x < 300 || cursor.x > (300 + getText()->getWidth(MAKE_RESOURCE(kResourcePackText, 1355))) || cursor.y < 340 || cursor.y > (340 + 24)) getText()->loadFont(kFontYellow);
		else getText()->loadFont(kFontBlue);
		getText()->setPosition(Common::Point(300, 340));
		getText()->draw(MAKE_RESOURCE(kResourcePackText, 1355));

		if (getCursor()->isHidden() || cursor.x < 550 || cursor.x > (550 + getText()->getWidth(MAKE_RESOURCE(kResourcePackText, 1354))) || cursor.y < 340 || cursor.y > (340 + 24)) getText()->loadFont(kFontYellow);
		else getText()->loadFont(kFontBlue);
		getText()->setPosition(Common::Point(550, 340));
		getText()->draw(MAKE_RESOURCE(kResourcePackText, 1354));

		if (_showMovie) {
			getSound()->playMusic(0, 0);
			getVideo()->play(_movieIndex, this);
			getSound()->playMusic(_musicResourceId);
		}
		return;
	}

	getText()->loadFont(kFontYellow);
	snprintf(text2, sizeof(text2), getText()->get(MAKE_RESOURCE(kResourcePackText, 1357)), getSharedData()->cdNumber);
	getText()->drawCentered(Common::Point(10, 100), 620, text2);
	Common::strlcpy(text, getText()->get(MAKE_RESOURCE(kResourcePackText, 1359 + _movieIndex)), sizeof(text));
	snprintf(text2, sizeof(text2), getText()->get(MAKE_RESOURCE(kResourcePackText, 1356)), moviesCd[_movieIndex]);
	Common::strcat_s(text, text2);
	getText()->drawCentered(Common::Point(10, 134), 620, text);
	getText()->drawCentered(Common::Point(10, 168), 620, getText()->get(MAKE_RESOURCE(kResourcePackText, 1358)));
	++_dword_456288;
	if (_dword_456288 == 90) {
		_dword_456288 = 0; _dword_455C78 = false; getCursor()->show();
	}
}

void Menu::updateQuitGame() {
	Common::Point cursor = LOGICAL_MOUSE(getCursor()->position());
	getText()->loadFont(kFontYellow);
	getText()->drawCentered(Common::Point(10, 100), 620, MAKE_RESOURCE(kResourcePackText, 1408));

	switchFont(cursor.x < 247 || cursor.x > (247 + getText()->getWidth(MAKE_RESOURCE(kResourcePackText, 1409))) || cursor.y < 273 || cursor.y > (273 + 24));
	getText()->setPosition(Common::Point(247, 273));
	getText()->draw(MAKE_RESOURCE(kResourcePackText, 1409));

	switchFont(cursor.x < 369 || cursor.x > (369 + getText()->getWidth(MAKE_RESOURCE(kResourcePackText, 1410))) || cursor.y < 273 || cursor.y > (273 + 24));
	getText()->setPosition(Common::Point(369, 273));
	getText()->draw(MAKE_RESOURCE(kResourcePackText, 1410));
}

void Menu::updateTextOptions() {
	Common::Point cursor = LOGICAL_MOUSE(getCursor()->position());
	getText()->loadFont(kFontYellow);
	getText()->drawCentered(Common::Point(10, 100), 620, MAKE_RESOURCE(kResourcePackText, 1411));
	getText()->draw(Common::Point(320, 150), MAKE_RESOURCE(kResourcePackText, 1412));

	switchFont(cursor.x < 350 || cursor.x > (350 + getText()->getWidth(MAKE_RESOURCE(kResourcePackText, Config.showMovieSubtitles ? 1414 : 1415))) || cursor.y < 150 || cursor.y > 174);
	getText()->setPosition(Common::Point(350, 150));
	getText()->draw(MAKE_RESOURCE(kResourcePackText, Config.showMovieSubtitles ? 1414 : 1415));

	getText()->loadFont(kFontYellow);
	getText()->draw(Common::Point(320, 179), MAKE_RESOURCE(kResourcePackText, 1413));

	switchFont(cursor.x < 350 || cursor.x > (350 + getText()->getWidth(MAKE_RESOURCE(kResourcePackText, Config.showEncounterSubtitles ? 1414 : 1415))) || cursor.y < 179 || cursor.y > 203);
	getText()->setPosition(Common::Point(350, 179));
	getText()->draw(MAKE_RESOURCE(kResourcePackText, Config.showEncounterSubtitles ? 1414 : 1415));

	switchFont(cursor.x < 300 || cursor.x > (300 + getText()->getWidth(MAKE_RESOURCE(kResourcePackText, 1416))) || cursor.y < 340 || cursor.y > (340 + 24));
	getText()->setPosition(Common::Point(300, 340));
	getText()->draw(MAKE_RESOURCE(kResourcePackText, 1416));
}

void Menu::updateAudioOptions() {
	Common::Point cursor = LOGICAL_MOUSE(getCursor()->position());
	int32 sizeMinus	= getText()->getWidth("-");
	int32 sizePlus  = getText()->getWidth("+");

	if (cursor.x >= 220 && cursor.x <= (220 + getText()->getWidth(MAKE_RESOURCE(kResourcePackText, 1430))) && cursor.y >= 360 && cursor.y <= (360 + 24)) {
		stopTestSounds();
		Config.write();
		_vm->syncSoundSettings();
		leave();
		return;
	}

	if (cursor.x < 360 || cursor.x > (360 + getText()->getWidth(MAKE_RESOURCE(kResourcePackText, 1431))) || cursor.y < 360 || cursor.y > (360 + 24)) {
		int32 volumeIndex = 0;
		int32 defaultVolume = 0;
		int32 *volume = &defaultVolume;
		bool found = false;

		for (;;) {
			if (found) return;

			switch (volumeIndex) {
			case 0:
				if (cursor.x >= 350 && cursor.x <= (350 + sizeMinus) && cursor.y >= 150 && cursor.y <= 174) {
					adjustMasterVolume(-1); adjustTestVolume(); found = true; break;
				}
				if (cursor.x >= (360 + sizeMinus) && cursor.x <= (360 + sizeMinus + sizePlus) && cursor.y >= 150 && cursor.y <= 174) {
					adjustMasterVolume(1); adjustTestVolume(); found = true; break;
				}
				break;
			case 1: volume = &Config.musicVolume; break;
			case 2: volume = &Config.ambientVolume; break;
			case 3: volume = &Config.sfxVolume; break;
			case 4: volume = &Config.voiceVolume; break;
			case 5: volume = &Config.movieVolume; break;
			default: break;
			}

			if (!found) {
				if (cursor.x < 350 || cursor.x > (350 + sizeMinus) || cursor.y < (29 * volumeIndex + 150) || cursor.y > (29 * (volumeIndex + 6))) {
					if (cursor.x >= (sizeMinus + 360)) {
						if (cursor.x <= (sizeMinus + sizePlus + 360)) {
							if (cursor.y >= (29 * volumeIndex + 150) && cursor.y <= (29 * (volumeIndex + 6))) {
								if (*volume < 0) {
									if (*volume == -9999) *volume = -5000;
									*volume += 250;
									if (*volume > 0) *volume = 0;
									adjustTestVolume();
								}
								found = true;
							}
						}
					}
				} else {
					if (*volume > -5000) {
						*volume -= 250;
						if (*volume <= -5000) *volume = -9999;
						adjustTestVolume();
					}
					found = true;
				}
			}

			++volumeIndex;
			if (volumeIndex >= 6) {
				if (!found) {
					if (cursor.x >= 350 && cursor.x <= (350 + getText()->getWidth(MAKE_RESOURCE(kResourcePackText, Config.reverseStereo ? 1428 : 1429)))
					 && cursor.y >= (29 * volumeIndex + 150) && cursor.y <= (29 * (volumeIndex + 6))) {
						Config.reverseStereo = !Config.reverseStereo;
						_vm->updateReverseStereo();
					}
				}
				return;
			}
		}
	}

	if (_testSoundsPlaying) stopTestSounds();
	else playTestSounds();
}

void Menu::clickSettings() {
	Common::Point cursor = LOGICAL_MOUSE(getCursor()->position());
	int32 sizeMinus	= getText()->getWidth("-");
	int32 sizePlus  = getText()->getWidth("+");

	if (cursor.x >= 300 && cursor.x <= (300 + getText()->getWidth(MAKE_RESOURCE(kResourcePackText, 1437))) && cursor.y >= 340 && cursor.y <= (340 + 24)) {
		Config.write();
		leave();
		return;
	}

	if (cursor.x >= 350 && cursor.x <= (sizeMinus + 350) && cursor.y >= 179 && cursor.y <= 203) {
		if (!Config.performance) return;
		Config.performance -= 1;
		adjustPerformance();
		return;
	}

	if (cursor.x >= sizeMinus + 360 && cursor.x <= (sizeMinus + sizePlus + 360) && cursor.y >= 179 && cursor.y <= 203) {
		if (Config.performance >= 5) return;
		Config.performance += 1;
		adjustPerformance();
		return;
	}

	if (cursor.x >= 350 && cursor.x <= (sizeMinus + 350) && cursor.y >= 150 && cursor.y <= 174) {
		if (!Config.gammaLevel) return;
		Config.gammaLevel -= 1;
		getScreen()->setGammaLevel(MAKE_RESOURCE(kResourcePackShared, 17));
		return;
	}

	if (cursor.x >= (sizeMinus + 360) && cursor.x <= (sizeMinus + sizePlus + 360) && cursor.y >= 150 && cursor.y <= 174) {
		if (Config.gammaLevel >= 8) return;
		Config.gammaLevel += 1;
		getScreen()->setGammaLevel(MAKE_RESOURCE(kResourcePackShared, 17));
		return;
	}

	if (cursor.x >= 350 && cursor.x <= (sizeMinus + 350) && cursor.y >= 209 && cursor.y <= 233) {
		if (Config.animationsSpeed == 1) return;
		Config.animationsSpeed--;
		return;
	}

	if (cursor.x >= (sizeMinus + 360) && cursor.x <= (sizeMinus + sizePlus + 360) && cursor.y >= 209 && cursor.y <= 233) {
		if (Config.animationsSpeed == 9) return;
		Config.animationsSpeed++;
		return;
	}
}

void Menu::clickKeyboardConfig() {
	Common::Point cursor = LOGICAL_MOUSE(getCursor()->position());

	if (cursor.x < 300 || cursor.x > (300 + getText()->getWidth(MAKE_RESOURCE(kResourcePackText, 1446))) || cursor.y < 340 || cursor.y > (340 + 24)) {
		int32 keyIndex = 0;
		Common::Keymap *keymap = g_system->getEventManager()->getKeymapper()->getKeymap("asylum");

		do {
			Common::Array<Common::HardwareInput> mappings = keymap->getActionMapping(keymap->getActions()[keyIndex]);
			Common::String keyCode = mappings.size() ? mappings[0].description.encode() : "<Not mapped>";

			if (cursor.x >= 350 && cursor.x <= (350 + getText()->getWidth(keyCode.c_str())) && cursor.y >= (29 * keyIndex + 150) && cursor.y <= (29 * (keyIndex + 6))) {
				_selectedShortcutIndex = keyIndex;
				getCursor()->hide();
			}
			++keyIndex;
		} while (keyIndex < 6);
	} else {
		Config.write();
		leave();
	}
}

void Menu::clickReturnToGame() {
	if (_gameStarted) {
		if (_musicResourceId != MAKE_RESOURCE(kResourcePackMusic, getWorld()->musicCurrentResourceIndex))
			getSound()->playMusic(kResourceNone, 0);
		getScreen()->clear();
		_vm->switchEventHandler(_vm->scene());
	} else {
		Common::Point cursor = LOGICAL_MOUSE(getCursor()->position());
		if (cursor.x >= 285 && cursor.x <= (285 + getText()->getWidth(MAKE_RESOURCE(kResourcePackText, 1811))) && cursor.y >= 273 && cursor.y <= (273 + 24))
			leave();
	}
}

void Menu::clickShowCredits() {
	closeCredits();
}

void Menu::keySaveGame(const AsylumEvent &evt) {
	if (!_isEditingSavegameName) return;

	switch (evt.kbd.keycode) {
	default:
		if (evt.kbd.ascii > 255 || !Common::isPrint(evt.kbd.ascii)) break;
		if (getSaveLoad()->getName()->size() < 44) {
			int32 width = getText()->getWidth(getSaveLoad()->getName()->c_str());
			bool test = false;
			if ((getSaveLoad()->getIndex() % 12) >= 6) test = (width + _prefixWidth + 350 < 630);
			else test = (width + _prefixWidth + 30 < 340);
			if (test) *getSaveLoad()->getName() += (char)evt.kbd.ascii;
		}
		break;
	case Common::KEYCODE_RETURN:
	case Common::KEYCODE_KP_ENTER:
		_isEditingSavegameName = false;
		getSaveLoad()->save();
		break;
	case Common::KEYCODE_ESCAPE:
		_dword_455C80 = false;
		_isEditingSavegameName = false;
		*getSaveLoad()->getName() = _previousName;
		getCursor()->show();
		break;
	case Common::KEYCODE_BACKSPACE:
	case Common::KEYCODE_DELETE:
		if (getSaveLoad()->getName()->size()) getSaveLoad()->getName()->deleteLastChar();
		break;
	case Common::KEYCODE_KP_PERIOD:
		*getSaveLoad()->getName() = "";
		break;
	}
}

void Menu::keyKeyboardConfig(const AsylumEvent &evt) {
	if (_selectedShortcutIndex == -1) return;
	if (evt.kbd.keycode == Common::KEYCODE_ESCAPE || evt.kbd.keycode == Common::KEYCODE_RETURN || evt.kbd.keycode == Common::KEYCODE_KP_ENTER) {
		_selectedShortcutIndex = -1;
		getCursor()->show();
		return;
	}
	if (evt.kbd.ascii > 255 || !Common::isAlnum(evt.kbd.ascii)) return;

	Common::Keymapper *keymapper = g_system->getEventManager()->getKeymapper();
	Common::Keymap    *keymap    = keymapper->getKeymap("asylum");
	Common::Action    *action    = keymap->getActions()[_selectedShortcutIndex];

	keymap->unregisterMapping(action);
	keymap->registerMapping(action, keymapper->findHardwareInput(evt));
	keymap->saveMappings();

	_selectedShortcutIndex = -1;
	getCursor()->show();
}

void Menu::keyShowCredits() {
	closeCredits();
}

} // end of namespace Asylum
