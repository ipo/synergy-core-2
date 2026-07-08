/*
 * synergy -- mouse and keyboard sharing utility
 * Copyright (C) 2012-2016 Symless Ltd.
 * Copyright (C) 2011 Nick Bolton
 * 
 * This package is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * found in the file LICENSE that should have accompanied this file.
 * 
 * This package is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#define TEST_ENV

#include "test/mock/synergy/MockEventQueue.h"
#include "test/mock/synergy/MockKeyMap.h"
#include "platform/MSWindowsKeyState.h"
#include "platform/MSWindowsDesks.h"
#include "platform/MSWindowsScreen.h"
#include "platform/MSWindowsScreenSaver.h"
#include "base/TMethodJob.h"

#include "test/global/gtest.h"
#include "test/global/gmock.h"

#include <vector>

// wParam = flags, HIBYTE(lParam) = virtual key, LOBYTE(lParam) = scan code
#define SYNERGY_MSG_FAKE_KEY		SYNERGY_HOOK_LAST_MSG + 4

using ::testing::_;
using ::testing::NiceMock;

namespace {

const KeyID kAtSign = 0x0040;
const KeyModifierMask kControlAlt = KeyModifierControl | KeyModifierAlt;

class TestableMSWindowsKeyState : public MSWindowsKeyState {
public:
	TestableMSWindowsKeyState(
		MSWindowsDesks* desks, void* eventTarget, IEventQueue* events,
		synergy::KeyMap& keyMap) :
		MSWindowsKeyState(desks, eventTarget, events, keyMap)
	{
	}

	using MSWindowsKeyState::getKeyMap;
};

struct AtSignGroups {
	std::vector<SInt32> altGrGroups;
	std::vector<SInt32> shiftGroups;
};

bool
containsGroup(const std::vector<SInt32>& groups, SInt32 group)
{
	for (std::vector<SInt32>::const_iterator i = groups.begin();
		 i != groups.end(); ++i) {
		if (*i == group) {
			return true;
		}
	}
	return false;
}

void
collectAtSignGroups(KeyID id, SInt32 group,
	synergy::KeyMap::KeyItem& item, void* userData)
{
	if (id != kAtSign) {
		return;
	}

	AtSignGroups* groups = static_cast<AtSignGroups*>(userData);
	const bool requiresAltGr =
		(item.m_required & KeyModifierAltGr) == KeyModifierAltGr &&
		(item.m_sensitive & KeyModifierAltGr) == KeyModifierAltGr;
	const bool requiresControlAlt =
		(item.m_required & kControlAlt) == kControlAlt &&
		(item.m_sensitive & kControlAlt) == kControlAlt;
	const bool requiresShift =
		(item.m_required & KeyModifierShift) == KeyModifierShift &&
		(item.m_sensitive & KeyModifierShift) == KeyModifierShift &&
		(item.m_required & (KeyModifierAltGr | kControlAlt)) == 0;

	if ((requiresAltGr || requiresControlAlt) &&
		!containsGroup(groups->altGrGroups, group)) {
		groups->altGrGroups.push_back(group);
	}
	if (requiresShift && !containsGroup(groups->shiftGroups, group)) {
		groups->shiftGroups.push_back(group);
	}
}

bool
findNonRestoreGroupChange(const synergy::KeyMap::Keystrokes& keys,
	SInt32* group)
{
	for (synergy::KeyMap::Keystrokes::const_iterator i = keys.begin();
		 i != keys.end(); ++i) {
		if (i->m_type == synergy::KeyMap::Keystroke::kGroup &&
			!i->m_data.m_group.m_restore) {
			*group = i->m_data.m_group.m_group;
			return true;
		}
	}
	return false;
}

} // namespace

class MSWindowsKeyStateTests : public ::testing::Test
{
protected:
	virtual void SetUp()
	{
		m_hook.loadLibrary(TRUE);
		m_screensaver = new MSWindowsScreenSaver();
	}

	virtual void TearDown()
	{
		delete m_screensaver;
	}

	MSWindowsDesks* newDesks(IEventQueue* eventQueue)
	{
		return new MSWindowsDesks(
            true, false, m_screensaver, eventQueue,
			new TMethodJob<MSWindowsKeyStateTests>(
				this, &MSWindowsKeyStateTests::updateKeysCB), false);
	}

	void* getEventTarget() const
	{
		return const_cast<MSWindowsKeyStateTests*>(this);
	}

private:
	void updateKeysCB(void*) { }
	IScreenSaver* m_screensaver;
	MSWindowsHook m_hook;
};

TEST_F(MSWindowsKeyStateTests, disable_eventQueueNotUsed)
{
	NiceMock<MockEventQueue> eventQueue;
	MSWindowsDesks* desks = newDesks(&eventQueue);
	MockKeyMap keyMap;
	MSWindowsKeyState keyState(desks, getEventTarget(), &eventQueue, keyMap);
	
	EXPECT_CALL(eventQueue, removeHandler(_, _)).Times(0);

	keyState.disable();
	delete desks;
}

TEST_F(MSWindowsKeyStateTests, testAutoRepeat_noRepeatAndButtonIsZero_resultIsTrue)
{
	NiceMock<MockEventQueue> eventQueue;
	MSWindowsDesks* desks = newDesks(&eventQueue);
	MockKeyMap keyMap;
	MSWindowsKeyState keyState(desks, getEventTarget(), &eventQueue, keyMap);
	keyState.setLastDown(1);

	bool actual = keyState.testAutoRepeat(true, false, 1);

	ASSERT_TRUE(actual);
	delete desks;
}

TEST_F(MSWindowsKeyStateTests, testAutoRepeat_pressFalse_lastDownIsZero)
{
	NiceMock<MockEventQueue> eventQueue;
	MSWindowsDesks* desks = newDesks(&eventQueue);
	MockKeyMap keyMap;
	MSWindowsKeyState keyState(desks, getEventTarget(), &eventQueue, keyMap);
	keyState.setLastDown(1);

	keyState.testAutoRepeat(false, false, 1);

	ASSERT_EQ(0, keyState.getLastDown());
	delete desks;
}

TEST_F(MSWindowsKeyStateTests, saveModifiers_noModifiers_savedModifiers0)
{
	NiceMock<MockEventQueue> eventQueue;
	MSWindowsDesks* desks = newDesks(&eventQueue);
	MockKeyMap keyMap;
	MSWindowsKeyState keyState(desks, getEventTarget(), &eventQueue, keyMap);

	keyState.saveModifiers();

	ASSERT_EQ(0, keyState.getSavedModifiers());
	delete desks;
}

TEST_F(MSWindowsKeyStateTests, generatedSwedishAltGrAtSign_doesNotSwitchGroups)
{
	NiceMock<MockEventQueue> eventQueue;
	MSWindowsDesks* desks = newDesks(&eventQueue);
	synergy::KeyMap keyMap;
	TestableMSWindowsKeyState keyState(
		desks, getEventTarget(), &eventQueue, keyMap);

	keyState.getKeyMap(keyMap);
	keyMap.finish();

	AtSignGroups groups;
	keyMap.foreachKey(&collectAtSignGroups, &groups);
	if (groups.altGrGroups.empty() || groups.shiftGroups.empty()) {
		SUCCEED() <<
			"test requires loaded Swedish-like AltGr @ layout and US-like Shift+2 @ layout";
		delete desks;
		return;
	}

	const SInt32 startingGroup = groups.altGrGroups.front();
	synergy::KeyMap::ModifierToKeys activeModifiers;
	KeyModifierMask currentState = 0;

	synergy::KeyMap::Keystrokes altGrKeys;
	const synergy::KeyMap::KeyItem* altGrItem = keyMap.mapKey(
		altGrKeys, kKeyAltGr, startingGroup, activeModifiers,
		currentState, 0, false);
	EXPECT_NE(static_cast<const synergy::KeyMap::KeyItem*>(NULL), altGrItem) <<
		"Windows Right Alt must be modeled as AltGr for AltGr character mapping";

	synergy::KeyMap::Keystrokes atKeys;
	const synergy::KeyMap::KeyItem* atItem = keyMap.mapKey(
		atKeys, kAtSign, startingGroup, activeModifiers,
		currentState, KeyModifierAltGr, false);

	ASSERT_NE(static_cast<const synergy::KeyMap::KeyItem*>(NULL), atItem);
	EXPECT_EQ(startingGroup, atItem->m_group) <<
		"AltGr+2/@ should stay in the Swedish AltGr group";

	SInt32 switchedGroup = -1;
	EXPECT_FALSE(findNonRestoreGroupChange(atKeys, &switchedGroup)) <<
		"AltGr+2/@ emitted a non-restore group change to group " <<
		switchedGroup;

	delete desks;
}

TEST_F(MSWindowsKeyStateTests, testKoreanLocale_inputModeKey_resultCorrectKeyID)
{
	NiceMock<MockEventQueue> eventQueue;
	MSWindowsDesks* desks = newDesks(&eventQueue);
	MockKeyMap keyMap;
	MSWindowsKeyState keyState(desks, getEventTarget(), &eventQueue, keyMap);

	keyState.setKeyLayout((HKL)0x00000412u);	// for ko-KR local ID
	ASSERT_EQ(0xEF31, keyState.getKeyID(0x15u, 0x1f2u));	// VK_HANGUL from Hangul key
	ASSERT_EQ(0xEF34, keyState.getKeyID(0x19u, 0x1f1u));	// VK_HANJA from Hanja key
	ASSERT_EQ(0xEF31, keyState.getKeyID(0x15u, 0x11du));	// VK_HANGUL from R-Alt key
	ASSERT_EQ(0xEF34, keyState.getKeyID(0x19u, 0x138u));	// VK_HANJA from R-Ctrl key

	keyState.setKeyLayout((HKL)0x00000411); // for ja-jp locale ID
	ASSERT_EQ(0xEF26, keyState.getKeyID(0x15u, 0x1du));	// VK_KANA
	ASSERT_EQ(0xEF2A, keyState.getKeyID(0x19u, 0x38u));	// VK_KANJI

	delete desks;
}
